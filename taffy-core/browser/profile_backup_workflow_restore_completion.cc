// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_internal.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

ProfileBackupWorkflow::RestoreStageStatus ProjectStageError(
    ProfileBackupError error) {
  switch (error) {
    case ProfileBackupError::kInvalidArgument:
    case ProfileBackupError::kSnapshotMismatch:
    case ProfileBackupError::kPlanRefused:
      return ProfileBackupWorkflow::RestoreStageStatus::kRefused;
    case ProfileBackupError::kBusy:
    case ProfileBackupError::kCoreUnavailable:
    case ProfileBackupError::kStorageUnavailable:
    case ProfileBackupError::kIoFailure:
    case ProfileBackupError::kCancelled:
      return ProfileBackupWorkflow::RestoreStageStatus::kUnavailable;
  }
  return ProfileBackupWorkflow::RestoreStageStatus::kUnavailable;
}

ProfileBackupWorkflow::RestoreCommitStatus ProjectCommit(
    const ProfileBackupCoordinator::RestoreCommitResult& result) {
  if (!result) {
    switch (result.error()) {
      case ProfileBackupError::kInvalidArgument:
      case ProfileBackupError::kSnapshotMismatch:
      case ProfileBackupError::kPlanRefused:
        return ProfileBackupWorkflow::RestoreCommitStatus::kRefused;
      case ProfileBackupError::kBusy:
      case ProfileBackupError::kCoreUnavailable:
      case ProfileBackupError::kStorageUnavailable:
      case ProfileBackupError::kIoFailure:
      case ProfileBackupError::kCancelled:
        return ProfileBackupWorkflow::RestoreCommitStatus::kUnavailable;
    }
    return ProfileBackupWorkflow::RestoreCommitStatus::kUnavailable;
  }
  if (!result->physical.journal_durable) {
    return ProfileBackupWorkflow::RestoreCommitStatus::kRecoveryRequired;
  }
  switch (result->physical.outcome) {
    case mojom::BackupRestoreCommitOutcome::kCommitted:
      return result->source_acknowledged
                 ? ProfileBackupWorkflow::RestoreCommitStatus::kHiddenCandidate
                 : ProfileBackupWorkflow::RestoreCommitStatus::
                       kRecoveryRequired;
    case mojom::BackupRestoreCommitOutcome::kDefinitelyNotCommitted:
      return ProfileBackupWorkflow::RestoreCommitStatus::
          kDefinitelyNotCommitted;
    case mojom::BackupRestoreCommitOutcome::kOutcomeUnknown:
      return ProfileBackupWorkflow::RestoreCommitStatus::kRecoveryRequired;
  }
  return ProfileBackupWorkflow::RestoreCommitStatus::kRecoveryRequired;
}

}  // namespace

ProfileBackupWorkflow::Operation*
ProfileBackupWorkflow::FindRestoreReviewLocked(
    WindowToken window,
    RestoreReviewToken review_token) {
  // Every caller already holds state_lock_ across the search and the use of
  // the operation it returns, which is what the Locked suffix names; base::Lock
  // is not reentrant, so this must assert the hold rather than take it. The
  // assertion is what tells the thread-safety analysis the guarded map may be
  // read here, and it fails a DCHECK build if a later caller forgets.
  state_lock_.AssertAcquired();
  for (auto& entry : operations_) {
    auto& operation = entry.second;
    if (operation->owner == window &&
        operation->restore_review_token == review_token) {
      return operation.get();
    }
  }
  return nullptr;
}

const ProfileBackupWorkflow::Operation*
ProfileBackupWorkflow::FindRestoreReviewLocked(
    WindowToken window,
    RestoreReviewToken review_token) const {
  // See the non-const overload: the lock is the caller's to hold.
  state_lock_.AssertAcquired();
  for (const auto& entry : operations_) {
    const auto& operation = entry.second;
    if (operation->owner == window &&
        operation->restore_review_token == review_token) {
      return operation.get();
    }
  }
  return nullptr;
}

bool ProfileBackupWorkflow::ConfirmAndStageImportedRestore(
    WindowToken window,
    RestoreReviewToken review_token,
    RestoreStageCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !coordinator_ || !IsActiveWindow(window) ||
      review_token == 0u) {
    return false;
  }
  std::string operation_id;
  std::vector<uint8_t> confirmation_digest;
  {
    base::AutoLock guard(state_lock_);
    Operation* operation = FindRestoreReviewLocked(window, review_token);
    if (!operation || operation->phase != Operation::Phase::kRestorePreview ||
        !operation->restore_can_stage ||
        operation->confirmation_digest.empty()) {
      return false;
    }
    for (const auto& [candidate_id, candidate] : operations_) {
      if (candidate.get() == operation) {
        operation_id = candidate_id;
        break;
      }
    }
    if (operation_id.empty()) {
      return false;
    }
    confirmation_digest = operation->confirmation_digest;
    operation->restore_stage_callback = std::move(callback);
    operation->phase = Operation::Phase::kRestoreStaging;
  }
  coordinator_->ConfirmAndStageImportedRestore(
      operation_id, std::move(confirmation_digest),
      base::BindOnce(&ProfileBackupWorkflow::OnImportedRestoreStaged,
                     weak_factory_.GetWeakPtr(), operation_id));
  return true;
}

