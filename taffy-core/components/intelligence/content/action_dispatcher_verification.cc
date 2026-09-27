// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Steps 9 and 10: observe the postconditions, then record exactly one
// terminal result and consume the capability. A result is written once, from
// one place, so an action cannot end twice or end without being recorded.

#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"

namespace taffy {

namespace {

// The origin category a finished result records. The tab's own origin is not
// carried into the record, so the category here is the conservative one; the
// service implementation refines it at the call site that holds both origins.
OriginCategory CategorizeAgainstTab(const Origin& origin) {
  if (origin.kind == OriginKind::kOpaque) {
    return OriginCategory::kOpaque;
  }
  return OriginCategory::kUnknown;
}

}  // namespace

// --- step 9 -----------------------------------------------------------------

void ActionDispatcher::StartVerification(PendingAction& pending) {
  const RequestId request_id = pending.request_id;

  uint32_t deadline_ms = 0;
  for (const Postcondition& postcondition : pending.postconditions()) {
    deadline_ms = std::max(deadline_ms, postcondition.timeout_ms);
  }
  // The postcondition ceiling, not the snapshot one (decision 0169). Nothing
  // in the shipping path writes `Postcondition::timeout_ms`, so this is
  // always the zero branch, and the zero branch used to answer the ceiling on
  // how long a renderer may spend building an observation.
  deadline_ms = ClampPostconditionDeadlineMs(deadline_ms);

  NodeHandle target;
  if (pending.envelope) {
    target = pending.envelope->target_handle;
  } else {
    target.tab_id = pending.command->tab_id;
  }

  pending.verifier = std::make_unique<PostconditionVerifier>(
      web_contents_, broker_,
      base::BindRepeating(&ActionDispatcher::ResolveNodeFacts,
                          weak_factory_.GetWeakPtr()),
      browser_effects_);
  pending.verifier->Start(
      target, pending.postconditions(), pending.dispatch_revision,
      pending.dispatch_watermark, pending.idempotency(),
      base::Milliseconds(deadline_ms),
      base::BindOnce(&ActionDispatcher::OnVerified, weak_factory_.GetWeakPtr(),
                     request_id));
}

void ActionDispatcher::OnVerified(const RequestId& request_id,
                                  PostconditionVerifier::Outcome outcome) {
  const std::optional<VerifierKind> primary =
      outcome.verified_by.empty()
          ? std::nullopt
          : std::optional<VerifierKind>(outcome.verified_by.front());
  FinishAction(request_id, outcome.code, outcome.primary_postcondition, primary,
               std::nullopt, outcome.telemetry, std::move(outcome.outcomes),
               std::move(outcome.verified_by));
}

void ActionDispatcher::OnDeltaObserved(const FrameId& frame_id,
                                       GraphRevision revision) {
  for (auto& [request_id, pending] : pending_) {
    if (pending.verifier) {
      pending.verifier->OnDeltaObserved(frame_id, revision);
    }
  }
}

// --- step 10 ----------------------------------------------------------------

void ActionDispatcher::FinishAction(
    const RequestId& request_id,
    ActionResultCode code,
    std::optional<PostconditionKind> verified,
    std::optional<VerifierKind> verifier,
    std::optional<PreconditionKind> failed_precondition,
    VerifierOutcome verifier_outcome,
    std::vector<PostconditionOutcome> postcondition_outcomes,
    std::vector<VerifierKind> verified_by) {
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;  // Already terminal. Exactly one result per request id.
  }
  PendingAction pending = std::move(it->second);
  pending_.erase(it);

  // The attribution window belongs to the action attempt, not to the tab for
  // the rest of its lifetime. Close only this attempt's watermark; if another
  // dispatch has superseded it, the source deliberately leaves that newer
  // window open.
  if (browser_effects_ && pending.dispatch_watermark.value != 0u) {
    browser_effects_->CloseDispatch(pending.dispatch_watermark);
  }

