// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

ProfileBackupError StageError(ProfileBackupRestoreTargetError error) {
  switch (error) {
    case ProfileBackupRestoreTargetError::kInvalidAuthorization:
      return ProfileBackupError::kPlanRefused;
    case ProfileBackupRestoreTargetError::kUnavailable:
      return ProfileBackupError::kCoreUnavailable;
    case ProfileBackupRestoreTargetError::kPayloadMismatch:
      return ProfileBackupError::kSnapshotMismatch;
    case ProfileBackupRestoreTargetError::kStorageUnavailable:
      return ProfileBackupError::kStorageUnavailable;
    case ProfileBackupRestoreTargetError::kBusy:
      return ProfileBackupError::kBusy;
  }
  return ProfileBackupError::kPlanRefused;
}

}  // namespace

void ProfileBackupCoordinator::ConfirmAndStageImportedRestore(
    std::string operation_id,
    std::vector<uint8_t> confirmed_digest,
    RestoreStageCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    return;
  }
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kInvalidArgument));
    return;
  }
  ImportOperation& operation = *found->second;
  if (operation.phase != ImportOperation::Phase::kReady ||
      operation.cancellation_requested) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kBusy));
    return;
  }
  if (!manager_ ||
      manager_->availability() != CoreServiceAvailability::kReady ||
      !operation.workflow_interest || !operation.target || !operation.plan ||
      !operation.plan->binding || !operation.imported ||
      !operation.imported->verified.plaintext_payload.IsValid() ||
      operation.target->target_profile_id() != operation.target_profile_id ||
      operation.plan->binding->owner_profile_id !=
          manager_->browser_profile_id() ||
      !IsValidBackupRestoreBinding(operation.plan->binding.get(),
                                   manager_->service_generation())) {
    operation.stage_callback = std::move(callback);
    operation.cancellation_error = ProfileBackupError::kCoreUnavailable;
    Cancel(operation_id);
    return;
  }
  if (confirmed_digest != operation.plan->confirmation_sha256) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kPlanRefused));
    return;
  }
  auto envelope = NewBackupOperation(manager_->service_generation(),
                                     "backup-restore-confirm", operation_id);
  if (!envelope) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  operation.confirmation_operation = envelope.Clone();
  operation.phase = ImportOperation::Phase::kConfirmingRestore;
  operation.stage_callback = std::move(callback);
  auto request = core_mojom::BackupRestorePlanConfirmationRequest::New(
      std::move(envelope), operation.plan->binding.Clone(),
      std::move(confirmed_digest));
  manager_->backup_protocol().ConfirmBackupRestorePlan(
      std::move(request),
      base::BindOnce(&ProfileBackupCoordinator::OnRestoreConfirmed,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnRestoreConfirmed(
    const std::string& operation_id,
    core_mojom::BackupRestoreStageAuthorizationResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kConfirmingRestore ||
      found->second->cancellation_requested) {
    return;
  }
  ImportOperation& operation = *found->second;
  if (!manager_ ||
      manager_->availability() != CoreServiceAvailability::kReady || !result ||
      result->status != core_mojom::BackupRestoreProtocolStatus::kSucceeded ||
      !result->authorization || !operation.confirmation_operation ||
      !IsLiveBackupOperation(operation.confirmation_operation.get(),
                             manager_->service_generation(),
                             BackupPlanningNowMonotonicMillis()) ||
      !IsValidBackupRestoreStageAuthorizationResult(
          *operation.confirmation_operation, *operation.plan->binding,
          *result) ||
      !operation.target ||
      operation.target->target_profile_id() != operation.target_profile_id) {
    operation.cancellation_error = ProfileBackupError::kPlanRefused;
    Cancel(operation_id);
    return;
  }
  operation.stage_authorization = std::move(result->authorization);
  operation.phase = ImportOperation::Phase::kStagingRestore;
  operation.target->Stage(
      operation.plan.Clone(), operation.stage_authorization.Clone(),
      std::move(operation.imported->verified.plaintext_payload),
      base::BindOnce(&ProfileBackupCoordinator::OnRestoreStaged,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnRestoreStaged(
    const std::string& operation_id,
    ProfileBackupRestoreTarget::StageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kStagingRestore ||
      found->second->cancellation_requested) {
    return;
  }
  ImportOperation& operation = *found->second;
  if (!result) {
    operation.cancellation_error = StageError(result.error());
    Cancel(operation_id);
    return;
  }
  if (!manager_ ||
      manager_->availability() != CoreServiceAvailability::kReady ||
      !operation.workflow_interest || !operation.stage_authorization ||
      !operation.plan || !operation.plan->binding || !operation.target ||
      operation.target->target_profile_id() != operation.target_profile_id ||
      !IsValidBackupRestoreBinding(operation.plan->binding.get(),
                                   manager_->service_generation())) {
    operation.cancellation_error = ProfileBackupError::kCoreUnavailable;
    Cancel(operation_id);
    return;
  }
  // Keep staging independently reversible. Only the explicit commit entry
  // point consumes commit authority and hands off durable recovery custody.
  operation.staged_procedures = std::move(result->procedures);
  operation.phase = ImportOperation::Phase::kStaged;
  auto callback = std::move(operation.stage_callback);
  if (callback) {
    std::move(callback).Run(base::ok());
  }
}

}  // namespace taffy
