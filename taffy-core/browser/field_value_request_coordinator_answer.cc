// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

// The answer path, in a translation unit of its own.
//
// Split on the responsibility rather than on length: everything here runs
// after a person has typed something, and it is the only code in TaffyGo that
// is ever holding bytes destined for a form field outside the vault. Keeping
// it in one small file is what lets a reviewer check the two properties it
// exists for without reading anything else —
//
//   * every value is **moved** into the mint and the local it came from is
//     overwritten, so nothing that walks off the end of this file is holding
//     a copy; and
//   * what leaves for the isolated core is a **count**, composed only after
//     every mint has already happened or been refused by name.

namespace taffy {

namespace {

namespace surface_mojom = browser::field_values::mojom;

uint64_t FieldApprovalMonotonicMillis(base::TimeTicks time) {
  const int64_t value = time.since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t FieldApprovalUtcMillis(base::Time time) {
  const int64_t value = time.InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool SamePage(const FieldValueDocument& expected,
              const FieldValueDocument& current) {
  return expected.host == current.host &&
         expected.normalized_origin == current.normalized_origin &&
         expected.frame_id == current.frame_id &&
         expected.page_epoch == current.page_epoch &&
         current.graph_revision >= expected.graph_revision;
}

surface_mojom::FieldValueRefusalPtr Refused(
    const std::string& field_id,
    surface_mojom::FieldValueRefusalReason reason) {
  auto refusal = surface_mojom::FieldValueRefusal::New();
  refusal->field_id = field_id;
  refusal->reason = reason;
  return refusal;
}

// Overwrites a value the browser is not going to hold.
//
// It is not the vault's scrub — this string never reached the vault — and
// that is exactly why it is here. A refused answer is still a value a person
// typed, sitting in a heap block this process is about to release, and the
// window in which a crash dump could carry it closes when the block is
// overwritten rather than when the process ends.
void Discard(std::string& value) {
  crypto::SecureZeroBuffer(base::as_writable_byte_span(value));
  value.clear();
}

}  // namespace

void FieldValueRequestCoordinator::Supply(
    const std::string& request_id,
    const std::vector<std::string>& values,
    SupplyCallback callback) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    // Never opened, already answered, or closed underneath the surface. A
    // second answer to one request lands here, and that is the whole of "a
    // request is answered once": the record is taken out of the map below
    // before anything is minted, so there is no window in which two answers
    // are both in flight for the same request.
    std::move(callback).Run(
        surface_mojom::FieldValueSupplyVerdict::kUnknownRequest,
        std::vector<surface_mojom::FieldValueRefusalPtr>());
    return;
  }
  const bool interactive =
      it->second.descriptors.size() == 1u &&
      it->second.descriptors.front()->challenge ==
          surface_mojom::FieldChallengeKind::kInteractiveChallenge;
  if ((interactive ? !values.empty()
                   : values.size() != it->second.fields.size()) ||
      values.size() > kMaxSuppliedFieldValues) {
    // A confirmation is over the complete ordered form. A prefix would let a
    // later proposal choose which displayed values the person actually
    // approved, and a suffix or reorder cannot be represented by the count
    // that crosses into the isolated core.
    std::move(callback).Run(surface_mojom::FieldValueSupplyVerdict::kMalformed,
                            std::vector<surface_mojom::FieldValueRefusalPtr>());
    return;
  }
  const std::optional<FieldValueDocument> document =
      resolve_document_.Run(it->second.tab_id);
  if (!document || !SamePage(it->second.document, *document)) {
    const std::string task_id = it->second.task_id;
    AbandonRequest(request_id, task_id,
                   surface_mojom::FieldValueCloseReason::kRevoked,
                   "page-moved-while-typing",
                   core_service::mojom::FieldValueAskOutcome::kPageMoved);
    std::move(callback).Run(
        surface_mojom::FieldValueSupplyVerdict::kUnknownRequest,
        std::vector<surface_mojom::FieldValueRefusalPtr>());
    return;
  }

