// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/field_value_request_coordinator.h"

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/browser_context.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

// The instruction half of an abandoned ask (decision 0215). Aliased because
// the qualified name is longer than the line it has to share with a call.
using AskOutcome = core_service::mojom::FieldValueAskOutcome;

namespace {

namespace surface_mojom = browser::field_values::mojom;

// The production node-facts seam: one node of one tab of this profile,
// re-read now.
//
// It goes through the tab's own page intelligence host so that every
// browser-side answer about what a node is comes from the one `ResolveNode`
// call the dispatch path uses. A second route would be a second answer, and
// the two would differ exactly when a page changed under an errand.
void ResolveNodeFactsInProfile(
    content::BrowserContext* browser_context,
    const std::string& tab_id,
    const std::string& node_id,
    FieldValueRequestCoordinator::NodeFactsCallback on_resolved) {
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(browser_context, tab_id);
  if (!host) {
    std::move(on_resolved).Run(std::nullopt);
    return;
  }
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  if (!live || live->tab_id != tab_id) {
    std::move(on_resolved).Run(std::nullopt);
    return;
  }
  NodeHandle handle;
  handle.tab_id = TabId{live->tab_id};
  handle.frame_id = FrameId{live->frame_id};
  handle.page_epoch = PageEpoch{live->page_epoch};
  // The revision the browser last saw, rather than a floor an errand
  // remembered: this is a re-read of what is on the page now, and pinning it
  // to an older revision would refuse the very page it is looking at.
  handle.graph_revision = live->graph_revision;
  handle.node_id = SemanticNodeId{node_id};
  handle.expected_origin.kind = OriginKind::kTuple;
  handle.expected_origin.serialization = live->origin;
  if (!handle.is_well_formed()) {
    std::move(on_resolved).Run(std::nullopt);
    return;
  }
  host->ResolveNodeFacts(handle, std::move(on_resolved));
}

std::optional<FieldValueDocument> DocumentOfTabInProfile(
    content::BrowserContext* browser_context,
    const std::string& tab_id) {
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(browser_context, tab_id);
  if (!host) {
    return std::nullopt;
  }
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  if (!live) {
    return std::nullopt;
  }
  FieldValueDocument document{
      .host = live->host,
      .normalized_origin = live->origin,
      .frame_id = live->frame_id,
      .page_epoch = live->page_epoch,
      .graph_revision = live->graph_revision,
  };
  return document.is_well_formed()
             ? std::make_optional(std::move(document))
             : std::nullopt;
}

base::OnceClosure RefuseChallengePresentation(
    const std::string&,
    const std::string&,
    ResolvedNodeFacts,
    FieldChallengePresentationCallback completion) {
  // The default resolver: this build has no way to capture anything, so the
  // clause is about the resolver rather than about the page.
  std::move(completion).Run(std::nullopt, "no-capture-in-this-build");
  return base::DoNothing();
}

base::OnceClosure StartChallengePresentationInProfile(
    content::BrowserContext* browser_context,
    const std::string& tab_id,
    const std::string& node_id,
    ResolvedNodeFacts facts,
    FieldChallengePresentationCallback completion) {
  return StartFieldChallengePresentation(browser_context, tab_id, node_id,
                                         std::move(facts),
                                         std::move(completion));
}

}  // namespace

bool FieldValueDocument::is_well_formed() const {
  return !host.empty() && !normalized_origin.empty() && !frame_id.empty() &&
         !page_epoch.empty() && graph_revision != 0u;
}

// Whether a person, rather than the assistant, has to put this field's value
// in.
//
// Two questions, and only the second is this one's own. The first is whether
// the node is a thing a value can be written into at all — it accepts text,
// it is editable, and it is neither disabled nor read-only — because a row a
// person types into that cannot receive what they typed wastes their time.
//
// The second is whether Taffy may write it itself, and that is answered at
// the moment the answer arrives rather than here: `FillClearance::For` reads
// the classification the browser re-read from the node, and a class it
// answers nothing for is the person's field and no one else's.
bool FieldNeedsAPerson(const ResolvedNodeFacts& facts) {
  return facts.SupportsAction(ActionType::kSetText) &&
         facts.HasState(NodeState::kEditable) &&
         !facts.HasState(NodeState::kDisabled) &&
         !facts.HasState(NodeState::kReadOnly);
}

