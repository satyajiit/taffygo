// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
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

void RetainEarlierDeadline(uint64_t candidate,
                           std::optional<uint64_t>* earliest) {
  if (candidate != 0u && (!*earliest || candidate < **earliest)) {
    *earliest = candidate;
  }
}

bool IsSurfaceExpired(const CoreDeferredTaskSurface& surface,
                      const CorePendingAdmission* pending,
                      uint64_t now_monotonic_ms,
                      uint64_t now_utc_ms) {
  if (!surface.binding || !surface.binding->operation ||
      now_monotonic_ms >= surface.binding->operation->deadline_monotonic_ms) {
    return true;
  }
  if (surface.binding->permission &&
      (now_monotonic_ms >= surface.binding->permission->deadline_monotonic_ms ||
       now_utc_ms >= surface.binding->permission->deadline_utc_ms)) {
    return true;
  }
  if (!pending || !pending->command || !pending->command->operation) {
    return false;
  }
  if (now_monotonic_ms >= pending->command->operation->deadline_monotonic_ms) {
    return true;
  }
  return pending->command->user_decision &&
         (now_monotonic_ms >= pending->command->user_decision
                                  ->approval_expires_at_monotonic_ms ||
          now_utc_ms >=
              pending->command->user_decision->approval_expires_at_utc_ms);
}

std::optional<uint64_t> SurfaceDeadline(const CoreDeferredTaskSurface& surface,
                                        const CorePendingAdmission* pending) {
  std::optional<uint64_t> earliest;
  if (surface.binding && surface.binding->operation) {
    RetainEarlierDeadline(surface.binding->operation->deadline_monotonic_ms,
                          &earliest);
  }
  if (surface.binding && surface.binding->permission) {
    RetainEarlierDeadline(surface.binding->permission->deadline_monotonic_ms,
                          &earliest);
  }
  if (pending && pending->command && pending->command->operation) {
    RetainEarlierDeadline(pending->command->operation->deadline_monotonic_ms,
                          &earliest);
  }
  if (pending && pending->command && pending->command->user_decision) {
    RetainEarlierDeadline(
        pending->command->user_decision->approval_expires_at_monotonic_ms,
        &earliest);
  }
  return earliest;
}

}  // namespace