  auto answer = std::make_unique<PendingAnswer>();
  answer->request_id = request_id;
  answer->request = std::move(it->second);
  answer->request.document = *document;
  // A copy of the person's bytes, because mojo hands them over as a const
  // reference. It is the only copy, it lives for the length of this settle,
  // and every element is either moved into the vault or overwritten below.
  answer->values = values;
  answer->expires_at = base::TimeTicks::Now() + kFieldValueLifetime;
  answer->expires_at_utc = base::Time::Now() + kFieldValueLifetime;
  answer->callback = std::move(callback);
  open_.erase(it);
  SettleNextField(std::move(answer));
}

void FieldValueRequestCoordinator::SettleNextField(
    std::unique_ptr<PendingAnswer> answer) {
  if (answer->prefix_closed) {
    // The core receives a count and derives positions [0, count). It cannot
    // represent a hole. Refuse and scrub the remaining suffix without even
    // resolving it, so every reported position names a value the vault really
    // holds and no unreported position can resolve by accident.
    while (answer->index < answer->values.size()) {
      const uint32_t index = answer->index++;
      Discard(answer->values[index]);
      answer->refused.push_back(
          Refused(answer->request.fields[index].field_id,
                  surface_mojom::FieldValueRefusalReason::kNotHeld));
    }
  }
  if (answer->index >= answer->values.size()) {
    FinishAnswer(std::move(answer));
    return;
  }
  // Re-read the target now, not when the sheet was drawn. A page can change
  // while a person types, and the classification that decides whether the
  // browser may hold these bytes is the one the node carries at this moment —
  // never the one a proposal asserted and never one cached a minute ago
  // (decision 0088, "trust the classification the proposal asserts" is the
  // alternative that was rejected).
  const RequestedField& field = answer->request.fields[answer->index];
  const std::string tab_id = answer->request.tab_id;
  const std::string node_id = field.node_id;
  resolve_node_.Run(
      tab_id, node_id,
      base::BindOnce(&FieldValueRequestCoordinator::OnTargetResolved,
                     weak_factory_.GetWeakPtr(), std::move(answer)));
}

void FieldValueRequestCoordinator::OnTargetResolved(
    std::unique_ptr<PendingAnswer> answer,
    std::optional<ResolvedNodeFacts> facts) {
  const uint32_t index = answer->index;
  const std::string field_id = answer->request.fields[index].field_id;
  std::string& value = answer->values[index];
  ++answer->index;

  if (!facts.has_value() ||
      facts->node_id.value != answer->request.fields[index].node_id ||
      !FieldNeedsAPerson(*facts)) {
    Discard(value);
    answer->refused.push_back(
        Refused(field_id, surface_mojom::FieldValueRefusalReason::kFieldGone));
    answer->prefix_closed = true;
    SettleNextField(std::move(answer));
    return;
  }

  const std::optional<FillClearance> clearance =
      FillClearance::For(facts->sensitivity);
  if (!clearance.has_value()) {
    // Named, never dropped. This is the field a person has to type into the
    // page themselves, and they cannot do that if nothing says which one it
    // was. It is not a setting and no approval reaches it: passwords,
    // passcodes, card security codes, personal identification numbers,
    // passkey assertions, recovery codes, tokens, keys and seed phrases all
    // classify as kCredential, and a field this build could not classify at
    // all is stricter still. One-time codes and challenge responses take the
    // separate, person-supplied-per-use path documented by the coordinator.
    Discard(value);
    answer->refused.push_back(
        Refused(field_id,
                surface_mojom::FieldValueRefusalReason::kFieldMayNotBeFilled));
    answer->prefix_closed = true;
    SettleNextField(std::move(answer));
    return;
  }

  // The name both sides derive. Checked for validity rather than assumed: a
  // `ValueReference` is bounded and a request identity is bounded by a larger
  // number, so a long enough request could name a value the vault would not
  // accept a name for.
  const ValueReference reference{
      DerivedValueReference(answer->request_id, index)};
  // Moved, not copied, and this is the statement the whole file is arranged
  // around. After it the local is empty or overwritten, the bytes are in the
  // vault or gone, and no copy continues into the command that reports the
  // count.
  const ValueReference minted =
      vault_->MintNamed(reference, TaskId{answer->request.task_id}, *clearance,
                        std::move(value), answer->expires_at);
  Discard(value);
  if (minted.is_valid()) {
    answer->minted_references.push_back(minted);
    ++answer->minted;
  } else {
    // The vault refused: no profile generation is active, the task identity
    // is not one it recognises, the answer was empty, or this position is
    // already held. Named rather than counted, for the reason above.
    answer->refused.push_back(
        Refused(field_id, surface_mojom::FieldValueRefusalReason::kNotHeld));
    answer->prefix_closed = true;
  }
  SettleNextField(std::move(answer));
}

void FieldValueRequestCoordinator::FinishAnswer(
    std::unique_ptr<PendingAnswer> answer) {
  for (std::string& value : answer->values) {
    Discard(value);
  }
  // The count goes only after every mint has already happened. It is a report
  // of what the vault holds, never a promise about what it is about to hold:
  // the core composes a fill proposal naming the person's n-th answer, and a
  // count that ran ahead of the mints would name values that are not there.
  const bool complete =
      answer->minted == answer->request.fields.size() &&
      answer->values.size() == answer->request.fields.size() &&
      answer->refused.empty() && !answer->values.empty();
  if (complete) {
    RecordPreapproval(*answer);
  } else {
    // The person's approval covered the complete ordered sequence. If one row
    // cannot be held, the answer reports zero and every earlier mint from the
    // same attempt is scrubbed now rather than retained unusably until expiry.
    for (const ValueReference& reference : answer->minted_references) {
      vault_->RevokeReference(reference);
    }
  }
  // The person answered either way, and that is what the outcome says. An
  // incomplete answer is zero values with kAnswered rather than a refusal
  // clause: the sheet was drawn, the person filled it in, and the browser
  // could not hold one of the rows — none of which is something the task can
  // act on differently by asking again (decision 0215).
  // Which field each held value is for, in position order, so the task puts
  // them into the page itself rather than waiting on a model turn to name
  // them (decision 0238). Node identities from the observation, never a value;
  // empty with the zero count, because an incomplete answer holds nothing.
  std::vector<std::string> field_node_ids;
  if (complete) {
    field_node_ids.reserve(answer->minted);
    for (uint32_t index = 0; index < answer->minted; ++index) {
      field_node_ids.push_back(answer->request.fields[index].node_id);
    }
  }
  // Counts and a flag, never a field's label or anything typed.
  LOG(WARNING) << "[taffy_field_request_answered] rows="
               << answer->request.fields.size()
               << " minted=" << answer->minted
               << " refused=" << answer->refused.size()
               << " complete=" << (complete ? 1 : 0);
  report_supplied_.Run(answer->request.task_id, answer->request_id,
                       complete ? answer->minted : 0u,
                       core_service::mojom::FieldValueAskOutcome::kAnswered,
                       field_node_ids);
  std::move(answer->callback)
      .Run(surface_mojom::FieldValueSupplyVerdict::kAccepted,
           std::move(answer->refused));
  if (client_.is_bound()) {
    client_->Close(answer->request_id,
                   surface_mojom::FieldValueCloseReason::kAnswered);
  }
  NotifyRequestClosed(answer->request_id);
}

void FieldValueRequestCoordinator::RecordPreapproval(
    const PendingAnswer& answer) {
  FormFillPreapproval approval;
  approval.task_id = answer.request.task_id;
  approval.tab_id = answer.request.tab_id;
  approval.normalized_origin = answer.request.document.normalized_origin;
  approval.frame_id = answer.request.document.frame_id;
  approval.page_epoch = answer.request.document.page_epoch;
  approval.graph_revision = answer.request.document.graph_revision;
  approval.fields = answer.request.fields;
  approval.expires_at_monotonic_ms =
      FieldApprovalMonotonicMillis(answer.expires_at);
  approval.expires_at_utc_ms = FieldApprovalUtcMillis(answer.expires_at_utc);
  if (approval.expires_at_monotonic_ms == 0u ||
      approval.expires_at_utc_ms == 0u) {
    return;
  }
  preapprovals_.try_emplace(answer.request_id, std::move(approval));
}

}  // namespace taffy
