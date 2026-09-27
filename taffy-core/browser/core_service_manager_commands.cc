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
#include "base/logging.h"
#include "base/time/time.h"
#include "taffy/browser/core_deferred_task_surface_owner.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

namespace service_mojom = core_service::mojom;

void CoreServiceManager::Submit(service_mojom::CoreServiceCommandPtr command,
                                CoreServiceSubmitCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !command || !ValidateCommand(*command)) {
    const std::string operation_id = command && command->operation
                                         ? command->operation->operation_id
                                         : std::string();
    // Named. Every command the browser refuses before the ordered core sees it
    // leaves as one `kInvalidCommand`, and the surfaces spell that one way —
    // a start says "Taffy could not read this request" whether the shutdown
    // flag, a structural rule or the revision half answered. The label is
    // compiled in; the only value printed is the command kind, a closed
    // enumeration that names no content.
    const char* refusal = "shutdown";
    if (!shutdown_started_) {
      refusal = command ? CoreServiceCommandStructuralRefusal(
                              *command, service_mojom::kMaxCommandBytes)
                        : "command-missing";
      if (!refusal) {
        // Structurally valid, so the revision, generation or correlation half
        // of `ValidateCommand` is what refused it.
        refusal = "revision";
      }
    }
    LOG(WARNING) << "[taffy_command_refused] at=submit/" << refusal << " kind="
                 << (command ? static_cast<int>(command->kind) : -1);
    std::move(callback).Run(MakeAdmission(
        operation_id, service_mojom::AdmissionStatus::kInvalidCommand));
    return;
  }

  std::string deferred_effect_id;
  const CoreDeferredTaskSurfaceMatch deferred_match =
      CoreDeferredTaskSurfaceOwner::Match(*this, *command, &deferred_effect_id);
  if (deferred_match == CoreDeferredTaskSurfaceMatch::kConflict) {
    LOG(WARNING) << "[taffy_command_refused] at=submit/deferred-conflict"
                 << " kind=" << static_cast<int>(command->kind);
    std::move(callback).Run(
        MakeAdmission(command->operation->operation_id,
                      service_mojom::AdmissionStatus::kInvalidCommand));
    return;
  }
  if (deferred_match == CoreDeferredTaskSurfaceMatch::kExact) {
    static_cast<void>(CoreDeferredTaskSurfaceOwner::Own(
        *this, deferred_effect_id, std::move(command), std::move(callback)));
    return;
  }

  command->operation->service_generation = service_generation_;
  if (command->set_asset_delivery_policy) {
    asset_metered_permitted_ =
        command->set_asset_delivery_policy->metered_permitted;
  }
  const std::string operation_id = command->operation->operation_id;
  if (recovery_policy_.circuit_open()) {
    std::move(callback).Run(MakeAdmission(
        operation_id, service_mojom::AdmissionStatus::kCoreUnavailable));
    SetAvailability(Availability::kCircuitOpen);
    return;
  }
  if (pending_admissions_.size() + pending_policy_evaluations_.size() >=
          service_mojom::kMaxInFlightPerProfile ||
      pending_admissions_.contains(operation_id)) {
    std::move(callback).Run(MakeAdmission(
        operation_id, service_mojom::AdmissionStatus::kBackpressure));
    return;
  }

  pending_admissions_.emplace(
      operation_id, PendingAdmission{std::move(command), std::move(callback),
                                     false, std::nullopt});
  RefreshIdleTeardown();
  EnsureStarted();
  DispatchQueuedCommands();
}

void CoreServiceManager::Cancel(service_mojom::OperationEnvelopePtr operation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!operation) {
    return;
  }
  auto it = pending_admissions_.find(operation->operation_id);
  if (it != pending_admissions_.end()) {
    if (it->second.deferred_task_surface) {
      deferred_task_surfaces_.erase(
          it->second.deferred_task_surface->effect_id);
      CoreDeferredTaskSurfaceOwner::ScheduleDeadline(*this);
    }
    CoreServiceSubmitCallback callback = std::move(it->second.callback);
    accepted_approvals_.RecordAdmission(
        operation->operation_id,
        service_mojom::AdmissionStatus::kCoreUnavailable);
    pending_admissions_.erase(it);
    RefreshIdleTeardown();
    std::move(callback).Run(
        MakeAdmission(operation->operation_id,
                      service_mojom::AdmissionStatus::kCoreUnavailable));
  }
  if (session_.is_bound() &&
      operation->service_generation == service_generation_) {
    session_->Cancel(std::move(operation));
  }
}