std::string DerivedValueReference(const std::string& request_id,
                                  uint32_t index) {
  // Must agree, character for character, with `value_reference_for_supplied`
  // in
  // taffy-core/components/intelligence/core/rust/task-engine/src/field_values.rs
  // — `format!("{}-value-{index}", request.as_str())`. Neither side sends the
  // other a name; both derive it, and the count that crosses between them is
  // only sufficient because they do.
  return base::StrCat({request_id, "-value-", base::NumberToString(index)});
}

// static
std::unique_ptr<FieldValueRequestCoordinator>
FieldValueRequestCoordinator::ForProfile(
    CoreServiceManager* manager,
    content::BrowserContext* browser_context) {
  CHECK(manager);
  CHECK(browser_context);
  // Unretained on all three: the manager owns this object and declares it
  // after every member whose address is handed over here, so reverse
  // destruction takes this down first.
  return std::make_unique<FieldValueRequestCoordinator>(
      manager->value_references(),
      base::BindRepeating(&ResolveNodeFactsInProfile,
                          base::Unretained(browser_context)),
      base::BindRepeating(&DocumentOfTabInProfile,
                          base::Unretained(browser_context)),
      base::BindRepeating(&CoreServiceManager::SubmitSuppliedFieldValues,
                          base::Unretained(manager)),
      base::BindRepeating(&StartChallengePresentationInProfile,
                          base::Unretained(browser_context)),
      base::BindRepeating(&CoreServiceManager::OnFieldValueRequestClosed,
                          base::Unretained(manager)));
}

FieldValueRequestCoordinator::FieldValueRequestCoordinator(
    ValueReferenceVault* vault,
    NodeFactsResolver resolve_node,
    TabDocumentResolver resolve_document,
    SuppliedCountSink report_supplied)
    : FieldValueRequestCoordinator(
          vault,
          std::move(resolve_node),
          std::move(resolve_document),
          std::move(report_supplied),
          base::BindRepeating(&RefuseChallengePresentation)) {}

FieldValueRequestCoordinator::FieldValueRequestCoordinator(
    ValueReferenceVault* vault,
    NodeFactsResolver resolve_node,
    TabDocumentResolver resolve_document,
    SuppliedCountSink report_supplied,
    ChallengePresentationResolver resolve_challenge_presentation,
    RequestClosedSink request_closed)
    : vault_(vault),
      resolve_node_(std::move(resolve_node)),
      resolve_document_(std::move(resolve_document)),
      report_supplied_(std::move(report_supplied)),
      resolve_challenge_presentation_(
          std::move(resolve_challenge_presentation)),
      request_closed_(std::move(request_closed)) {
  CHECK(vault_);
  CHECK(resolve_node_);
  CHECK(resolve_document_);
  CHECK(report_supplied_);
  CHECK(resolve_challenge_presentation_);
}

FieldValueRequestCoordinator::~FieldValueRequestCoordinator() = default;

