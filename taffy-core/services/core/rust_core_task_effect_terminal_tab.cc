// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <set>
#include <string>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bool ValidIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

bool SameOperation(const mojom::OperationEnvelope& left,
                   const mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

bool ValidTaskTabTarget(const mojom::TaskTabDocumentTarget& target) {
  return ValidIdentifier(target.tab_id) && ValidIdentifier(target.frame_id) &&
         ValidIdentifier(target.page_epoch) && target.graph_revision != 0u;
}

bool SameTaskTabTarget(const mojom::TaskTabDocumentTarget& left,
                       const mojom::TaskTabDocumentTarget& right) {
  return left.tab_id == right.tab_id && left.frame_id == right.frame_id &&
         left.page_epoch == right.page_epoch &&
         left.graph_revision == right.graph_revision;
}

}  // namespace

bool CopyTaskTabCompletion(const mojom::TaskEffectBinding& effect,
                           const mojom::TaskEffectCompletion& completion,
                           bridge::BridgeTaskTerminal& out) {
  const auto& executable = effect.action->executable;
  const auto operation = executable->operation_kind;
  const bool lists = operation == mojom::TaskActionOperationKind::kTabsList;
  const bool activates =
      operation == mojom::TaskActionOperationKind::kTabsActivate;
  const bool closes = operation == mojom::TaskActionOperationKind::kTabsClose;
  if ((!lists && !activates && !closes) || !executable->task_tab ||
      !completion.effect_result || !completion.effect_result->operation ||
      !SameOperation(*effect.operation, *completion.effect_result->operation) ||
      completion.effect_result->effect_id != effect.effect_id ||
      completion.effect_result->kind != mojom::EffectKind::kBrowserAction ||
      completion.effect_result->status != mojom::EffectStatus::kCompleted ||
      !completion.effect_result->browser_action ||
      completion.effect_result->browser_action->outcome !=
          mojom::BrowserActionOutcome::kCompleted ||
      !completion.effect_result->browser_action->dispatch_id ||
      *completion.effect_result->browser_action->dispatch_id !=
          effect.action->dispatch_id ||
      completion.effect_result->browser_action->discovered_source ||
      completion.effect_result->browser_action->discovery_tab_id ||
      completion.effect_result->browser_action->browser_session_id ||
      completion.effect_result->browser_action->task_download ||
      completion.effect_result->browser_action->task_store ||
      !completion.effect_result->browser_action->task_tab) {
    return false;
  }
  const auto& binding = executable->task_tab;
  const auto& result = completion.effect_result->browser_action->task_tab;
  const auto expected_postcondition =
      lists       ? mojom::TaskTabPostcondition::kListed
      : activates ? mojom::TaskTabPostcondition::kActive
                  : mojom::TaskTabPostcondition::kAbsent;
  if (!ValidIdentifier(binding->browser_session_id) ||
      result->browser_session_id != binding->browser_session_id ||
      result->operation_kind != operation ||
      result->postcondition != expected_postcondition ||
      (lists != binding->target.is_null()) ||
      (lists != result->target.is_null())) {
    return false;
  }

  out.has_task_tab_result = true;
  out.task_tab_browser_session_id = result->browser_session_id;
  out.task_tab_operation = static_cast<uint8_t>(result->operation_kind);
  out.task_tab_postcondition = static_cast<uint8_t>(result->postcondition);
  out.task_tab_state_was_already_satisfied =
      result->state_was_already_satisfied;

  if (lists) {
    if (result->state_was_already_satisfied ||
        result->tabs.size() > mojom::kMaxTaskTabResults) {
      return false;
    }
    std::set<std::string> tab_ids;
    size_t active_count = 0u;
    for (const mojom::TaskTabSnapshotPtr& snapshot : result->tabs) {
      if (!snapshot || !snapshot->target ||
          !ValidTaskTabTarget(*snapshot->target) ||
          !tab_ids.insert(snapshot->target->tab_id).second) {
        return false;
      }
      active_count += snapshot->active ? 1u : 0u;
      bridge::BridgeTaskTabSnapshot projected;
      projected.tab_id = snapshot->target->tab_id;
      projected.frame_id = snapshot->target->frame_id;
      projected.page_epoch = snapshot->target->page_epoch;
      projected.graph_revision = snapshot->target->graph_revision;
      projected.active = snapshot->active;
      out.task_tab_snapshots.push_back(std::move(projected));
    }
    return active_count <= 1u;
  }

  if (!result->tabs.empty() || !binding->target || !result->target ||
      !ValidTaskTabTarget(*binding->target) ||
      !ValidTaskTabTarget(*result->target) ||
      !SameTaskTabTarget(*binding->target, *result->target)) {
    return false;
  }
  out.has_task_tab_target = true;
  out.task_tab_target_tab_id = result->target->tab_id;
  out.task_tab_target_frame_id = result->target->frame_id;
  out.task_tab_target_page_epoch = result->target->page_epoch;
  out.task_tab_target_graph_revision = result->target->graph_revision;
  return true;
}

}  // namespace taffy::core_service_internal