void CoreServiceManager::DispatchQueuedCommands() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (availability_ != Availability::kReady || !session_.is_bound()) {
    return;
  }
  std::vector<std::string> ready;
  for (const auto& [operation_id, pending] : pending_admissions_) {
    if (!pending.sent && (!pending.deferred_task_surface ||
                          pending.deferred_task_surface->ready)) {
      ready.push_back(operation_id);
    }
  }
  std::vector<std::string> ready_policy;
  for (const auto& [operation_id, pending] : pending_policy_evaluations_) {
    if (!pending.sent) {
      ready_policy.push_back(operation_id);
    }
  }
  for (const std::string& operation_id : ready) {
    auto it = pending_admissions_.find(operation_id);
    if (it == pending_admissions_.end()) {
      continue;
    }
    const AuthoritySubmissionStage authority_stage =
        accepted_approvals_.StageSubmittedCommand(
            *it->second.command, browser_profile_id_, browser_session_id_,
            state_bindings_, NowMonotonicMillis(), NowUtcMillis());
    if (authority_stage == AuthoritySubmissionStage::kInvalid) {
      LOG(WARNING) << "[taffy_command_refused] at=authority/"
                   << accepted_approvals_.last_stage_refusal()
                   << " kind=" << static_cast<int>(it->second.command->kind);
      if (it->second.deferred_task_surface) {
        deferred_task_surfaces_.erase(
            it->second.deferred_task_surface->effect_id);
        CoreDeferredTaskSurfaceOwner::ScheduleDeadline(*this);
      }
      CoreServiceSubmitCallback callback = std::move(it->second.callback);
      pending_admissions_.erase(it);
      RefreshIdleTeardown();
      std::move(callback).Run(MakeAdmission(
          operation_id, service_mojom::AdmissionStatus::kInvalidCommand));
      continue;
    }
    it->second.sent = true;
    session_->Submit(it->second.command.Clone(),
                     base::BindOnce(&CoreServiceManager::OnAdmission,
                                    weak_factory_.GetWeakPtr(),
                                    service_generation_, operation_id));
  }
  for (const std::string& operation_id : ready_policy) {
    auto it = pending_policy_evaluations_.find(operation_id);
    if (it == pending_policy_evaluations_.end()) {
      continue;
    }
    if (!it->second.request ||
        !ValidatePolicyRequestForCurrentGeneration(*it->second.request)) {
      CoreServicePolicyEvaluationCallback callback =
          std::move(it->second.callback);
      pending_policy_evaluations_.erase(it);
      RefreshIdleTeardown();
      auto result = service_mojom::PolicyEvaluationResult::New();
      result->operation_id = operation_id;
      result->status = service_mojom::PolicyEvaluationStatus::kInvalidRequest;
      std::move(callback).Run(std::move(result));
      continue;
    }
    it->second.sent = true;
    session_->EvaluatePolicy(
        it->second.request.Clone(),
        base::BindOnce(&CoreServiceManager::OnPolicyEvaluated,
                       weak_factory_.GetWeakPtr(), service_generation_,
                       operation_id));
  }
}

void CoreServiceManager::OnAdmission(uint64_t generation,
                                     std::string operation_id,
                                     service_mojom::AdmissionPtr admission) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_admissions_.find(operation_id);
  if (generation != service_generation_ || it == pending_admissions_.end()) {
    ++late_reply_count_;
    return;
  }
  if (!admission || admission->operation_id != operation_id) {
    admission = MakeAdmission(operation_id,
                              service_mojom::AdmissionStatus::kInvalidCommand);
  }
  accepted_approvals_.RecordAdmission(operation_id, admission->status);
  const service_mojom::CoreServiceCommand* command = it->second.command.get();
  if (admission->status == service_mojom::AdmissionStatus::kAccepted &&
      command && command->user_decision &&
      command->user_decision->decision !=
          service_mojom::UserDecisionKind::kAccept) {
    accepted_approvals_.RevokeAction(command->user_decision->task_id,
                                     command->user_decision->action_id);
  }
  if (admission->status == service_mojom::AdmissionStatus::kAccepted &&
      command && command->cancel_task) {
    accepted_approvals_.RevokeTask(command->cancel_task->task_id, generation);
  }
  const std::optional<std::string> deferred_effect_id =
      it->second.deferred_task_surface
          ? std::make_optional(it->second.deferred_task_surface->effect_id)
          : std::nullopt;
  CoreServiceSubmitCallback callback = std::move(it->second.callback);
  pending_admissions_.erase(it);
  if (deferred_effect_id) {
    CoreDeferredTaskSurfaceOwner::RecordAdmission(
        *this, *deferred_effect_id, operation_id, admission->status);
  }
  RefreshIdleTeardown();
  std::move(callback).Run(std::move(admission));
}

