// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/backup_workflow_android_notifications.h"
#include "taffy/browser/android/backup_workflow_restore_android.h"
#include "taffy/browser/android/backup_workflow_restore_android_internal.h"
#include "taffy/browser/android/backup_workflow_restore_projection.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

void DeliverStage(jni_zero::ScopedJavaGlobalRef<jobject> caller,
                  ProfileBackupWorkflow::RestoreReviewToken token,
                  ProfileBackupWorkflow::RestoreStageStatus status) {
  if (caller) {
    backup_workflow_notifications::RestoreStaged(
        caller, static_cast<int64_t>(token), static_cast<int32_t>(status));
  }
}

void DeliverCommit(jni_zero::ScopedJavaGlobalRef<jobject> caller,
                   ProfileBackupWorkflow::RestoreReviewToken token,
                   ProfileBackupWorkflow::RestoreCommitStatus status) {
  if (caller) {
    const auto wire_status =
        status == ProfileBackupWorkflow::RestoreCommitStatus::
                      kPrecommitCleanupRequired
            ? ProfileBackupWorkflow::RestoreCommitStatus::kUnavailable
            : status;
    backup_workflow_notifications::RestoreCommitted(
        caller, static_cast<int64_t>(token),
        static_cast<int32_t>(wire_status));
  }
}

void DeliverResolution(jni_zero::ScopedJavaGlobalRef<jobject> caller,
                       ProfileBackupWorkflow::RestoreReviewToken token,
                       ProfileBackupWorkflow::RestoreResolutionStatus status) {
  if (caller) {
    backup_workflow_notifications::RestoreResolved(
        caller, static_cast<int64_t>(token), static_cast<int32_t>(status));
  }
}

}  // namespace

BackupWorkflowRestoreAndroid::Operation*
BackupWorkflowRestoreAndroid::FindByReview(
    ProfileBackupWorkflow::WindowToken window,
    ProfileBackupWorkflow::RestoreReviewToken token) {
  for (auto& [operation_id, operation] : operations_) {
    if (operation->owner == window && operation->review_token == token) {
      return operation.get();
    }
  }
  return nullptr;
}

bool BackupWorkflowRestoreAndroid::ConfirmAndStage(
    const jni_zero::JavaRef<jobject>& caller,
    ProfileBackupWorkflow::WindowToken window,
    ProfileBackupWorkflow::RestoreReviewToken review_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Operation* operation = FindByReview(window, review_token);
  if (!caller || !operation || operation->phase != Operation::Phase::kPreview ||
      operation->stage_caller) {
    return false;
  }
  operation->stage_caller.Reset(caller);
  operation->phase = Operation::Phase::kStaging;
  if (workflow_->ConfirmAndStageImportedRestore(
          window, review_token,
          base::BindOnce(&BackupWorkflowRestoreAndroid::OnWorkflowStaged,
                         weak_factory_.GetWeakPtr(),
                         operation->operation_id))) {
    return true;
  }
  operation->stage_caller.Reset();
  operation->phase = Operation::Phase::kPreview;
  return false;
}

void BackupWorkflowRestoreAndroid::OnWorkflowStaged(
    const std::string& operation_id,
    ProfileBackupWorkflow::RestoreStageStatus status) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  Operation& operation = *found->second;
  auto caller = std::move(operation.stage_caller);
  operation.phase = status == ProfileBackupWorkflow::RestoreStageStatus::kStaged
                        ? Operation::Phase::kStaged
                        : Operation::Phase::kCleaning;
  DeliverStage(std::move(caller), operation.review_token, status);
}

bool BackupWorkflowRestoreAndroid::Commit(
    const jni_zero::JavaRef<jobject>& caller,
    ProfileBackupWorkflow::WindowToken window,
    ProfileBackupWorkflow::RestoreReviewToken review_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Operation* operation = FindByReview(window, review_token);
  if (!caller || !operation || operation->phase != Operation::Phase::kStaged ||
      operation->commit_caller) {
    return false;
  }
  operation->commit_caller.Reset(caller);
  operation->phase = Operation::Phase::kCommitting;
  operation->commit_completion_pending = true;
  if (workflow_->CommitStagedImportedRestore(
          window, review_token,
          base::BindOnce(&BackupWorkflowRestoreAndroid::OnWorkflowCommitted,
                         weak_factory_.GetWeakPtr(),
                         operation->operation_id))) {
    return true;
  }
  operation->commit_completion_pending = false;
  operation->commit_caller.Reset();
  operation->phase = Operation::Phase::kStaged;
  return false;
}

