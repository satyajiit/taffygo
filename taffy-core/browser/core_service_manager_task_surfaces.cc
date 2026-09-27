// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_deferred_task_surface_owner.h"
#include "taffy/browser/core_service_manager.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

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

bool ApprovalMatches(const service_mojom::TaskEffectBinding& binding,
                     const PendingApprovalLookup& pending,
                     uint64_t generation) {
  return binding.operation && binding.approval &&
         pending.service_generation == generation &&
         pending.task_revision == binding.operation->task_revision &&
         pending.proposal_digest == binding.approval->proposal_digest;
}

bool PermissionMatches(const service_mojom::TaskEffectBinding& binding,
                       const PendingPermissionLookup& pending,
                       uint64_t generation, uint64_t now_monotonic_ms) {
  return binding.operation && binding.permission &&
         pending.task_id == binding.task_id &&
         pending.service_generation == generation &&
         pending.task_revision == binding.operation->task_revision &&
         pending.permission == binding.permission->permission &&
         pending.deadline_monotonic_ms ==
             binding.permission->deadline_monotonic_ms &&
         now_monotonic_ms < pending.deadline_monotonic_ms;
}

}  // namespace

// A surface succeeds only when the state that carries it has reached the
// native owner and this registry still holds that state's bindings. Both
// halves are checked here rather than at the call sites, so the immediate
// path and the on-publication path cannot answer the same question two ways.
service_mojom::TaskEffectCompletionStatus
CoreServiceManager::ResolveTaskSurfaceStatus(
    const service_mojom::TaskEffectBinding& binding, uint64_t state_sequence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (observers_.empty() ||
      state_bindings_.state_sequence() != state_sequence) {
    return service_mojom::TaskEffectCompletionStatus::kUnavailable;
  }
  if (binding.kind ==
          service_mojom::TaskReducerEffectKind::kRequestApproval &&
      binding.approval) {
    const std::optional<PendingApprovalLookup> approval =
        state_bindings_.FindPendingApproval(binding.task_id,
                                            binding.approval->action_id);
    if (approval && ApprovalMatches(binding, *approval, service_generation_)) {
      // The published state carried the safe proposal view. The browser
      // digest remains in this registry and never enters the UI contract.
      return service_mojom::TaskEffectCompletionStatus::kSucceeded;
    }
    return service_mojom::TaskEffectCompletionStatus::kUnavailable;
  }
  if (binding.kind ==
          service_mojom::TaskReducerEffectKind::kRequestPermission &&
      binding.permission) {
    const std::optional<PendingPermissionLookup> permission =
        state_bindings_.FindPendingPermission(binding.permission->request_id);
    if (!permission ||
        !PermissionMatches(binding, *permission, service_generation_,
                           NowMonotonicMillis())) {
      return service_mojom::TaskEffectCompletionStatus::kUnavailable;
    }
    if (emitted_permission_requests_.insert(binding.permission->request_id)
            .second) {
      for (Observer& observer : observers_) {
        observer.OnCorePermissionRequest(binding.permission->request_id,
                                         permission->api_permission);
      }
    }
    // A prior exact emission means the same native owner already has the
    // request. It is still one accepted ownership transfer, not a second
    // prompt or a fabricated success.
    return service_mojom::TaskEffectCompletionStatus::kSucceeded;
  }
  if (binding.kind ==
          service_mojom::TaskReducerEffectKind::kRequestFieldValues &&
      binding.field_values) {
    // No pending-binding lookup, and the absence is worth naming. An approval
    // and a permission are each registered in the state bindings, so this
    // method can ask the registry whether the surface it is about is one the
    // published state carried. A field-value request has no such row: the
    // Core Service contract's browser bindings carry none, and the request's
    // own gate is the tab check ExecuteTaskEffect already made — which is a
    // browser-owned authority fact rather than a restatement of what the core
    // said.
    //
    // The dedupe set is the permission arm's, exactly: one request identity
    // is handed over once, and a repeat is one accepted ownership transfer
    // rather than a second sheet.
    //
    // Two hand-offs, and they are not the same hand-off. The coordinator is
    // the browser's own owner of the ask: it re-reads the form, draws the
    // sheet a person types into, mints what they type and reports the count.
    // The observers are the surfaces that are *watching* the profile, and
    // what they learn is only that a request is open — the same fact the
    // status plane carries as `pending_field_value_request`, delivered at the
    // moment it becomes true rather than at the next publication.
    if (emitted_field_value_requests_.insert(binding.field_values->request_id)
            .second) {
      OpenFieldValueRequest(binding.field_values->request_id, binding.task_id,
                            binding.field_values->tab_id,
                            binding.field_values->node_id,
                            binding.field_values->companion_node_ids);
      for (Observer& observer : observers_) {
        observer.OnCoreFieldValueRequest(binding.field_values->request_id,
                                         binding.task_id,
                                         binding.field_values->tab_id,
                                         binding.field_values->node_id);
      }
    }
    return service_mojom::TaskEffectCompletionStatus::kSucceeded;
  }
  return service_mojom::TaskEffectCompletionStatus::kUnavailable;
}

