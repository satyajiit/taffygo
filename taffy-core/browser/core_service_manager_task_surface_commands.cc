// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "taffy/browser/core_deferred_task_surface_owner.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool SameSurfaceSubject(const service_mojom::TaskEffectBinding& left,
                        const service_mojom::TaskEffectBinding& right) {
  if (left.kind != right.kind || left.task_id != right.task_id) {
    return false;
  }
  if (left.approval && right.approval) {
    return left.approval->action_id == right.approval->action_id;
  }
  if (left.permission && right.permission) {
    return left.permission->request_id == right.permission->request_id;
  }
  if (left.field_values && right.field_values) {
    return left.field_values->request_id == right.field_values->request_id;
  }
  return false;
}

}  // namespace

CoreDeferredTaskSurfaceMatch CoreDeferredTaskSurfaceOwner::Match(
    const CoreServiceManager& manager,
    const service_mojom::CoreServiceCommand& command,
    std::string* effect_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  const bool is_surface_command = command.user_decision ||
                                  command.permission_result ||
                                  command.supply_field_values;
  if (!is_surface_command || !command.operation) {
    return CoreDeferredTaskSurfaceMatch::kNone;
  }

  CoreDeferredTaskSurfaceMatch result = CoreDeferredTaskSurfaceMatch::kNone;
  for (const auto& [candidate_id, surface] : manager.deferred_task_surfaces_) {
    if (!surface.binding || !surface.binding->operation) {
      continue;
    }
    const service_mojom::TaskEffectBinding& binding = *surface.binding;
    bool same_subject = false;
    bool exact = false;
    if (command.user_decision && binding.approval) {
      same_subject =
          command.user_decision->task_id == binding.task_id &&
          command.user_decision->action_id == binding.approval->action_id;
      exact = same_subject && command.user_decision->approval_digest ==
                                  binding.approval->proposal_digest;
    } else if (command.permission_result && binding.permission) {
      same_subject = command.permission_result->request_id ==
                     binding.permission->request_id;
      exact = same_subject &&
              command.permission_result->task_id == binding.task_id &&
              command.permission_result->permission ==
                  binding.permission->permission;
    } else if (command.supply_field_values && binding.field_values) {
      same_subject = command.supply_field_values->request_id ==
                     binding.field_values->request_id;
      exact = same_subject &&
              command.supply_field_values->task_id == binding.task_id;
    }
    if (!same_subject) {
      continue;
    }
    exact =
        exact &&
        command.operation->service_generation ==
            binding.operation->service_generation &&
        command.operation->task_revision == binding.operation->task_revision;
    if (result != CoreDeferredTaskSurfaceMatch::kNone || !exact) {
      return CoreDeferredTaskSurfaceMatch::kConflict;
    }
    result = CoreDeferredTaskSurfaceMatch::kExact;
    if (effect_id) {
      *effect_id = candidate_id;
    }
  }
  return result;
}

CoreDeferredTaskSurfaceOwnership CoreDeferredTaskSurfaceOwner::Own(
    CoreServiceManager& manager,
    const std::string& effect_id,
    service_mojom::CoreServiceCommandPtr command,
    CoreServiceSubmitCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  const std::string operation_id = command && command->operation
                                       ? command->operation->operation_id
                                       : std::string();
  const auto refuse = [&manager, &operation_id,
                       &callback](service_mojom::AdmissionStatus status) {
    std::move(callback).Run(manager.MakeAdmission(operation_id, status));
    return CoreDeferredTaskSurfaceOwnership::kRefused;
  };
  std::string matching_effect_id;
  if (manager.shutdown_started_ || !command ||
      !manager.ValidateCommand(*command) ||
      Match(manager, *command, &matching_effect_id) !=
          CoreDeferredTaskSurfaceMatch::kExact ||
      matching_effect_id != effect_id) {
    return refuse(service_mojom::AdmissionStatus::kInvalidCommand);
  }

  auto surface = manager.deferred_task_surfaces_.find(effect_id);
  if (surface == manager.deferred_task_surfaces_.end() ||
      !surface->second.binding) {
    return refuse(service_mojom::AdmissionStatus::kInvalidCommand);
  }
  if (surface->second.owner_operation_id) {
    return refuse(service_mojom::AdmissionStatus::kDuplicate);
  }
  const uint64_t now_monotonic_ms = NowMonotonicMillis();
  const uint64_t now_utc_ms = NowUtcMillis();
  const service_mojom::TaskEffectBinding& binding = *surface->second.binding;
  const bool permission_expired =
      binding.permission &&
      (now_monotonic_ms >= binding.permission->deadline_monotonic_ms ||
       now_utc_ms >= binding.permission->deadline_utc_ms);
  const bool approval_expired =
      command->user_decision &&
      (now_monotonic_ms >=
           command->user_decision->approval_expires_at_monotonic_ms ||
       now_utc_ms >= command->user_decision->approval_expires_at_utc_ms);
  if (now_monotonic_ms >= binding.operation->deadline_monotonic_ms ||
      now_monotonic_ms >= command->operation->deadline_monotonic_ms ||
      permission_expired || approval_expired) {
    manager.deferred_task_surfaces_.erase(surface);
    ScheduleDeadline(manager);
    return refuse(service_mojom::AdmissionStatus::kDeadlineExceeded);
  }
  if (manager.recovery_policy_.circuit_open()) {
    manager.SetAvailability(CoreServiceManager::Availability::kCircuitOpen);
    return refuse(service_mojom::AdmissionStatus::kCoreUnavailable);
  }
  if (manager.pending_admissions_.size() +
              manager.pending_policy_evaluations_.size() >=
          service_mojom::kMaxInFlightPerProfile ||
      manager.pending_admissions_.contains(operation_id)) {
    return refuse(manager.pending_admissions_.contains(operation_id)
                      ? service_mojom::AdmissionStatus::kDuplicate
                      : service_mojom::AdmissionStatus::kBackpressure);
  }

  CorePendingAdmission pending;
  pending.command = std::move(command);
  pending.callback = std::move(callback);
  pending.deferred_task_surface =
      CorePendingAdmission::DeferredTaskSurface{effect_id, false};
  manager.pending_admissions_.emplace(operation_id, std::move(pending));
  surface->second.owner_operation_id = operation_id;
  manager.RefreshIdleTeardown();
  manager.EnsureStarted();
  ScheduleDeadline(manager);
  return CoreDeferredTaskSurfaceOwnership::kOwned;
}