void BackupWorkflowRestoreAndroid::OnWorkflowCommitted(
    const std::string& operation_id,
    ProfileBackupWorkflow::RestoreCommitStatus status) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  Operation& operation = *found->second;
  auto caller = std::move(operation.commit_caller);
  const auto token = operation.review_token;
  const bool detached = operation.detached;
  operation.commit_completion_pending = false;
  if (status ==
      ProfileBackupWorkflow::RestoreCommitStatus::kPrecommitCleanupRequired) {
    if (operation.phase != Operation::Phase::kRecoveryRequired) {
      operation.phase = Operation::Phase::kCleaning;
    }
    if (backup_workflow_restore_internal::CanReleasePrecommitOperation(
            operation.precommit_cleanup_settled,
            operation.commit_completion_pending)) {
      operations_.erase(found);
    }
    DeliverCommit(std::move(caller), token, status);
    return;
  }
  operation.cleanup_handle.reset();
  operation.creation_lifecycle.reset();
  if (status == ProfileBackupWorkflow::RestoreCommitStatus::kHiddenCandidate) {
    operation.phase = Operation::Phase::kHiddenReview;
    operation.discard_only_resolution = false;
  } else if (status == ProfileBackupWorkflow::RestoreCommitStatus::
                           kDefinitelyNotCommitted) {
    // Portable history proves the commit did not happen, but the exact
    // reserved target still needs a fresh, explicit Discard authorization.
    operation.phase = Operation::Phase::kCleanupReview;
    operation.discard_only_resolution = true;
  } else {
    operation.phase = Operation::Phase::kRecoveryRequired;
  }
  if (detached) {
    operations_.erase(found);
  }
  DeliverCommit(std::move(caller), token, status);
}

bool BackupWorkflowRestoreAndroid::Resolve(
    const jni_zero::JavaRef<jobject>& caller,
    ProfileBackupWorkflow::WindowToken window,
    ProfileBackupWorkflow::RestoreReviewToken review_token,
    mojom::BackupRestoreResolutionChoice choice) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Operation* operation = FindByReview(window, review_token);
  if (!caller || !operation ||
      (operation->phase != Operation::Phase::kHiddenReview &&
       operation->phase != Operation::Phase::kCleanupReview) ||
      (operation->discard_only_resolution &&
       choice != mojom::BackupRestoreResolutionChoice::kDiscardCandidate) ||
      operation->resolution_caller || operation->reservation_id.empty()) {
    return false;
  }
  auto reservation = workflow_->BeginImportedRestoreResolution(
      window, review_token, choice,
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnWorkflowResolution,
                     weak_factory_.GetWeakPtr(), operation->operation_id));
  if (!reservation || *reservation != operation->reservation_id) {
    return false;
  }
  operation->resolution_caller.Reset(caller);
  operation->phase = Operation::Phase::kResolving;
  operation->resolution_lifecycle =
      std::make_unique<BrowserProfilesRestoreLifecycle>(profile_manager_,
                                                        local_state_);
  operation->resolution_lifecycle->ResolveBackupRestoreCandidate(
      *reservation, choice,
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnCandidateResolved,
                     weak_factory_.GetWeakPtr(), review_token));
  return true;
}

void BackupWorkflowRestoreAndroid::OnCandidateResolved(
    ProfileBackupWorkflow::RestoreReviewToken review_token,
    BrowserProfilesRestoreLifecycle::CandidateResolutionResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.end();
  for (auto iterator = operations_.begin(); iterator != operations_.end();
       ++iterator) {
    auto& candidate = iterator->second;
    if (candidate->review_token == review_token &&
        candidate->phase == Operation::Phase::kResolving) {
      found = iterator;
      break;
    }
  }
  if (found == operations_.end()) {
    return;
  }
  Operation* const operation = found->second.get();
  operation->resolution_lifecycle.reset();
  if (operation->detached) {
    // The physical lifecycle has already durably settled or quarantined the
    // candidate. Window withdrawal grants no publication callback or replay;
    // the durable registry is the remaining recovery surface.
    operations_.erase(found);
    return;
  }
  workflow_->CompleteImportedRestoreResolution(
      operation->owner, review_token,
      backup_workflow_restore_internal::ProjectCandidateResolution(result));
}