void CoreServiceManager::QueueTaskSurface(
    service_mojom::TaskEffectBindingPtr binding,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (binding && binding->kind ==
                     service_mojom::TaskReducerEffectKind::kRequestApproval &&
      binding->approval && binding->approval->form_action) {
    const service_mojom::TaskExecutableAction& action =
        *binding->approval->form_action;
    const bool exact_fill_shape =
        action.action_class == service_mojom::PolicyActionClass::kFillField &&
        action.operation_kind ==
            service_mojom::TaskActionOperationKind::kFormFill &&
        action.node_id && action.input && action.input->supplied_value &&
        !action.destination_origin && !action.destination_address &&
        !action.operand_handle && !action.transient_search_query &&
        !action.task_tab && !action.task_download && !action.task_store &&
        TaskOperationMatchesClassAndTool(action.operation_kind,
                                         action.action_class,
                                         action.tool_name) &&
        TaskActionInputMatchesOperationAndCanonical(
            action.input.get(), action.operation_kind, action.canonical_intent,
            action.tab_id, action.node_id);
    const FormApprovalKey approval_key{binding->task_id,
                                       binding->approval->action_id};
    if (!exact_fill_shape || !field_value_requests_ ||
        preapproved_form_actions_.contains(approval_key)) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }

    const std::optional<TaskPolicyDocumentContext> live =
        ResolveTaskPolicyDocument(browser_context_.get(), action.tab_id);
    if (!live || !binding->operation ||
        !accepted_approvals_.IsTaskSourceAuthorized(
            binding->task_id, action.tab_id, *binding->operation, live->origin,
            service_generation_)) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }

    const uint64_t now_monotonic_ms = NowMonotonicMillis();
    const uint64_t now_utc_ms = NowUtcMillis();
    const service_mojom::TaskSuppliedValuePosition& supplied =
        *action.input->supplied_value;
    const std::optional<FormFillPreapprovalReceipt> receipt =
        field_value_requests_->ConsumeFormFillPreapproval(
            binding->task_id, action.tab_id, *action.node_id,
            supplied.request_id, supplied.index, live->origin, live->frame_id,
            live->page_epoch, live->graph_revision, now_monotonic_ms,
            now_utc_ms);
    if (!receipt) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }

    BrowserFormActionPreapproval preapproval{
        .task_id = binding->task_id,
        .action_id = binding->approval->action_id,
        .proposal_digest = binding->approval->proposal_digest,
        .tool_name = action.tool_name,
        .tab_id = action.tab_id,
        .node_id = *action.node_id,
        .request_id = supplied.request_id,
        .canonical_intent = action.canonical_intent,
        .supplied_value_index = supplied.index,
        .normalized_origin = live->origin,
        .frame_id = live->frame_id,
        .page_epoch = live->page_epoch,
        .graph_revision = live->graph_revision,
        .expires_at_monotonic_ms = receipt->expires_at_monotonic_ms,
        .expires_at_utc_ms = receipt->expires_at_utc_ms,
    };
    if (!preapproved_form_actions_
             .emplace(approval_key, std::move(preapproval))
             .second) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }

    CoreApiCommandFactory factory(browser_profile_id_,
                                  CreateCoreApiEntropySource());
    std::optional<ProjectedCoreCommand> projected = factory.BuildApproveAction(
        binding->task_id, binding->approval->action_id,
        binding->approval->proposal_digest, binding->operation->task_revision,
        service_generation_, now_monotonic_ms, now_utc_ms,
        browser_session_id_);
    if (!projected || !projected->core_service_command ||
        !projected->core_service_command->operation ||
        !projected->core_service_command->user_decision) {
      preapproved_form_actions_.erase(approval_key);
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }
    // The authorization expires when the value references do. Do not mint a
    // fresh approval window after the person has left the confirmation.
    projected->core_service_command->operation->deadline_monotonic_ms =
        receipt->expires_at_monotonic_ms;
    projected->core_service_command->user_decision
        ->approval_expires_at_monotonic_ms =
        receipt->expires_at_monotonic_ms;
    projected->core_service_command->user_decision
        ->approval_expires_at_utc_ms = receipt->expires_at_utc_ms;
    const uint64_t state_sequence = state_bindings_.state_sequence();
    const std::string effect_id = binding->effect_id;
    if (!CoreDeferredTaskSurfaceOwner::Remember(*this, *binding,
                                                state_sequence)) {
      preapproved_form_actions_.erase(approval_key);
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }
    const CoreDeferredTaskSurfaceOwnership ownership =
        CoreDeferredTaskSurfaceOwner::Own(
            *this, effect_id, std::move(projected->core_service_command),
            base::BindOnce(
                [](base::WeakPtr<CoreServiceManager> manager,
                   uint64_t generation, FormApprovalKey approval_key,
                   service_mojom::AdmissionPtr admission) {
                  if (manager && generation == manager->service_generation_ &&
                      (!admission ||
                       admission->status !=
                           service_mojom::AdmissionStatus::kAccepted)) {
                    manager->preapproved_form_actions_.erase(approval_key);
                  }
                },
                weak_factory_.GetWeakPtr(), service_generation_, approval_key));
    if (ownership != CoreDeferredTaskSurfaceOwnership::kOwned) {
      preapproved_form_actions_.erase(approval_key);
      CoreDeferredTaskSurfaceOwner::Forget(
          *this, effect_id,
          service_mojom::AdmissionStatus::kInvalidCommand);
      std::move(callback).Run(MakeTaskEffectCompletion(
          binding.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }
    // Succeeded means only that the browser owns the exact decision and its
    // admission callback. The command remains deferred until a strictly newer
    // publication retains this action; the Core Service alone later reports
    // whether it accepted it.
    std::move(callback).Run(MakeTaskEffectCompletion(
        binding.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded));
    return;
  }
  if (!binding || observers_.empty() ||
      pending_task_surfaces_.size() >=
          service_mojom::kMaxTaskEffectsPerState ||
      pending_task_surfaces_.contains(binding->effect_id) ||
      deferred_task_surfaces_.contains(binding->effect_id)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        binding.get(),
        service_mojom::TaskEffectCompletionStatus::kUnavailable));
    return;
  }
  // The surface belongs to the state whose bindings are currently registered.
  // If that state has already reached the native owner, the condition this
  // surface waits on is met now: queueing it would wait for a publication of a
  // sequence that has already happened and can never happen again, and the
  // service will not produce a later sequence while this effect is
  // outstanding. That is a permanent stall, so it is answered here instead of
  // depending on the order the service happened to send two messages in.
  const uint64_t state_sequence = state_bindings_.state_sequence();
  const std::string effect_id = binding->effect_id;
  if (!CoreDeferredTaskSurfaceOwner::Remember(*this, *binding,
                                              state_sequence)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        binding.get(), service_mojom::TaskEffectCompletionStatus::kUnavailable));
    return;
  }
  if (state_cache_.latest() &&
      state_cache_.last_sequence() == state_sequence) {
    const service_mojom::TaskEffectCompletionStatus status =
        ResolveTaskSurfaceStatus(*binding, state_sequence);
    if (status != service_mojom::TaskEffectCompletionStatus::kSucceeded) {
      CoreDeferredTaskSurfaceOwner::Forget(
          *this, effect_id,
          service_mojom::AdmissionStatus::kStaleRevision);
    }
    std::move(callback).Run(MakeTaskEffectCompletion(binding.get(), status));
    return;
  }
  PendingTaskSurface pending;
  pending.state_sequence = state_sequence;
  pending.binding = std::move(binding);
  pending.callback = std::move(callback);
  pending_task_surfaces_.emplace(effect_id, std::move(pending));
  RefreshIdleTeardown();
}

