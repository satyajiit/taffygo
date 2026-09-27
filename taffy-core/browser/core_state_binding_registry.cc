// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_state_binding_registry.h"

#include <algorithm>
#include <set>
#include <utility>

#include "taffy/browser/core_api/task_consent_shape.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

std::optional<uint8_t> TaskControlRank(mojom::TaskControlKind control) {
  switch (control) {
    case mojom::TaskControlKind::kPause:
      return 0u;
    case mojom::TaskControlKind::kResume:
      return 1u;
    case mojom::TaskControlKind::kTakeOver:
      return 2u;
    case mojom::TaskControlKind::kStop:
      return 3u;
  }
  return std::nullopt;
}

bool IsCanonicalTaskControlList(
    const std::vector<mojom::TaskControlKind>& controls) {
  if (controls.size() > mojom::kMaxTaskControls) {
    return false;
  }
  std::optional<uint8_t> previous;
  for (const mojom::TaskControlKind control : controls) {
    const std::optional<uint8_t> rank = TaskControlRank(control);
    if (!rank || (previous && *rank <= *previous)) {
      return false;
    }
    previous = rank;
  }
  return true;
}

bool IsSha256Digest(const std::string& value) {
  if (value.size() != 64u) {
    return false;
  }
  for (const char character : value) {
    if (!(character >= '0' && character <= '9') &&
        !(character >= 'a' && character <= 'f')) {
      return false;
    }
  }
  return true;
}

bool IsNormalizedTupleOrigin(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxNormalizedOriginBytes) {
    return false;
  }
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !origin.opaque() && origin.Serialize() == value;
}

std::optional<core_api::mojom::PlatformPermission> ToCoreApiPermission(
    mojom::PlatformPermission permission) {
  switch (permission) {
    case mojom::PlatformPermission::kNotifications:
      return core_api::mojom::PlatformPermission::kNotifications;
    case mojom::PlatformPermission::kMicrophone:
      return core_api::mojom::PlatformPermission::kMicrophone;
    case mojom::PlatformPermission::kCamera:
      return core_api::mojom::PlatformPermission::kCamera;
    case mojom::PlatformPermission::kLocation:
      return core_api::mojom::PlatformPermission::kLocation;
  }
  return std::nullopt;
}

}  // namespace

CoreStateBindingRegistry::CoreStateBindingRegistry() = default;
CoreStateBindingRegistry::~CoreStateBindingRegistry() = default;

mojom::PendingApprovalRegistrationStatus CoreStateBindingRegistry::Replace(
    mojom::CoreStateBrowserBindingsPtr bindings) {
  return bindings ? Replace(*bindings)
                  : mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
}