void ProfileBackupWorkflow::OnImportedRestoreStaged(
    const std::string& operation_id,
    ProfileBackupCoordinator::RestoreStageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestoreStageCallback callback;
  RestoreStageStatus status = RestoreStageStatus::kUnavailable;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        found->second->phase != Operation::Phase::kRestoreStaging) {
      return;
    }
    Operation& operation = *found->second;
    callback = std::move(operation.restore_stage_callback);
    if (result) {
      operation.phase = Operation::Phase::kRestoreStaged;
      status = RestoreStageStatus::kStaged;
    } else {
      operation.phase = Operation::Phase::kPrecommitCleanupRequired;
      status = ProjectStageError(result.error());
    }
  }
  if (callback) {
    std::move(callback).Run(status);
  }
}

bool ProfileBackupWorkflow::CommitStagedImportedRestore(
    WindowToken window,
    RestoreReviewToken review_token,
    RestoreCommitCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !coordinator_ || !IsActiveWindow(window) ||
      review_token == 0u) {
    return false;
  }
  std::string operation_id;
  {
    base::AutoLock guard(state_lock_);
    Operation* operation = FindRestoreReviewLocked(window, review_token);
    if (!operation || operation->phase != Operation::Phase::kRestoreStaged) {
      return false;
    }
    for (const auto& [candidate_id, candidate] : operations_) {
      if (candidate.get() == operation) {
        operation_id = candidate_id;
        break;
      }
    }
    if (operation_id.empty()) {
      return false;
    }
    operation->restore_commit_callback = std::move(callback);
    operation->phase = Operation::Phase::kRestoreCommitting;
  }
  coordinator_->CommitStagedImportedRestore(
      operation_id,
      base::BindOnce(&ProfileBackupWorkflow::OnImportedRestoreCommitted,
                     weak_factory_.GetWeakPtr(), operation_id));
  return true;
}

void ProfileBackupWorkflow::OnImportedRestoreCommitted(
    const std::string& operation_id,
    ProfileBackupCoordinator::RestoreCommitResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestoreCommitStatus projected_status = ProjectCommit(result);
  if (!result) {
    // Preserve the internal Android drain continuation before triggering
    // cancellation. A fake target (and some no-I/O paths) may synchronously
    // deliver the one-use cleanup receipt and retire this workflow operation.
    RestoreCommitCallback precommit_callback;
    {
      base::AutoLock guard(state_lock_);
      auto found = operations_.find(operation_id);
      if (found == operations_.end() ||
          found->second->phase != Operation::Phase::kRestoreCommitting) {
        return;
      }
      precommit_callback = std::move(found->second->restore_commit_callback);
      // Publish this phase before triggering coordinator cancellation. Its
      // one-use cleanup receipt may synchronously finish Android physical
      // cleanup and re-enter AbandonOperation. That withdrawal must erase the
      // workflow owner instead of mistaking it for a consumed commit drain.
      found->second->phase = Operation::Phase::kPrecommitCleanupRequired;
    }
    base::WeakPtr<ProfileBackupWorkflow> weak_this = weak_factory_.GetWeakPtr();
    const bool cancellation_started =
        coordinator_->CancelBeforeCommit(operation_id);
    if (!weak_this) {
      return;
    }
    if (cancellation_started) {
      {
        base::AutoLock guard(weak_this->state_lock_);
        auto found = weak_this->operations_.find(operation_id);
        if (found != weak_this->operations_.end()) {
          found->second->phase = Operation::Phase::kPrecommitCleanupRequired;
        }
      }
      if (precommit_callback) {
        std::move(precommit_callback)
            .Run(RestoreCommitStatus::kPrecommitCleanupRequired);
      }
      return;
    }
    {
      base::AutoLock guard(weak_this->state_lock_);
      auto found = weak_this->operations_.find(operation_id);
      if (found == weak_this->operations_.end() ||
          found->second->phase != Operation::Phase::kPrecommitCleanupRequired) {
        return;
      }
      found->second->phase = Operation::Phase::kRestoreCommitting;
      found->second->restore_commit_callback = std::move(precommit_callback);
    }
    // CancelBeforeCommit refusing after a commit-authority request means the
    // consumptive operation may have escaped. The writer must close for
    // read-only recovery, and neither workflow nor UI may spell that as a
    // retryable precommit Unavailable result.
    projected_status = RestoreCommitStatus::kRecoveryRequired;
  }
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        found->second->phase != Operation::Phase::kRestoreCommitting) {
      return;
    }
    found->second->phase = Operation::Phase::kRestoreClosing;
  }
  coordinator_->CloseRestoreForRecovery(
      operation_id,
      base::BindOnce(&ProfileBackupWorkflow::OnImportedRestoreClosed,
                     weak_factory_.GetWeakPtr(), operation_id,
                     projected_status));
}