void CoreDeferredTaskSurfaceOwner::CompletePublished(
    CoreServiceManager& manager,
    uint64_t state_sequence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager.sequence_checker_);
  auto pending_surfaces = std::move(manager.pending_task_surfaces_);
  manager.pending_task_surfaces_.clear();
  manager.RefreshIdleTeardown();
  for (auto &[effect_id, pending] : pending_surfaces) {
    static_cast<void>(effect_id);
    service_mojom::TaskEffectCompletionStatus status =
        service_mojom::TaskEffectCompletionStatus::kUnavailable;
    if (pending.binding && pending.state_sequence == state_sequence) {
      status =
          manager.ResolveTaskSurfaceStatus(*pending.binding, state_sequence);
    }
    if (status != service_mojom::TaskEffectCompletionStatus::kSucceeded) {
      Forget(manager, effect_id,
             service_mojom::AdmissionStatus::kStaleRevision);
    }
    std::move(pending.callback)
        .Run(MakeTaskEffectCompletion(pending.binding.get(), status));
  }
}

void CoreServiceManager::ResolvePendingTaskSurfacesUnavailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CoreDeferredTaskSurfaceOwner::FailAll(
      *this, service_mojom::AdmissionStatus::kCoreUnavailable);
  auto pending_surfaces = std::move(pending_task_surfaces_);
  pending_task_surfaces_.clear();
  RefreshIdleTeardown();
  for (auto &[effect_id, pending] : pending_surfaces) {
    static_cast<void>(effect_id);
    std::move(pending.callback)
        .Run(MakeTaskEffectCompletion(
            pending.binding.get(),
            service_mojom::TaskEffectCompletionStatus::kUnavailable));
  }
}

}  // namespace taffy
