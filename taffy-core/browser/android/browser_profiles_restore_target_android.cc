// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/browser_profiles_restore_target_android.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_target_android_internal.h"
#include "taffy/browser/core_backup_protocol_validation.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using TargetError = ProfileBackupRestoreTargetError;

bool RequestNamesTarget(const mojom::BackupRestorePlanResult* plan,
                        const mojom::BackupRestoreStageAuthorization* authority,
                        const std::string& target_profile_id) {
  return plan && authority && plan->target && plan->binding &&
         plan->binding->target && authority->binding &&
         authority->binding->target && plan->snapshot_sha256.size() == 32u &&
         plan->target->kind ==
             mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         plan->target->profile_id == target_profile_id &&
         plan->binding->target->kind == plan->target->kind &&
         plan->binding->target->profile_id == target_profile_id &&
         authority->binding->target->kind == plan->target->kind &&
         authority->binding->target->profile_id == target_profile_id &&
         IsExactBackupRestoreBinding(plan->binding.get(),
                                     authority->binding.get());
}

}  // namespace

BrowserProfilesRestoreLifecycle::DormantTargetState::DormantTargetState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    ResolvedDormantBackupRestoreTarget resolved_target)
    : profile_manager_(profile_manager),
      local_state_(local_state),
      resolved_target_(std::move(resolved_target)),
      blocking_owner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

BrowserProfilesRestoreLifecycle::DormantTargetState::~DormantTargetState() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::Initialize(
    InitializationCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::Initialize)
      .WithArgs(resolved_target_.target_profile_path,
                resolved_target_.target_profile_id)
      .Then(std::move(callback));
}

