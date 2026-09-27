// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>

#include "base/logging.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

// The form arm of `EvaluateTaskPolicy`: a fill, a choice, a toggle or a
// submit, which is admitted only against an approval the browser holds for
// that exact action (decisions 0215 and 0239).
std::optional<CoreServiceManager::TaskPolicyDenial>
CoreServiceManager::DenyTaskFormPolicy(
    const service_mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentContext* live,
    uint64_t now,
    uint64_t now_utc) {
  if (!live) {
    return TaskPolicyDenial{
        service_mojom::TaskActionResultCode::kEgressNotAuthorized,
        "document-has-no-site"};
  }
  // A fill's first ask carries no approval, so there is nothing yet to match
  // against the person's answer: the decider says it needs one, the approval
  // that follows consumes the sheet's preapproval, and the ask after that
  // comes back here with a receipt. Refusing this ask as malformed, as this
  // path did, meant no fill ever reached the page — not the model's, and not
  // the task's own after the person had answered (decision 0239).
  //
  // Asking is only worth it when there is an answer to approve it with. A
  // fill of a value the person never gave would publish a question the
  // browser then refuses, and a refused approval leaves the action waiting
  // on nobody. So it is refused here, under the code whose sentence says
  // to ask the person, which is also what decision 0215 intended for it.
  if (TaskPolicyAskIsAnsweredByAsking(effect)) {
    const service_mojom::TaskSuppliedValuePosition* supplied =
        effect.input ? effect.input->supplied_value.get() : nullptr;
    if (!field_value_requests_ || !supplied || !effect.node_id ||
        !field_value_requests_->HoldsFormFillPreapproval(
            effect.task_id, effect.tab_id, *effect.node_id,
            supplied->request_id, supplied->index, live->origin, live->frame_id,
            live->page_epoch, live->graph_revision, now, now_utc)) {
      LOG(WARNING) << "[taffy_task_policy_fill_asks] at=no-held-value";
      return TaskPolicyDenial{
          service_mojom::TaskActionResultCode::kValueReferenceUnknown,
          "no-held-value"};
    }
    LOG(WARNING) << "[taffy_task_policy_fill_asks] at=first-ask";
  }
  const FormApprovalKey approval_key{effect.task_id, effect.action_id};
  const auto found = preapproved_form_actions_.find(approval_key);
  if (!TaskPolicyAskIsAnsweredByAsking(effect)) {
    if (found == preapproved_form_actions_.end()) {
      return TaskPolicyDenial{
          service_mojom::TaskActionResultCode::kApprovalRequired, "approval"};
    }
    // Finding the record spends it even when the tuple is wrong. A caller
    // cannot probe substitutions and then retry the original approved
    // action.
    BrowserFormActionPreapproval approved = std::move(found->second);
    preapproved_form_actions_.erase(found);
    const TaskPolicyDocumentBinding approved_document{
        .tab_id = live->tab_id,
        .frame_id = live->frame_id,
        .page_epoch = live->page_epoch,
        .origin = live->origin,
        .graph_revision = live->graph_revision,
    };
    if (!TaskPolicyEffectMatchesFormPreapproval(effect, approved_document,
                                                approved, browser_session_id_,
                                                now, now_utc)) {
      return TaskPolicyDenial{
          service_mojom::TaskActionResultCode::kPreparedEffectChanged,
          "prepared-changed"};
    }
  } else if (found != preapproved_form_actions_.end()) {
    // A browser-owned approval already exists for this action, so an ask
    // without its receipt is not a first ask. Spend the record rather than
    // leave it for a later probe.
    preapproved_form_actions_.erase(found);
    return TaskPolicyDenial{
        service_mojom::TaskActionResultCode::kPreparedEffectChanged,
        "prepared-changed"};
  }
  return std::nullopt;
}

}  // namespace taffy
