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
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;

ProfileBackupError CommitError(ProfileBackupRestoreTargetError error) {
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

void ProfileBackupCoordinator::CommitStagedImportedRestore(
    std::string operation_id,
    RestoreCommitCallback callback) {
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
  auto& operation = *found->second;
  if (operation.phase != ImportOperation::Phase::kStaged ||
      operation.cancellation_requested ||
      operation.commit_authority_requested) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kBusy));
    return;
  }
  if (!manager_ ||
      manager_->availability() != CoreServiceAvailability::kReady ||
      !operation.workflow_interest || !operation.target || !operation.plan ||
      !operation.plan->binding || !operation.stage_authorization ||
      operation.target->target_profile_id() != operation.target_profile_id ||
      operation.plan->binding->owner_profile_id !=
          manager_->browser_profile_id() ||
      !IsValidBackupRestoreBinding(operation.plan->binding.get(),
                                   manager_->service_generation()) ||
      !IsExactBackupRestoreBinding(
          operation.plan->binding.get(),
          operation.stage_authorization->binding.get())) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  auto envelope = NewBackupOperation(manager_->service_generation(),
                                     "backup-restore-commit", operation_id);
  if (!envelope) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  operation.commit_authority_requested = true;
  operation.commit_operation = envelope.Clone();
  operation.commit_callback = std::move(callback);
  operation.phase = ImportOperation::Phase::kAuthorizingCommit;
  PrecommitCancellationCallback cancellation_callback =
      std::move(operation.precommit_cancellation_callback);
  manager_->backup_protocol().ReportBackupRestoreStageVerified(
      wire::BackupRestoreStageVerificationRequest::New(
          std::move(envelope), operation.stage_authorization.Clone(),
          operation.plan->snapshot_sha256,
          std::move(operation.staged_procedures)),
      base::BindOnce(&ProfileBackupCoordinator::DeliverRestoreCommitAuthority,
                     weak_factory_.GetWeakPtr(), manager_, operation_id));
  // The consumptive request has escaped. Even if the caller synchronously
  // withdraws after this notification, physical cleanup can no longer rely on
  // a precommit receipt. The pending authority reply is drained separately.
  if (cancellation_callback) {
    std::move(cancellation_callback)
        .Run(base::unexpected(ProfileBackupError::kBusy));
  }
}

void ProfileBackupCoordinator::DeliverRestoreCommitAuthority(
    base::WeakPtr<ProfileBackupCoordinator> coordinator,
    base::WeakPtr<CoreServiceManager> manager,
    std::string operation_id,
    wire::BackupRestoreCommitAuthorizationResultPtr result) {
  if (coordinator) {
    coordinator->OnRestoreCommitAuthorized(operation_id, std::move(result));
    return;
  }
  // The owner disappeared before it could dispatch physical work. A validated
  // late authority can only report that fact, never start a writer or enter
  // the precommit cancellation retry loop (which is no longer this phase).
  if (!manager || manager->availability() != CoreServiceAvailability::kReady ||
      !result ||
      result->status != wire::BackupRestoreProtocolStatus::kSucceeded ||
      !result->authorization || !result->authorization->binding ||
      result->authorization->binding->owner_profile_id !=
          manager->browser_profile_id() ||
      !IsValidBackupRestoreBinding(result->authorization->binding.get(),
                                   manager->service_generation())) {
    return;
  }
  auto operation = NewBackupOperation(manager->service_generation(),
                                      "backup-restore-unstarted", operation_id);
  if (!operation) {
    return;
  }
  manager->backup_protocol().ReportBackupRestoreCommitOutcome(
      wire::BackupRestoreCommitOutcomeReport::New(
          std::move(operation), std::move(result->authorization),
          wire::BackupRestoreCommitOutcome::kDefinitelyNotCommitted),
      base::BindOnce([](wire::BackupRestoreProtocolResultPtr) {}));
}