bool CoreDeferredTaskSurfaceOwner::Remember(
    CoreServiceManager& manager,
    const service_mojom::TaskEffectBinding& binding,
    uint64_t state_sequence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  if (!binding.operation || binding.effect_id.empty() ||
      binding.operation->service_generation != manager.service_generation_ ||
      binding.operation->task_revision == 0u ||
      state_sequence != manager.state_bindings_.state_sequence() ||
      manager.deferred_task_surfaces_.contains(binding.effect_id) ||
      (!binding.approval && !binding.permission && !binding.field_values)) {
    return false;
  }
  std::optional<std::string> superseded_effect_id;
  for (const auto& [effect_id, existing] : manager.deferred_task_surfaces_) {
    if (existing.binding && SameSurfaceSubject(binding, *existing.binding)) {
      // Action and request identifiers need not be globally unique. A newer
      // effect for the same subject explicitly supersedes an admitted guard
      // before its state is published; no observer can tap in between these
      // two browser-owned mutations.
      if (!existing.admitted || state_sequence <= existing.state_sequence) {
        return false;
      }
      superseded_effect_id = effect_id;
      break;
    }
  }
  if (!superseded_effect_id && manager.deferred_task_surfaces_.size() >=
                                   service_mojom::kMaxTaskEffectsPerState) {
    return false;
  }
  if (superseded_effect_id) {
    manager.deferred_task_surfaces_.erase(*superseded_effect_id);
  }
  CoreDeferredTaskSurface surface;
  surface.state_sequence = state_sequence;
  surface.binding = binding.Clone();
  manager.deferred_task_surfaces_.emplace(binding.effect_id,
                                          std::move(surface));
  ScheduleDeadline(manager);
  return true;
}

void CoreDeferredTaskSurfaceOwner::RecordAdmission(
    CoreServiceManager& manager,
    const std::string& effect_id,
    const std::string& operation_id,
    service_mojom::AdmissionStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  const auto surface = manager.deferred_task_surfaces_.find(effect_id);
  if (surface == manager.deferred_task_surfaces_.end() ||
      surface->second.owner_operation_id != operation_id) {
    return;
  }
  if (status != service_mojom::AdmissionStatus::kAccepted) {
    Forget(manager, effect_id, status);
    return;
  }
  surface->second.admitted = true;
  // A state may have arrived ahead of the Mojo admission reply. Reconcile it
  // now so an already-advanced binding does not leave a guard until timeout.
  Reconcile(manager);
}

void CoreDeferredTaskSurfaceOwner::Forget(
    CoreServiceManager& manager,
    const std::string& effect_id,
    service_mojom::AdmissionStatus pending_status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  const auto surface = manager.deferred_task_surfaces_.find(effect_id);
  if (surface == manager.deferred_task_surfaces_.end()) {
    return;
  }
  CoreServiceSubmitCallback callback;
  std::string operation_id;
  if (surface->second.owner_operation_id) {
    operation_id = *surface->second.owner_operation_id;
    const auto pending = manager.pending_admissions_.find(operation_id);
    if (pending != manager.pending_admissions_.end() &&
        pending->second.deferred_task_surface &&
        pending->second.deferred_task_surface->effect_id == effect_id &&
        !pending->second.sent) {
      callback = std::move(pending->second.callback);
      manager.pending_admissions_.erase(pending);
    }
  }
  manager.deferred_task_surfaces_.erase(surface);
  manager.RefreshIdleTeardown();
  ScheduleDeadline(manager);
  if (callback) {
    std::move(callback).Run(
        manager.MakeAdmission(operation_id, pending_status));
  }
}

}  // namespace taffy