void FieldValueRequestCoordinator::Bind(
    mojo::PendingReceiver<surface_mojom::TaffyFieldValueSurface> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void FieldValueRequestCoordinator::Connect(
    mojo::PendingRemote<surface_mojom::TaffyFieldValueClient> client) {
  // One surface at a time. A second connection replaces the first, and every
  // request the first was drawing is closed and answered with nothing: a
  // person cannot answer a sheet that is no longer on any screen, and a task
  // left waiting for one who could would never reach the handover that is the
  // floor under all of this (decision 0088 section 3).
  if (client_.is_bound()) {
    CloseEveryRequest(CloseReason::kRevoked, /*report_zero=*/true);
    client_.reset();
  }
  client_.Bind(std::move(client));
}

void FieldValueRequestCoordinator::OnCoreFieldValueRequest(
    const std::string& request_id,
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& node_id,
    const std::vector<std::string>& companion_node_ids) {
  if (request_id.empty() || task_id.empty() || tab_id.empty() ||
      node_id.empty() || open_.contains(request_id) ||
      preapprovals_.contains(request_id)) {
    return;
  }
  if (!client_.is_bound()) {
    // Nothing can draw the sheet, so nobody will answer it. The task is told
    // the person supplied nothing rather than left waiting on a surface that
    // does not exist.
    LOG(WARNING) << "[taffy_field_request_abandoned] at=no-surface";
    report_supplied_.Run(task_id, request_id, 0u, AskOutcome::kNoSurface,
                         /*field_node_ids=*/{});
    NotifyRequestClosed(request_id);
    return;
  }
  const std::optional<FieldValueDocument> document =
      resolve_document_.Run(tab_id);
  if (!document || !document->is_well_formed()) {
    LOG(WARNING) << "[taffy_field_request_abandoned] at=no-document";
    report_supplied_.Run(task_id, request_id, 0u, AskOutcome::kNoSurface,
                         /*field_node_ids=*/{});
    NotifyRequestClosed(request_id);
    return;
  }
  OpenRequest request;
  request.task_id = task_id;
  request.tab_id = tab_id;
  request.named_node_id = node_id;
  request.companion_node_ids = companion_node_ids;
  request.document = *document;
  open_.emplace(request_id, std::move(request));
  resolve_node_.Run(
      tab_id, node_id,
      base::BindOnce(&FieldValueRequestCoordinator::OnFormResolved,
                     weak_factory_.GetWeakPtr(), request_id, node_id));
}

void FieldValueRequestCoordinator::Dismiss(const std::string& request_id) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  const std::string task_id = it->second.task_id;
  // Zero is an answer. A person who opened the sheet and filled nothing in
  // has answered, and the task needs to learn that rather than wait on a
  // surface that has already closed.
  AbandonRequest(request_id, task_id, CloseReason::kDismissed, "dismissed",
                 AskOutcome::kDismissed);
}

void FieldValueRequestCoordinator::CloseRequest(const std::string& request_id,
                                                CloseReason reason) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  base::OnceClosure cancel =
      std::move(it->second.cancel_challenge_presentation);
  open_.erase(it);
  if (cancel) {
    std::move(cancel).Run();
  }
  if (client_.is_bound()) {
    client_->Close(request_id, reason);
  }
  NotifyRequestClosed(request_id);
}

void FieldValueRequestCoordinator::AbandonRequest(
    const std::string& request_id,
    const std::string& task_id,
    CloseReason reason,
    const char* at,
    AskOutcome outcome) {
  LOG(WARNING) << "[taffy_field_request_abandoned] at=" << at;
  report_supplied_.Run(task_id, request_id, 0u, outcome,
                       /*field_node_ids=*/{});
  CloseRequest(request_id, reason);
}

void FieldValueRequestCoordinator::CloseEveryRequest(CloseReason reason,
                                                     bool report_zero) {
  auto closing = std::move(open_);
  open_.clear();
  for (auto& [request_id, request] : closing) {
    if (request.cancel_challenge_presentation) {
      std::move(request.cancel_challenge_presentation).Run();
    }
    if (client_.is_bound()) {
      client_->Close(request_id, reason);
    }
    if (report_zero) {
      // The generation is closing every sheet at once, so no clause about any
      // one of them applies. Nothing could draw a sheet from here on, which is
      // what kNoSurface says.
      report_supplied_.Run(request.task_id, request_id, 0u,
                           AskOutcome::kNoSurface, /*field_node_ids=*/{});
    }
    NotifyRequestClosed(request_id);
  }
}

void FieldValueRequestCoordinator::NotifyRequestClosed(
    const std::string& request_id) {
  if (request_closed_) {
    request_closed_.Run(request_id);
  }
}