  // A verifier can settle inside its own Start() — a read-only command with a
  // "no mutation" postcondition decides immediately — which reaches here on the
  // verifier's own stack. Destroying it now would be destroying the object this
  // call is running inside, so it goes away on the next task instead.
  if (pending.verifier) {
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(pending.verifier));
  }

  const CapabilityGrant& capability = pending.capability();

  // Step 10: record the terminal result and settle the capability. Settling
  // happens whatever the outcome — a capability that was admitted is spent.
  if (pending.capability_admitted) {
    capabilities_->Settle(capability.capability_reference, code);
  }

  // The lease has the same lifetime as the capability it authorized: one
  // action. Nothing released it, so a granted mutating action left a
  // mutating lease on its tab until the lease expired, and the registry
  // allows exactly one of those per tab. The next mutating move — the
  // navigate a task proposes after a search — was refused `kAlreadyHeld` and
  // reached the model as ActorLeaseMissing, which reads as "you may not do
  // this" rather than "wait". Releasing here can only ever narrow authority.
  leases_->Release(capability.actor_lease_id);

  // Every refused dispatch, by its own name. The seam above this one carries
  // a dispatched action's outcome as one coarse status, so a node that moved,
  // a control that is not visible and a policy denial all reach the model as
  // "denied" — which its recovery table reads as "never retry". Which code it
  // actually was is the fact that says whether the next move is a fresh look
  // or a hand-back, and nothing was recording it.
  if (code != ActionResultCode::kVerified) {
    LOG(WARNING) << "[taffy_action_refused] code=" << static_cast<int>(code)
                 << " failed_precondition="
                 << (failed_precondition
                         ? static_cast<int>(*failed_precondition)
                         : -1)
                 << " admitted=" << (pending.capability_admitted ? 1 : 0);
  }

  ActionResult result;
  result.schema_version = kBipSchemaVersion;
  result.request_id = request_id;
  result.result_code = code;
  result.dispatched =
      pending.renderer_acknowledged || !pending.dispatched_at.is_null();
  result.terminal = true;
  result.completed_at_monotonic_ms = NowMonotonicMs();
  result.failed_precondition = failed_precondition;

  // The renderer's acknowledgement is recorded because it happened, not
  // because it counts. It can appear beside a verifier only when a verifier
  // also corroborated the effect; on its own it means DISPATCHED, and the code
  // above already reflects that (protocol section 11.6).
  if (pending.renderer_acknowledged) {
    result.verified_by.push_back(VerifierKind::kRendererAcknowledgement);
  }
  for (VerifierKind kind : verified_by) {
    CHECK_NE(kind, VerifierKind::kRendererAcknowledgement);
    result.verified_by.push_back(kind);
  }
  if (verified_by.empty() && verifier.has_value()) {
    result.verified_by.push_back(*verifier);
  }

  if (pending.envelope) {
    const AuthorizedActionEnvelope& e = *pending.envelope;
    result.action_id = e.action_id;
    result.dispatch_id = e.dispatch_id;
    result.observed_page_epoch = e.target_handle.page_epoch;
  } else {
    const AuthorizedBrowserCommand& c = *pending.command;
    result.action_id = c.action_id;
    result.dispatch_id = c.dispatch_id;
    if (c.source_handle) {
      result.observed_page_epoch = c.source_handle->page_epoch;
    }
  }
  if (pending.dispatch_revision != 0) {
    result.observed_graph_revision = pending.dispatch_revision;
  }

  // One outcome per declared postcondition, straight from the verifier. A
  // refusal that never reached a verifier carries none: claiming an outcome
  // for a check that never ran would be inventing evidence.
  result.postcondition_outcomes = std::move(postcondition_outcomes);

  // A repeat can duplicate an effect only when something may already have
  // happened. Nothing was dispatched, so nothing can be duplicated.
  result.repeat_may_duplicate_effect =
      result.dispatched && (RepeatMayDuplicateEffect(pending.idempotency()) ||
                            IsAmbiguousOutcome(code));

  if (journal_ && pending.journalled) {
    journal_->RecordTerminalResult(result,
                                   MakeJournalCallback(base::BindOnce([](bool) {
                                     // The action's observed result is still
                                     // returned to its caller. A false append
                                     // leaves the durable intent open and
                                     // therefore ambiguous; it must not be
                                     // replaced with a made-up terminal fact.
                                   })));
  }

  ActionRecord record;
  record.request_id = ToRecordIdentifier(request_id.value);
  record.dispatch_id = ToRecordIdentifier(result.dispatch_id.value);
  record.action_id = ToRecordIdentifier(result.action_id.value);
  record.tab_id = ToRecordIdentifier(pending.tab_id().value);
  record.capability_reference =
      ToRecordIdentifier(capability.capability_reference.value);
  record.actor_lease_id = ToRecordIdentifier(capability.actor_lease_id.value);
  record.graph_revision = pending.dispatch_revision;
  record.code = code;
  record.stale_reason = ObservabilityRecorder::StaleReasonFor(code);
  record.verifier_outcome = verifier_outcome;
  record.policy_version = capability.policy_version;
  record.renderer_acknowledged = pending.renderer_acknowledged;
  record.repeat_may_duplicate_effect = result.repeat_may_duplicate_effect;
  if (failed_precondition.has_value()) {
    record.failed_precondition = *failed_precondition;
    record.had_failed_precondition = true;
  }
  if (verified.has_value()) {
    record.verified_postcondition = *verified;
    record.had_verified_postcondition = true;
  }
  if (pending.envelope) {
    const AuthorizedActionEnvelope& e = *pending.envelope;
    record.task_id = ToRecordIdentifier(e.task_id.value);
    record.frame_id = ToRecordIdentifier(e.target_handle.frame_id.value);
    record.page_epoch = ToRecordIdentifier(e.target_handle.page_epoch.value);
    record.action_type = e.action_type;
    record.is_node_action = true;
    record.origin_category =
        CategorizeAgainstTab(e.target_handle.expected_origin);
  } else {
    const AuthorizedBrowserCommand& command = *pending.command;
    record.task_id = ToRecordIdentifier(command.task_id.value);
    if (command.source_handle) {
      record.frame_id =
          ToRecordIdentifier(command.source_handle->frame_id.value);
      record.page_epoch =
          ToRecordIdentifier(command.source_handle->page_epoch.value);
      record.action_type = ActionType::kActivate;
      record.is_node_action = true;
      record.origin_category =
          CategorizeAgainstTab(command.source_handle->expected_origin);
    } else {
      record.is_node_action = false;
    }
  }
  record.dispatch_latency_us =
      pending.dispatched_at.is_null()
          ? 0
          : (pending.dispatched_at - pending.started_at).InMicroseconds();
  record.verification_latency_us =
      pending.dispatched_at.is_null()
          ? 0
          : (base::TimeTicks::Now() - pending.dispatched_at).InMicroseconds();
  observability_->Record(record);

  if (pending.on_complete) {
    std::move(pending.on_complete).Run(std::move(result));
  }
}

}  // namespace taffy