void CoreDeferredTaskSurfaceOwner::Reconcile(CoreServiceManager& manager) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  const uint64_t now_monotonic_ms = NowMonotonicMillis();
  const uint64_t now_utc_ms = NowUtcMillis();
  std::vector<std::pair<std::string, service_mojom::AdmissionStatus>> failures;
  for (auto it = manager.deferred_task_surfaces_.begin();
       it != manager.deferred_task_surfaces_.end();) {
    CoreDeferredTaskSurface& surface = it->second;
    CorePendingAdmission* pending = nullptr;
    if (surface.owner_operation_id) {
      const auto found =
          manager.pending_admissions_.find(*surface.owner_operation_id);
      if (found == manager.pending_admissions_.end()) {
        if (!surface.admitted) {
          it = manager.deferred_task_surfaces_.erase(it);
          continue;
        }
      } else {
        pending = &found->second;
        if (pending->sent) {
          ++it;
          continue;
        }
      }
    }
    if (!surface.binding || !surface.binding->operation ||
        surface.binding->operation->service_generation !=
            manager.service_generation_) {
      failures.emplace_back(it->first,
                            service_mojom::AdmissionStatus::kStaleGeneration);
      ++it;
      continue;
    }
    if (IsSurfaceExpired(surface, pending, now_monotonic_ms, now_utc_ms)) {
      failures.emplace_back(it->first,
                            service_mojom::AdmissionStatus::kDeadlineExceeded);
      ++it;
      continue;
    }
    if (!manager.state_cache_.latest() ||
        manager.state_cache_.last_sequence() <= surface.state_sequence) {
      ++it;
      continue;
    }

    const service_mojom::TaskEffectBinding& binding = *surface.binding;
    const std::optional<uint64_t> revision =
        manager.FindTaskRevision(binding.task_id);
    bool exact_binding = false;
    if (binding.approval) {
      const std::optional<PendingApprovalLookup> current =
          manager.FindPendingApproval(binding.task_id,
                                      binding.approval->action_id);
      exact_binding =
          current &&
          current->service_generation == manager.service_generation_ &&
          current->proposal_digest == binding.approval->proposal_digest &&
          revision && current->task_revision == *revision;
    } else if (binding.permission) {
      const std::optional<PendingPermissionLookup> current =
          manager.FindPendingPermission(binding.permission->request_id);
      exact_binding =
          current && current->task_id == binding.task_id &&
          current->service_generation == manager.service_generation_ &&
          current->permission == binding.permission->permission &&
          current->deadline_monotonic_ms ==
              binding.permission->deadline_monotonic_ms &&
          current->deadline_utc_ms == binding.permission->deadline_utc_ms &&
          current->browser_session_id ==
              binding.permission->browser_session_id &&
          revision && current->task_revision == *revision;
    } else if (binding.field_values) {
      // The field request has no row in CoreStateBrowserBindings. Its exact
      // witness is the retained effect identity plus this unchanged task
      // revision: every reducer path that clears `pending_field_values`
      // records an event and therefore advances the revision. The browser's
      // emitted-request set cannot be the witness here because the browser
      // deliberately closes that presentation as soon as it has secured the
      // person's answer, before the retained command is admitted.
      exact_binding = revision.has_value();
    }
    if (!exact_binding || !revision ||
        *revision != binding.operation->task_revision) {
      failures.emplace_back(it->first,
                            service_mojom::AdmissionStatus::kStaleRevision);
      ++it;
      continue;
    }
    if (!pending) {
      if (surface.admitted) {
        ++it;
      } else {
        it = manager.deferred_task_surfaces_.erase(it);
      }
      continue;
    }
    pending->deferred_task_surface->ready = true;
    ++it;
  }

  for (const auto& [effect_id, status] : failures) {
    Forget(manager, effect_id, status);
  }
  ScheduleDeadline(manager);
  manager.DispatchQueuedCommands();
}

void CoreDeferredTaskSurfaceOwner::ScheduleDeadline(
    CoreServiceManager& manager) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  manager.deferred_task_surface_deadline_timer_.Stop();
  std::optional<uint64_t> earliest;
  for (const auto& [effect_id, surface] : manager.deferred_task_surfaces_) {
    static_cast<void>(effect_id);
    const CorePendingAdmission* pending = nullptr;
    if (surface.owner_operation_id) {
      const auto found =
          manager.pending_admissions_.find(*surface.owner_operation_id);
      if (found != manager.pending_admissions_.end()) {
        if (found->second.sent) {
          continue;
        }
        pending = &found->second;
      }
    }
    const std::optional<uint64_t> deadline = SurfaceDeadline(surface, pending);
    if (deadline) {
      RetainEarlierDeadline(*deadline, &earliest);
    }
  }
  if (!earliest) {
    return;
  }
  const uint64_t now = NowMonotonicMillis();
  const uint64_t delay_ms = *earliest > now ? *earliest - now : 0u;
  manager.deferred_task_surface_deadline_timer_.Start(
      FROM_HERE, base::Milliseconds(delay_ms),
      base::BindOnce(
          [](base::WeakPtr<CoreServiceManager> manager) {
            if (manager) {
              CoreDeferredTaskSurfaceOwner::Reconcile(*manager);
            }
          },
          manager.weak_factory_.GetWeakPtr()));
}

void CoreDeferredTaskSurfaceOwner::FailAll(
    CoreServiceManager& manager,
    service_mojom::AdmissionStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  std::vector<std::string> effect_ids;
  effect_ids.reserve(manager.deferred_task_surfaces_.size());
  for (const auto& [effect_id, surface] : manager.deferred_task_surfaces_) {
    static_cast<void>(surface);
    effect_ids.push_back(effect_id);
  }
  for (const std::string& effect_id : effect_ids) {
    Forget(manager, effect_id, status);
  }
  manager.deferred_task_surface_deadline_timer_.Stop();
}

}  // namespace taffy