void FieldValueRequestCoordinator::CloseAllRequests() {
  // No count is reported, and the absence is the point: this runs when the
  // profile generation is going, so the tasks these sheets belonged to are
  // going with it and there is no core left to tell. Reporting would mean
  // submitting a command from inside teardown for an errand that no longer
  // exists.
  CloseEveryRequest(CloseReason::kRevoked, /*report_zero=*/false);
  preapprovals_.clear();
}

void FieldValueRequestCoordinator::CloseRequestsForTask(
    const std::string& task_id) {
  std::vector<std::string> closing;
  for (const auto& [request_id, request] : open_) {
    if (request.task_id == task_id) {
      closing.push_back(request_id);
    }
  }
  for (const std::string& request_id : closing) {
    // No count, for the same reason: the task whose sheet this was is being
    // revoked or released, and a command answering for it would be answering
    // for an errand that has ended.
    CloseRequest(request_id, CloseReason::kRevoked);
  }
  for (auto it = preapprovals_.begin(); it != preapprovals_.end();) {
    if (it->second.task_id == task_id) {
      it = preapprovals_.erase(it);
    } else {
      ++it;
    }
  }
}

bool FieldValueRequestCoordinator::HoldsFormFillPreapproval(
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& node_id,
    const std::string& request_id,
    uint32_t index,
    const std::string& normalized_origin,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms) const {
  const auto it = preapprovals_.find(request_id);
  return it != preapprovals_.end() &&
         IsExactPreapprovalUse(it->second, task_id, tab_id, node_id, index,
                               normalized_origin, frame_id, page_epoch,
                               graph_revision, now_monotonic_ms, now_utc_ms);
}

// static
bool FieldValueRequestCoordinator::IsExactPreapprovalUse(
    const FormFillPreapproval& approval,
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& node_id,
    uint32_t index,
    const std::string& normalized_origin,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms) {
  const bool expired = now_monotonic_ms >= approval.expires_at_monotonic_ms ||
                       now_utc_ms >= approval.expires_at_utc_ms;
  const bool index_in_range = index < approval.fields.size();
  return !expired && approval.task_id == task_id && approval.tab_id == tab_id &&
         approval.normalized_origin == normalized_origin &&
         approval.frame_id == frame_id && approval.page_epoch == page_epoch &&
         // The reading the person answered on, or any later reading of the
         // same document. The task's own first fill changes the page — a
         // script marks the field it typed into — so an exact revision
         // refused every second field of every such form (decision 0194).
         // Which document, which field and in what order stay exact.
         approval.graph_revision <= graph_revision &&
         index == approval.next_index && index_in_range &&
         approval.fields[index].node_id == node_id;
}

std::optional<FormFillPreapprovalReceipt>
FieldValueRequestCoordinator::ConsumeFormFillPreapproval(
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& node_id,
    const std::string& request_id,
    uint32_t index,
    const std::string& normalized_origin,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms) {
  const auto it = preapprovals_.find(request_id);
  if (it == preapprovals_.end()) {
    return std::nullopt;
  }
  FormFillPreapproval& approval = it->second;
  if (!IsExactPreapprovalUse(approval, task_id, tab_id, node_id, index,
                             normalized_origin, frame_id, page_epoch,
                             graph_revision, now_monotonic_ms, now_utc_ms)) {
    // A mismatch invalidates the whole remaining sequence. Keeping it after a
    // reordered or substituted proposal would turn a refusal into an oracle
    // the next proposal could probe, and would let a later action silently
    // resume an approval whose exact order had already been violated.
    preapprovals_.erase(it);
    return std::nullopt;
  }
  FormFillPreapprovalReceipt receipt{
      .expires_at_monotonic_ms = approval.expires_at_monotonic_ms,
      .expires_at_utc_ms = approval.expires_at_utc_ms,
  };
  ++approval.next_index;
  if (approval.next_index == approval.fields.size()) {
    preapprovals_.erase(it);
  }
  return receipt;
}

}  // namespace taffy