void CoreServiceManager::ResolvePendingAdmissionsUnavailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto pending = std::move(pending_admissions_);
  pending_admissions_.clear();
  RefreshIdleTeardown();
  for (auto& [operation_id, request] : pending) {
    if (request.deferred_task_surface) {
      deferred_task_surfaces_.erase(request.deferred_task_surface->effect_id);
    }
    accepted_approvals_.RecordAdmission(
        operation_id, service_mojom::AdmissionStatus::kCoreUnavailable);
    std::move(request.callback)
        .Run(MakeAdmission(operation_id,
                           service_mojom::AdmissionStatus::kCoreUnavailable));
  }
  CoreDeferredTaskSurfaceOwner::ScheduleDeadline(*this);
}

service_mojom::AdmissionPtr CoreServiceManager::MakeAdmission(
    std::string operation_id,
    service_mojom::AdmissionStatus status) const {
  auto admission = service_mojom::Admission::New();
  admission->operation_id = std::move(operation_id);
  admission->status = status;
  return admission;
}

bool CoreServiceManager::ValidateCommand(
    const service_mojom::CoreServiceCommand& command) const {
  if (!IsStructurallyValidCoreServiceCommand(command,
                                             service_mojom::kMaxCommandBytes)) {
    return false;
  }
  const auto validates_revision = [this, &command](const std::string& task_id) {
    const std::optional<uint64_t> revision = FindTaskRevision(task_id);
    return revision && command.operation->task_revision != 0u &&
           command.operation->service_generation == service_generation_ &&
           command.operation->task_revision == *revision;
  };
  if (command.start_task) {
    return command.operation->task_revision == 0u;
  }
  if (command.cancel_task) {
    const std::optional<TaskControlLookup> control = FindTaskControl(
        command.cancel_task->task_id, service_mojom::TaskControlKind::kStop);
    return control &&
           command.operation->service_generation ==
               control->service_generation &&
           command.operation->task_revision == control->task_revision;
  }
  const auto validates_control = [this, &command](
                                     const std::string& task_id,
                                     service_mojom::TaskControlKind kind) {
    const std::optional<TaskControlLookup> control =
        FindTaskControl(task_id, kind);
    return control &&
           command.operation->service_generation ==
               control->service_generation &&
           command.operation->task_revision == control->task_revision;
  };
  if (command.pause_task) {
    return validates_control(command.pause_task->task_id,
                             service_mojom::TaskControlKind::kPause);
  }
  if (command.resume_task) {
    return validates_control(command.resume_task->task_id,
                             service_mojom::TaskControlKind::kResume);
  }
  if (command.take_over) {
    return validates_control(command.take_over->task_id,
                             service_mojom::TaskControlKind::kTakeOver);
  }
  if (command.user_decision) {
    const std::optional<PendingApprovalLookup> approval = FindPendingApproval(
        command.user_decision->task_id, command.user_decision->action_id);
    return approval &&
           command.operation->service_generation ==
               approval->service_generation &&
           command.operation->task_revision == approval->task_revision &&
           command.user_decision->approval_digest == approval->proposal_digest;
  }
  if (command.supply_field_values) {
    const std::optional<uint64_t> revision =
        FindTaskRevision(command.supply_field_values->task_id);
    return revision &&
           command.operation->service_generation == service_generation_ &&
           command.operation->task_revision == *revision &&
           emitted_field_value_requests_.contains(
               command.supply_field_values->request_id);
  }
  if (command.permission_result) {
    const std::optional<PendingPermissionLookup> permission =
        FindPendingPermission(command.permission_result->request_id);
    const int64_t now = base::TimeTicks::Now().since_origin().InMilliseconds();
    return permission &&
           command.permission_result->task_id == permission->task_id &&
           command.permission_result->permission == permission->permission &&
           command.operation->service_generation ==
               permission->service_generation &&
           command.operation->task_revision == permission->task_revision &&
           (now < 0 ||
            static_cast<uint64_t>(now) < permission->deadline_monotonic_ms);
  }
  if (command.accept_task_artifact || command.export_task_artifact) {
    const std::string& task_id = command.accept_task_artifact
                                     ? command.accept_task_artifact->task_id
                                     : command.export_task_artifact->task_id;
    return validates_revision(task_id);
  }
  // These answers belong to an existing task, just like an artifact action.
  // Their specific pending prompt or handover is checked by the ordered core;
  // the browser must first bind them to the revision it actually published.
  if (command.complete_handover) {
    return validates_revision(command.complete_handover->task_id);
  }
  if (command.expire_handover) {
    return validates_revision(command.expire_handover->task_id);
  }
  if (command.supply_user_input) {
    return validates_revision(command.supply_user_input->task_id);
  }
  if (command.follow_up) {
    return validates_revision(command.follow_up->task_id);
  }
  return command.operation->task_revision == 0u;
}

}  // namespace taffy