void ProfileBackupWorkflow::OnImportedRestoreClosed(
    const std::string& operation_id,
    RestoreCommitStatus projected_status,
    ProfileBackupCoordinator::RestoreStageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestoreCommitCallback callback;
  RestoreCommitStatus status = projected_status;
  bool detached = false;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        found->second->phase != Operation::Phase::kRestoreClosing) {
      return;
    }
    Operation& operation = *found->second;
    callback = std::move(operation.restore_commit_callback);
    detached = operation.restore_window_detached;
    if (!result) {
      // Once Commit returned, inability to prove the writer handoff is a
      // recovery state even when the earlier callback looked definitive.
      status = RestoreCommitStatus::kRecoveryRequired;
    }
    switch (status) {
      case RestoreCommitStatus::kHiddenCandidate:
        operation.restore_discard_only_resolution = false;
        operation.phase = Operation::Phase::kHiddenReview;
        break;
      case RestoreCommitStatus::kDefinitelyNotCommitted:
        operation.restore_discard_only_resolution = true;
        operation.phase = Operation::Phase::kRestoreDefinitelyNotCommitted;
        break;
      case RestoreCommitStatus::kRecoveryRequired:
        operation.phase = Operation::Phase::kRestoreRecoveryRequired;
        break;
      case RestoreCommitStatus::kRefused:
      case RestoreCommitStatus::kUnavailable:
      case RestoreCommitStatus::kPrecommitCleanupRequired:
        operation.phase = Operation::Phase::kPrecommitCleanupRequired;
        break;
    }
    if (detached) {
      operations_.erase(found);
    }
  }
  if (callback) {
    std::move(callback).Run(status);
  }
}

std::optional<std::string>
ProfileBackupWorkflow::BeginImportedRestoreResolution(
    WindowToken window,
    RestoreReviewToken review_token,
    mojom::BackupRestoreResolutionChoice choice,
    RestoreResolutionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !IsActiveWindow(window) || review_token == 0u ||
      (choice != mojom::BackupRestoreResolutionChoice::kAcceptCandidate &&
       choice != mojom::BackupRestoreResolutionChoice::kDiscardCandidate)) {
    return std::nullopt;
  }
  base::AutoLock guard(state_lock_);
  Operation* operation = FindRestoreReviewLocked(window, review_token);
  if (!operation || operation->reservation_id.empty() ||
      (operation->phase != Operation::Phase::kHiddenReview &&
       operation->phase != Operation::Phase::kRestoreDefinitelyNotCommitted) ||
      (operation->restore_discard_only_resolution &&
       choice != mojom::BackupRestoreResolutionChoice::kDiscardCandidate)) {
    return std::nullopt;
  }
  operation->phase = Operation::Phase::kRestoreResolving;
  operation->restore_resolution_callback = std::move(callback);
  return operation->reservation_id;
}

void ProfileBackupWorkflow::CompleteImportedRestoreResolution(
    WindowToken window,
    RestoreReviewToken review_token,
    RestoreResolutionStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestoreResolutionCallback callback;
  std::string terminal_operation;
  {
    base::AutoLock guard(state_lock_);
    Operation* operation = FindRestoreReviewLocked(window, review_token);
    if (!operation || operation->phase != Operation::Phase::kRestoreResolving) {
      return;
    }
    callback = std::move(operation->restore_resolution_callback);
    if (status == RestoreResolutionStatus::kPublished ||
        status == RestoreResolutionStatus::kVerifiedDeleted) {
      for (const auto& [operation_id, candidate] : operations_) {
        if (candidate.get() == operation) {
          terminal_operation = operation_id;
          break;
        }
      }
    } else if (status == RestoreResolutionStatus::kRecoveryRequired) {
      operation->phase = Operation::Phase::kRestoreRecoveryRequired;
    } else {
      operation->phase = operation->restore_discard_only_resolution
                             ? Operation::Phase::kRestoreDefinitelyNotCommitted
                             : Operation::Phase::kHiddenReview;
    }
    if (!terminal_operation.empty()) {
      operations_.erase(terminal_operation);
    }
  }
  if (!terminal_operation.empty() && coordinator_) {
    // Physical resolution and reservation retirement have already completed.
    // Release the inert source-side import and encrypted archive stage before
    // publishing terminal UI success; otherwise every completed restore
    // permanently consumes one of the coordinator's bounded operation slots.
    coordinator_->Cancel(terminal_operation);
  }
  if (callback) {
    std::move(callback).Run(status);
  }
}

}  // namespace taffy