void BackupWorkflowRestoreAndroid::OnWorkflowResolution(
    const std::string& operation_id,
    ProfileBackupWorkflow::RestoreResolutionStatus status) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  const auto token = found->second->review_token;
  auto caller = std::move(found->second->resolution_caller);
  if (status == ProfileBackupWorkflow::RestoreResolutionStatus::kPublished ||
      status ==
          ProfileBackupWorkflow::RestoreResolutionStatus::kVerifiedDeleted) {
    operations_.erase(found);
  } else if (backup_workflow_restore_internal::ResolutionRetainsHiddenCandidate(
                 status)) {
    // These statuses are emitted only from durable non-completion or from a
    // gate proven to precede consumptive resolution authorization. Preserve
    // the exact candidate for a later explicit fresh-Core decision.
    found->second->phase = found->second->discard_only_resolution
                               ? Operation::Phase::kCleanupReview
                               : Operation::Phase::kHiddenReview;
  } else {
    found->second->phase = Operation::Phase::kRecoveryRequired;
  }
  DeliverResolution(std::move(caller), token, status);
}

void BackupWorkflowRestoreAndroid::OnPrecommitCancellation(
    const std::string& operation_id,
    ProfileBackupCoordinator::PrecommitCancellationResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  Operation& operation = *found->second;
  if (!result) {
    if (result.error() != ProfileBackupError::kBusy) {
      operation.phase = Operation::Phase::kRecoveryRequired;
    }
    return;
  }
  if (!operation.creation_lifecycle || !operation.cleanup_handle) {
    operation.phase = Operation::Phase::kRecoveryRequired;
    return;
  }
  operation.phase = Operation::Phase::kCleaning;
  PrecommitBackupRestoreCleanupHandle cleanup =
      std::move(*operation.cleanup_handle);
  operation.cleanup_handle.reset();
  operation.creation_lifecycle->CleanupNewPrecommitReservation(
      std::move(cleanup), std::move(*result),
      base::BindOnce(&BackupWorkflowRestoreAndroid::OnPrecommitCleanup,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void BackupWorkflowRestoreAndroid::OnPrecommitCleanup(
    const std::string& operation_id,
    BrowserProfilesRestoreLifecycle::PrecommitCleanupResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end()) {
    return;
  }
  if (!result) {
    found->second->phase = Operation::Phase::kRecoveryRequired;
  }
  found->second->precommit_cleanup_settled = true;
  const ProfileBackupWorkflow::WindowToken owner = found->second->owner;
  // The coordinator operation has already yielded its one-use cancellation
  // receipt and the lifecycle has settled exact physical cleanup. Retire the
  // workflow's presentation bookkeeping too; this is not a second cleanup or
  // a way to infer success from an unavailable UI result.
  workflow_->AbandonOperation(owner, operation_id);
  if (backup_workflow_restore_internal::CanReleasePrecommitOperation(
          found->second->precommit_cleanup_settled,
          found->second->commit_completion_pending)) {
    operations_.erase(found);
  }
}

void BackupWorkflowRestoreAndroid::OnOperationAbandoned(
    ProfileBackupWorkflow::WindowToken window,
    const std::string& operation_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window) {
    return;
  }
  found->second->detached = true;
  found->second->prepare_caller.Reset();
  found->second->stage_caller.Reset();
  found->second->commit_caller.Reset();
  found->second->resolution_caller.Reset();
  if (!found->second->target_adopted) {
    found->second->phase = Operation::Phase::kCleaning;
    StartPretransferCleanup(operation_id);
  } else if (found->second->phase == Operation::Phase::kHiddenReview ||
             found->second->phase == Operation::Phase::kCleanupReview ||
             found->second->phase == Operation::Phase::kRecoveryRequired) {
    // No live physical owner remains in these phases. Coordinator withdrawal
    // below releases its inert operation; durable quarantine remains.
    operations_.erase(found);
  }
}

void BackupWorkflowRestoreAndroid::OnWindowUnregistered(
    ProfileBackupWorkflow::WindowToken window) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  WithdrawRecoveredWindow(window);
  for (auto& [operation_id, operation] : operations_) {
    if (operation->owner != window) {
      continue;
    }
    operation->detached = true;
    operation->prepare_caller.Reset();
    operation->stage_caller.Reset();
    operation->commit_caller.Reset();
    operation->resolution_caller.Reset();
  }
  std::vector<std::string> pretransfer;
  for (const auto& [operation_id, operation] : operations_) {
    if (operation->owner == window && !operation->target_adopted) {
      pretransfer.push_back(operation_id);
    }
  }
  for (const std::string& operation_id : pretransfer) {
    StartPretransferCleanup(operation_id);
  }
  for (auto iterator = operations_.begin(); iterator != operations_.end();) {
    if (iterator->second->owner == window &&
        (iterator->second->phase == Operation::Phase::kHiddenReview ||
         iterator->second->phase == Operation::Phase::kCleanupReview ||
         iterator->second->phase == Operation::Phase::kRecoveryRequired)) {
      iterator = operations_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

}  // namespace taffy