const std::string&
BrowserProfilesRestoreLifecycle::DormantTargetState::target_profile_id() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return resolved_target_.target_profile_id;
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::Stage(
    mojom::BackupRestorePlanResultPtr plan,
    mojom::BackupRestoreStageAuthorizationPtr authorization,
    base::File plaintext_payload,
    StageCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (pending_stage_ || pending_commit_ || cleanup_callback_ ||
      close_for_recovery_callback_ || closed_for_recovery_) {
    std::move(callback).Run(base::unexpected(TargetError::kBusy));
    return;
  }
  if (abandon_requested_) {
    std::move(callback).Run(base::unexpected(TargetError::kUnavailable));
    return;
  }
  if (stage_authorization_consumed_) {
    std::move(callback).Run(base::unexpected(TargetError::kBusy));
    return;
  }
  if (!RequestNamesTarget(plan.get(), authorization.get(),
                          resolved_target_.target_profile_id)) {
    std::move(callback).Run(
        base::unexpected(TargetError::kInvalidAuthorization));
    return;
  }
  if (!plaintext_payload.IsValid()) {
    std::move(callback).Run(base::unexpected(TargetError::kPayloadMismatch));
    return;
  }

  pending_stage_ = std::make_unique<PendingStage>();
  pending_stage_->expected_record_count = plan->entries.size();
  std::ranges::copy(plan->snapshot_sha256,
                    pending_stage_->expected_snapshot_sha256.begin());
  pending_stage_->retained_plan = plan.Clone();
  pending_stage_->plan = std::move(plan);
  pending_stage_->authorization = std::move(authorization);
  pending_stage_->plaintext_payload = std::move(plaintext_payload);
  pending_stage_->callback = std::move(callback);
  VerifyCustody(base::BindOnce(&DormantTargetState::OnStagePreValidation,
                               weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::Abandon(
    CleanupCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (cleanup_callback_ || close_for_recovery_callback_ ||
      closed_for_recovery_ || pending_commit_ ||
      commit_authorization_consumed_) {
    std::move(callback).Run(false);
    return;
  }
  abandon_requested_ = true;
  cleanup_callback_ = std::move(callback);
  if (!pending_stage_) {
    BeginAbandon();
  }
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::CloseForRecovery(
    CleanupCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (pending_stage_ || pending_commit_ || cleanup_callback_ ||
      cleanup_in_flight_ || abandon_requested_ ||
      close_for_recovery_callback_ || closed_for_recovery_ ||
      !stage_authorization_consumed_ || !staged_plan_) {
    std::move(callback).Run(false);
    return;
  }
  // Withdraw every mutable entry point before posting destruction. The reply
  // is sequenced after StorageOwner's destructor, so a successful callback is
  // also the process-local exclusive-path lease handoff barrier.
  closed_for_recovery_ = true;
  close_for_recovery_callback_ = std::move(callback);
  blocking_owner_.AsyncCall(&BlockingOwner::CloseForRecovery)
      .WithArgs(!commit_storage_dispatched_)
      .Then(base::BindOnce(&DormantTargetState::OnClosedForRecovery,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    CloseForPrecommitCleanup(CleanupCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (pending_stage_ || pending_commit_ || cleanup_callback_ ||
      cleanup_in_flight_ || abandon_requested_ ||
      close_for_recovery_callback_ || closed_for_recovery_ ||
      stage_authorization_consumed_ || commit_authorization_consumed_) {
    std::move(callback).Run(false);
    return;
  }
  closed_for_recovery_ = true;
  close_for_recovery_callback_ = std::move(callback);
  blocking_owner_.AsyncCall(&BlockingOwner::CloseForRecovery)
      .WithArgs(false)
      .Then(base::BindOnce(&DormantTargetState::OnClosedForRecovery,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnClosedForRecovery(
    bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!close_for_recovery_callback_) {
    return;
  }
  CleanupCallback callback = std::move(close_for_recovery_callback_);
  std::move(callback).Run(closed);
}

bool BrowserProfilesRestoreLifecycle::DormantTargetState::HasExactLiveCustody()
    const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto resolved = ResolveDormantBackupRestoreTarget(
      profile_manager_, local_state_, resolved_target_.reservation_id);
  return resolved.has_value() && *resolved == resolved_target_;
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::VerifyCustody(
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (!HasExactLiveCustody()) {
    std::move(callback).Run(false);
    return;
  }
  VerifyDormantBackupRestoreTargetPreferences(
      profile_manager_, local_state_, resolved_target_.target_profile_path,
      std::move(callback));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnStagePreValidation(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_stage_) {
    return;
  }
  if (abandon_requested_ || !matches || !HasExactLiveCustody()) {
    FinishStage(base::unexpected(TargetError::kUnavailable));
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::Stage)
      .WithArgs(std::move(pending_stage_->plan),
                std::move(pending_stage_->authorization),
                std::move(pending_stage_->plaintext_payload),
                pending_stage_->expected_snapshot_sha256,
                pending_stage_->expected_record_count)
      .Then(base::BindOnce(&DormantTargetState::OnStageExecuted,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnStageExecuted(
    StageExecutionResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_stage_) {
    return;
  }
  stage_authorization_consumed_ |= result.authorization_consumed;
  pending_stage_->execution_result = std::move(result);
  VerifyCustody(base::BindOnce(&DormantTargetState::OnStagePostValidation,
                               weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnStagePostValidation(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_stage_ || !pending_stage_->execution_result) {
    return;
  }
  StageResult result =
      matches && HasExactLiveCustody()
          ? std::move(pending_stage_->execution_result->result)
          : StageResult(base::unexpected(TargetError::kUnavailable));
  FinishStage(std::move(result));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::FinishStage(
    StageResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_stage_) {
    return;
  }
  if (result.has_value()) {
    staged_plan_ = std::move(pending_stage_->retained_plan);
  }
  StageCallback callback = std::move(pending_stage_->callback);
  pending_stage_.reset();
  base::WeakPtr<DormantTargetState> weak_this = weak_factory_.GetWeakPtr();
  std::move(callback).Run(std::move(result));
  if (weak_this && weak_this->cleanup_callback_) {
    weak_this->BeginAbandon();
  }
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::BeginAbandon() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!cleanup_callback_ || pending_stage_ || pending_commit_ ||
      commit_authorization_consumed_ || cleanup_in_flight_) {
    return;
  }
  cleanup_in_flight_ = true;
  cleanup_storage_succeeded_ = false;
  VerifyCustody(base::BindOnce(&DormantTargetState::OnAbandonPreValidation,
                               weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnAbandonPreValidation(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!cleanup_callback_) {
    return;
  }
  if (!matches || !HasExactLiveCustody()) {
    FinishAbandon(false);
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::Abandon)
      .Then(base::BindOnce(&DormantTargetState::OnAbandonExecuted,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnAbandonExecuted(
    bool cleaned) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!cleanup_callback_) {
    return;
  }
  cleanup_storage_succeeded_ = cleaned;
  VerifyCustody(base::BindOnce(&DormantTargetState::OnAbandonPostValidation,
                               weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnAbandonPostValidation(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!cleanup_storage_succeeded_ || !matches || !HasExactLiveCustody()) {
    FinishAbandon(false);
    return;
  }
  // A successful Abandon callback is the coordinator's target-drain proof.
  // SequenceBound destruction alone is asynchronous, so close the typed
  // storage owner and its exclusive path lease behind an explicit reply
  // barrier before returning success.
  closed_for_recovery_ = true;
  blocking_owner_.AsyncCall(&BlockingOwner::CloseForRecovery)
      .WithArgs(false)
      .Then(base::BindOnce(&DormantTargetState::OnAbandonStorageClosed,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnAbandonStorageClosed(bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  FinishAbandon(closed);
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::FinishAbandon(
    bool cleaned) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!cleanup_callback_) {
    return;
  }
  CleanupCallback callback = std::move(cleanup_callback_);
  cleanup_in_flight_ = false;
  cleanup_storage_succeeded_ = false;
  if (cleaned) {
    staged_plan_.reset();
  }
  std::move(callback).Run(cleaned);
}

}  // namespace taffy