void ProfileBackupCoordinator::OnRestoreCommitAuthorized(
    const std::string& operation_id,
    wire::BackupRestoreCommitAuthorizationResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kAuthorizingCommit) {
    return;
  }
  auto& operation = *found->second;
  if (!manager_ ||
      manager_->availability() != CoreServiceAvailability::kReady ||
      !operation.commit_operation || !operation.plan ||
      !operation.plan->binding || !result ||
      !IsValidBackupRestoreCommitAuthorizationResult(
          *operation.commit_operation, *operation.plan->binding, *result) ||
      result->status != wire::BackupRestoreProtocolStatus::kSucceeded ||
      !result->authorization) {
    operation.commit_result =
        base::unexpected(ProfileBackupRestoreTargetError::kUnavailable);
    FinishRestoreCommit(operation_id, false);
    return;
  }
  operation.commit_authorization = std::move(result->authorization);
  operation.phase = ImportOperation::Phase::kCommittingRestore;
  if (operation.cancellation_requested || !operation.target ||
      !IsLiveBackupRestoreCommitAuthorization(
          operation.commit_authorization.get(), manager_->service_generation(),
          BackupPlanningNowMonotonicMillis())) {
    // No physical owner was called. The outcome report may outlive the
    // decision deadline and cannot execute the now-withdrawn authorization.
    OnRestoreCommitted(
        operation_id,
        base::unexpected(ProfileBackupRestoreTargetError::kUnavailable));
    return;
  }
  operation.commit_dispatched = true;
  operation.target->Commit(
      operation.plan.Clone(), operation.commit_authorization.Clone(),
      base::BindOnce(&ProfileBackupCoordinator::OnRestoreCommitted,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnRestoreCommitted(
    const std::string& operation_id,
    ProfileBackupRestoreTarget::CommitResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kCommittingRestore) {
    return;
  }
  auto& operation = *found->second;
  // A definitive value without its durable witness cannot become success at
  // the UI or the portable protocol, even if a buggy adapter returns it.
  if (result && (!result->journal_durable ||
                 !core_service::wire::BackupRestoreCommitOutcomeFromWire(
                     static_cast<uint32_t>(result->outcome)))) {
    result->outcome = wire::BackupRestoreCommitOutcome::kOutcomeUnknown;
  }
  operation.commit_result = std::move(result);
  if (!manager_ ||
      manager_->availability() != CoreServiceAvailability::kReady ||
      !operation.commit_authorization ||
      !IsValidBackupRestoreBinding(
          operation.commit_authorization->binding.get(),
          manager_->service_generation())) {
    FinishRestoreCommit(operation_id, false);
    return;
  }
  auto envelope = NewBackupOperation(manager_->service_generation(),
                                     "backup-restore-outcome", operation_id);
  if (!envelope) {
    FinishRestoreCommit(operation_id, false);
    return;
  }
  const auto outcome =
      *operation.commit_result
          ? operation.commit_result->value().outcome
          : wire::BackupRestoreCommitOutcome::kDefinitelyNotCommitted;
  operation.phase = ImportOperation::Phase::kReportingCommit;
  manager_->backup_protocol().ReportBackupRestoreCommitOutcome(
      wire::BackupRestoreCommitOutcomeReport::New(
          std::move(envelope), operation.commit_authorization.Clone(), outcome),
      base::BindOnce(&ProfileBackupCoordinator::OnRestoreCommitReported,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnRestoreCommitReported(
    const std::string& operation_id,
    wire::BackupRestoreProtocolResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kReportingCommit) {
    return;
  }
  FinishRestoreCommit(
      operation_id,
      result &&
          result->status == wire::BackupRestoreProtocolStatus::kSucceeded);
}

void ProfileBackupCoordinator::FinishRestoreCommit(
    const std::string& operation_id,
    bool source_acknowledged) {
  auto found = imports_.find(operation_id);
  if (found == imports_.end() || !found->second->commit_result) {
    return;
  }
  auto& operation = *found->second;
  const auto& physical = *operation.commit_result;
  operation.phase =
      physical && physical->journal_durable && source_acknowledged &&
              physical->outcome == wire::BackupRestoreCommitOutcome::kCommitted
          ? ImportOperation::Phase::kHiddenReview
          : ImportOperation::Phase::kCommitRecoveryRequired;
  if (operation.phase == ImportOperation::Phase::kCommitRecoveryRequired) {
    // No recoverable physical work needs this dead/ambiguous portable session.
    // Keep the target under browser reservation, not an endless Core interest.
    operation.workflow_interest.reset();
  }
  auto callback = std::move(operation.commit_callback);
  if (!callback) {
    return;
  }
  if (operation.cancellation_requested && !operation.commit_dispatched) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kCancelled));
  } else if (!physical) {
    std::move(callback).Run(base::unexpected(CommitError(physical.error())));
  } else {
    std::move(callback).Run(ProfileBackupRestoreCommitReceipt{
        .physical = *physical, .source_acknowledged = source_acknowledged});
  }
}

}  // namespace taffy