mojom::PendingApprovalRegistrationStatus CoreStateBindingRegistry::Replace(
    const mojom::CoreStateBrowserBindings& bindings) {
  if (bindings.service_generation == 0u || bindings.state_sequence == 0u) {
    return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
  }
  if (bindings.task_revisions.size() > mojom::kMaxTaskRevisionsPerProfile ||
      bindings.pending_approvals.size() >
          mojom::kMaxPendingApprovalsPerProfile ||
      bindings.pending_permissions.size() >
          mojom::kMaxPendingPermissionsPerProfile ||
      bindings.task_settlements.size() > mojom::kMaxTaskRevisionsPerProfile ||
      bindings.terminal_tasks.size() > mojom::kMaxTaskRevisionsPerProfile ||
      bindings.accepted_task_consents.size() >
          mojom::kMaxTaskRevisionsPerProfile ||
      bindings.committed_action_approvals.size() >
          mojom::kMaxPendingApprovalsPerProfile) {
    return mojom::PendingApprovalRegistrationStatus::kTooManyBindings;
  }

  base::flat_map<std::string, uint64_t> task_revisions;
  base::flat_map<std::string, std::vector<mojom::TaskControlKind>>
      task_controls;
  for (const auto& task : bindings.task_revisions) {
    if (!task || task->service_generation != bindings.service_generation ||
        task->task_revision == 0u || !IsIdentifier(task->task_id) ||
        !IsCanonicalTaskControlList(task->allowed_controls) ||
        !task_revisions.emplace(task->task_id, task->task_revision).second ||
        !task_controls.emplace(task->task_id, task->allowed_controls).second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
  }

  std::vector<TerminalTaskLookup> terminal_tasks;
  std::set<std::string> terminal_task_ids;
  for (const auto& terminal : bindings.terminal_tasks) {
    const auto task = terminal ? task_revisions.find(terminal->task_id)
                               : task_revisions.end();
    if (!terminal ||
        terminal->service_generation != bindings.service_generation ||
        terminal->task_revision == 0u || !IsIdentifier(terminal->task_id) ||
        task == task_revisions.end() ||
        task->second != terminal->task_revision ||
        !task_controls.at(terminal->task_id).empty() ||
        !terminal_task_ids.insert(terminal->task_id).second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
    terminal_tasks.push_back(
        TerminalTaskLookup{terminal->task_id, terminal->service_generation,
                           terminal->task_revision, terminal->kind});
  }

  std::vector<TaskSettlementLookup> task_settlements;
  std::set<std::string> settlement_task_ids;
  for (const auto& settlement : bindings.task_settlements) {
    const auto task = settlement ? task_revisions.find(settlement->task_id)
                                 : task_revisions.end();
    if (!settlement ||
        settlement->service_generation != bindings.service_generation ||
        settlement->task_revision == 0u || !IsIdentifier(settlement->task_id) ||
        task == task_revisions.end() ||
        task->second != settlement->task_revision ||
        !task_controls.at(settlement->task_id).empty() ||
        terminal_task_ids.contains(settlement->task_id) ||
        !settlement_task_ids.insert(settlement->task_id).second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
    task_settlements.push_back(TaskSettlementLookup{
        settlement->task_id, settlement->service_generation,
        settlement->task_revision, settlement->kind});
  }

  base::flat_map<std::string, PendingPermissionLookup> pending_permissions;
  std::set<std::string> permission_task_ids;
  for (const auto& permission : bindings.pending_permissions) {
    const auto api_permission =
        permission ? ToCoreApiPermission(permission->permission) : std::nullopt;
    const auto task = permission ? task_revisions.find(permission->task_id)
                                 : task_revisions.end();
    if (!permission || !api_permission ||
        permission->service_generation != bindings.service_generation ||
        permission->task_revision == 0u ||
        permission->deadline_monotonic_ms == 0u ||
        permission->deadline_utc_ms == 0u ||
        !IsIdentifier(permission->browser_session_id) ||
        !IsIdentifier(permission->task_id) ||
        !IsIdentifier(permission->request_id) || task == task_revisions.end() ||
        task->second != permission->task_revision ||
        terminal_task_ids.contains(permission->task_id) ||
        !permission_task_ids.insert(permission->task_id).second ||
        !pending_permissions
             .emplace(permission->request_id,
                      PendingPermissionLookup{
                          permission->task_id, permission->permission,
                          *api_permission, permission->service_generation,
                          permission->task_revision,
                          permission->deadline_monotonic_ms,
                          permission->deadline_utc_ms,
                          permission->browser_session_id})
             .second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
  }

  base::flat_map<ApprovalKey, PendingApprovalLookup> pending_approvals;
  std::set<std::string> approval_task_ids;
  for (const auto& approval : bindings.pending_approvals) {
    const auto task = approval ? task_revisions.find(approval->task_id)
                               : task_revisions.end();
    if (!approval ||
        approval->service_generation != bindings.service_generation ||
        approval->task_revision == 0u || !IsIdentifier(approval->task_id) ||
        !IsIdentifier(approval->action_id) ||
        !IsSha256Digest(approval->proposal_digest) ||
        task == task_revisions.end() ||
        task->second != approval->task_revision ||
        terminal_task_ids.contains(approval->task_id) ||
        !approval_task_ids.insert(approval->task_id).second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
    ApprovalKey key(approval->task_id, approval->action_id);
    if (!pending_approvals
             .emplace(std::move(key),
                      PendingApprovalLookup{bindings.service_generation,
                                            approval->task_revision,
                                            approval->proposal_digest})
             .second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
  }

  std::set<std::string> accepted_consent_tasks;
  for (const auto& consent : bindings.accepted_task_consents) {
    const auto task =
        consent ? task_revisions.find(consent->task_id) : task_revisions.end();
    if (!consent || !consent->consent_preview ||
        consent->service_generation != bindings.service_generation ||
        !IsIdentifier(consent->task_id) ||
        !IsIdentifier(consent->browser_session_id) ||
        !IsIdentifier(consent->receipt_id) ||
        consent->current_task_revision == 0u ||
        consent->accepted_revision == 0u ||
        consent->accepted_revision > consent->current_task_revision ||
        task == task_revisions.end() ||
        task->second != consent->current_task_revision ||
        terminal_task_ids.contains(consent->task_id) ||
        settlement_task_ids.contains(consent->task_id) ||
        !accepted_consent_tasks.insert(consent->task_id).second ||
        !IsAdmittedDurableTaskConsentShape(*consent->consent_preview)) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
    std::set<std::string> source_ids;
    std::set<std::string> tab_ids;
    for (const auto& source : consent->consent_preview->sources) {
      if (!source || !IsIdentifier(source->source_id) ||
          !IsIdentifier(source->tab_id) ||
          !IsNormalizedTupleOrigin(source->normalized_origin) ||
          !source_ids.insert(source->source_id).second ||
          !tab_ids.insert(source->tab_id).second) {
        return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
      }
    }
  }

  std::set<ApprovalKey> committed_approval_keys;
  for (const auto& approval : bindings.committed_action_approvals) {
    const auto task = approval ? task_revisions.find(approval->task_id)
                               : task_revisions.end();
    if (!approval ||
        approval->service_generation != bindings.service_generation ||
        !IsIdentifier(approval->task_id) ||
        !IsIdentifier(approval->action_id) ||
        !IsIdentifier(approval->receipt_id) ||
        !IsIdentifier(approval->browser_session_id) ||
        !IsSha256Digest(approval->proposal_digest) ||
        approval->committed_revision == 0u ||
        approval->expires_at_monotonic_ms == 0u ||
        approval->expires_at_utc_ms == 0u || task == task_revisions.end() ||
        task->second != approval->committed_revision ||
        terminal_task_ids.contains(approval->task_id) ||
        settlement_task_ids.contains(approval->task_id)) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
    ApprovalKey key(approval->task_id, approval->action_id);
    if (pending_approvals.contains(key) ||
        !committed_approval_keys.insert(std::move(key)).second) {
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    }
  }

  task_revisions_ = std::move(task_revisions);
  task_controls_ = std::move(task_controls);
  pending_approvals_ = std::move(pending_approvals);
  pending_permissions_ = std::move(pending_permissions);
  task_settlements_ = std::move(task_settlements);
  terminal_tasks_ = std::move(terminal_tasks);
  service_generation_ = bindings.service_generation;
  state_sequence_ = bindings.state_sequence;
  return mojom::PendingApprovalRegistrationStatus::kRegistered;
}

void CoreStateBindingRegistry::Reset() {
  task_revisions_.clear();
  task_controls_.clear();
  pending_approvals_.clear();
  pending_permissions_.clear();
  task_settlements_.clear();
  terminal_tasks_.clear();
  service_generation_ = 0u;
  state_sequence_ = 0u;
}

std::optional<TaskControlLookup> CoreStateBindingRegistry::FindTaskControl(
    const std::string& task_id,
    mojom::TaskControlKind kind) const {
  const auto revision = task_revisions_.find(task_id);
  const auto controls = task_controls_.find(task_id);
  if (revision == task_revisions_.end() || controls == task_controls_.end() ||
      !TaskControlRank(kind) ||
      std::find(controls->second.begin(), controls->second.end(), kind) ==
          controls->second.end()) {
    return std::nullopt;
  }
  return TaskControlLookup{service_generation_, revision->second, kind};
}

std::optional<uint64_t> CoreStateBindingRegistry::FindTaskRevision(
    const std::string& task_id) const {
  const auto found = task_revisions_.find(task_id);
  return found == task_revisions_.end()
             ? std::nullopt
             : std::optional<uint64_t>(found->second);
}

std::optional<PendingApprovalLookup>
CoreStateBindingRegistry::FindPendingApproval(
    const std::string& task_id,
    const std::string& action_id) const {
  const auto found = pending_approvals_.find(ApprovalKey(task_id, action_id));
  return found == pending_approvals_.end()
             ? std::nullopt
             : std::optional<PendingApprovalLookup>(found->second);
}

std::optional<PendingPermissionLookup>
CoreStateBindingRegistry::FindPendingPermission(
    const std::string& request_id) const {
  const auto found = pending_permissions_.find(request_id);
  return found == pending_permissions_.end()
             ? std::nullopt
             : std::optional<PendingPermissionLookup>(found->second);
}

std::optional<TaskSettlementLookup>
CoreStateBindingRegistry::FindTaskSettlement(const std::string& task_id) const {
  const auto found =
      std::find_if(task_settlements_.begin(), task_settlements_.end(),
                   [&task_id](const TaskSettlementLookup& settlement) {
                     return settlement.task_id == task_id;
                   });
  return found == task_settlements_.end()
             ? std::nullopt
             : std::optional<TaskSettlementLookup>(*found);
}

std::optional<TerminalTaskLookup> CoreStateBindingRegistry::FindTerminalTask(
    const std::string& task_id) const {
  const auto found =
      std::find_if(terminal_tasks_.begin(), terminal_tasks_.end(),
                   [&task_id](const TerminalTaskLookup& terminal) {
                     return terminal.task_id == task_id;
                   });
  return found == terminal_tasks_.end()
             ? std::nullopt
             : std::optional<TerminalTaskLookup>(*found);
}

}  // namespace taffy
