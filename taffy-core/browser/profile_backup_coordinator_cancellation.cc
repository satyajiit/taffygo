// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"

namespace taffy {
namespace {
namespace core_mojom = core_service::mojom;

void AbandonAllStages(
    scoped_refptr<storage::backup::BackupArchiveStageStore> store) {
  store->AbandonAll();
}
void PostAbandon(scoped_refptr<storage::backup::BackupArchiveStageStore> store,
                 std::string operation_id) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](scoped_refptr<storage::backup::BackupArchiveStageStore> owned,
             std::string id) { owned->Abandon(id); },
          std::move(store), std::move(operation_id)));
}

void DeliverFinishedPrecommitCancellation(
    ProfileBackupCoordinator::RestorePreviewCallback preview_callback,
    ProfileBackupCoordinator::RestoreStageCallback stage_callback,
    ProfileBackupCoordinator::PrecommitCancellationCallback cleanup_callback,
    ProfileBackupError error,
    std::optional<ProfileBackupPrecommitCancellationReceipt> receipt) {
  if (preview_callback) {
    std::move(preview_callback).Run(base::unexpected(error));
  }
  if (stage_callback) {
    std::move(stage_callback).Run(base::unexpected(error));
  }
  if (cleanup_callback) {
    if (receipt) {
      std::move(cleanup_callback).Run(std::move(*receipt));
    } else {
      std::move(cleanup_callback)
          .Run(base::unexpected(ProfileBackupError::kBusy));
    }
  }
}
}  // namespace

void ProfileBackupCoordinator::Cancel(const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ExportCallback export_callback;
  RestorePreviewCallback import_callback;
  RestoreStageCallback stage_callback;
  ProfileBackupError import_error = ProfileBackupError::kCancelled;
  if (auto found = exports_.find(operation_id); found != exports_.end()) {
    export_callback = std::move(found->second->callback);
    exports_.erase(found);
  }
  bool cancel_portable_restore = false;
  if (auto found = imports_.find(operation_id); found != imports_.end()) {
    ImportOperation& operation = *found->second;
    import_callback = std::move(operation.callback);
    stage_callback = std::move(operation.stage_callback);
    import_error = operation.cancellation_error;
    // Cleanup needs identity and ownership, not an open plaintext import.
    // A target already staging owns its moved descriptor independently.
    operation.imported.reset();
    switch (operation.phase) {
      case ImportOperation::Phase::kAuthorizingCommit:
      case ImportOperation::Phase::kCommittingRestore:
      case ImportOperation::Phase::kReportingCommit:
        // A pending authority reply is suppressed before physical handoff.
        // Once handed off, one atomic terminal drains; neither cancellation
        // nor a precommit cleanup RPC may consume that candidate's custody.
        operation.cancellation_requested = true;
        break;
      case ImportOperation::Phase::kHiddenReview:
      case ImportOperation::Phase::kCommitRecoveryRequired:
        operation.cancellation_requested = true;
        // A completed recovery handoff has already destroyed the writer and
        // released workflow interest. Durable registry/journal quarantine,
        // rather than this inert map entry, owns restart custody.
        if (!operation.target) {
          imports_.erase(found);
        }
        break;
      case ImportOperation::Phase::kPlanningRestore:
        // A successful late reply mints the retained plan. Keep callback
        // custody so OnRestorePlanned can cancel that exact binding.
        operation.cancellation_requested = true;
        cancel_portable_restore = true;
        break;
      case ImportOperation::Phase::kReady:
      case ImportOperation::Phase::kConfirmingRestore:
      case ImportOperation::Phase::kStagingRestore:
      case ImportOperation::Phase::kStaged:
        operation.cancellation_requested = true;
        cancel_portable_restore = true;
        break;
      case ImportOperation::Phase::kCancellingRestore:
        cancel_portable_restore = true;
        break;
      case ImportOperation::Phase::kAwaitingCopy:
        if (operation.precommit_cancellation_callback) {
          operation.cancellation_requested = true;
          operation.portable_cancellation_complete = true;
          operation.phase = ImportOperation::Phase::kCancellingRestore;
          cancel_portable_restore = true;
        } else {
          imports_.erase(found);
        }
        break;
      case ImportOperation::Phase::kStartingCore:
      case ImportOperation::Phase::kReadingVerifiedStage:
      case ImportOperation::Phase::kInspectingManifest:
      case ImportOperation::Phase::kHashingPayload:
        // No request can have created a portable plan yet. Physical target
        // custody still belongs to this operation until cleanup is verified.
        operation.cancellation_requested = true;
        operation.portable_cancellation_complete = true;
        operation.phase = ImportOperation::Phase::kCancellingRestore;
        cancel_portable_restore = true;
        break;
    }
  }
  if (IsValidProfileBackupOperationId(operation_id)) {
    PostAbandon(stage_store_, operation_id);
  }
  if (cancel_portable_restore) {
    BeginImportCancellation(operation_id);
  }
  if (export_callback) {
    std::move(export_callback)
        .Run(base::unexpected(ProfileBackupError::kCancelled));
  }
  if (import_callback) {
    std::move(import_callback).Run(base::unexpected(import_error));
  }
  if (stage_callback) {
    std::move(stage_callback).Run(base::unexpected(import_error));
  }
}

void ProfileBackupCoordinator::CancelAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<ExportCallback> export_callbacks;
  std::vector<RestorePreviewCallback> import_callbacks;
  std::vector<RestoreStageCallback> stage_callbacks;
  std::vector<std::string> portable_cancellations;
  for (auto& entry : exports_) {
    auto& operation = entry.second;
    if (operation->callback) {
      export_callbacks.push_back(std::move(operation->callback));
    }
  }
  for (auto iterator = imports_.begin(); iterator != imports_.end();) {
    const std::string operation_id = iterator->first;
    auto& operation = iterator->second;
    if (operation->callback) {
      import_callbacks.push_back(std::move(operation->callback));
    }
    if (operation->stage_callback) {
      stage_callbacks.push_back(std::move(operation->stage_callback));
    }
    operation->imported.reset();
    switch (operation->phase) {
      case ImportOperation::Phase::kAuthorizingCommit:
      case ImportOperation::Phase::kCommittingRestore:
      case ImportOperation::Phase::kReportingCommit:
        operation->cancellation_requested = true;
        ++iterator;
        break;
      case ImportOperation::Phase::kHiddenReview:
      case ImportOperation::Phase::kCommitRecoveryRequired:
        operation->cancellation_requested = true;
        if (!operation->target) {
          iterator = imports_.erase(iterator);
        } else {
          ++iterator;
        }
        break;
      case ImportOperation::Phase::kPlanningRestore:
        operation->cancellation_requested = true;
        portable_cancellations.push_back(operation_id);
        ++iterator;
        break;
      case ImportOperation::Phase::kReady:
      case ImportOperation::Phase::kConfirmingRestore:
      case ImportOperation::Phase::kStagingRestore:
      case ImportOperation::Phase::kStaged:
        operation->cancellation_requested = true;
        portable_cancellations.push_back(operation_id);
        ++iterator;
        break;
      case ImportOperation::Phase::kCancellingRestore:
        portable_cancellations.push_back(operation_id);
        ++iterator;
        break;
      case ImportOperation::Phase::kAwaitingCopy:
        if (operation->precommit_cancellation_callback) {
          operation->cancellation_requested = true;
          operation->portable_cancellation_complete = true;
          operation->phase = ImportOperation::Phase::kCancellingRestore;
          portable_cancellations.push_back(operation_id);
          ++iterator;
        } else {
          iterator = imports_.erase(iterator);
        }
        break;
      case ImportOperation::Phase::kStartingCore:
      case ImportOperation::Phase::kReadingVerifiedStage:
      case ImportOperation::Phase::kInspectingManifest:
      case ImportOperation::Phase::kHashingPayload:
        operation->cancellation_requested = true;
        operation->portable_cancellation_complete = true;
        operation->phase = ImportOperation::Phase::kCancellingRestore;
        portable_cancellations.push_back(operation_id);
        ++iterator;
        break;
    }
  }
  exports_.clear();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&AbandonAllStages, stage_store_));
  base::WeakPtr<ProfileBackupCoordinator> weak_this =
      weak_factory_.GetWeakPtr();
  for (const std::string& operation_id : portable_cancellations) {
    if (!weak_this) {
      break;
    }
    weak_this->BeginImportCancellation(operation_id);
  }
  for (auto& callback : export_callbacks) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kCancelled));
  }
  for (auto& callback : import_callbacks) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kCancelled));
  }
  for (auto& callback : stage_callbacks) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kCancelled));
  }
}

void ProfileBackupCoordinator::BeginImportCancellation(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::WeakPtr<ProfileBackupCoordinator> weak_this =
      weak_factory_.GetWeakPtr();
  BeginTargetCleanup(operation_id);
  // Abandon is allowed to complete inline. When portable cancellation is
  // already settled, its receipt callback may destroy this coordinator.
  if (weak_this) {
    weak_this->ContinueImportCancellation(operation_id);
  }
}

void ProfileBackupCoordinator::ContinueImportCancellation(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    return;
  }
  ImportOperation& operation = *found->second;
  if (operation.portable_cancellation_complete) {
    MaybeFinishImportCancellation(operation_id);
    return;
  }
  if (operation.commit_authority_requested ||
      !operation.cancellation_requested || !manager_ ||
      operation.cancellation_rpc_in_flight || !operation.plan ||
      !operation.plan->binding) {
    return;
  }
  core_mojom::OperationEnvelopePtr envelope = NewBackupOperation(
      manager_->service_generation(), "backup-restore-cancel", operation_id);
  if (!envelope) {
    return;
  }
  auto request = core_mojom::BackupRestoreCancellationRequest::New(
      std::move(envelope), operation.plan->binding.Clone());
  operation.phase = ImportOperation::Phase::kCancellingRestore;
  operation.cancellation_rpc_in_flight = true;
  manager_->backup_protocol().CancelBackupRestoreBeforeCommit(
      std::move(request),
      base::BindOnce(&ProfileBackupCoordinator::OnImportCancelled,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnImportCancelled(
    const std::string& operation_id,
    core_mojom::BackupRestoreProtocolResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kCancellingRestore) {
    return;
  }
  found->second->cancellation_rpc_in_flight = false;
  if (!result ||
      result->status != core_mojom::BackupRestoreProtocolStatus::kSucceeded) {
    // Physical bytes are already abandoned, but portable authority is not.
    // Retain the workflow interest so an explicit later Cancel can retry and
    // a disconnect can withdraw the source-Core session exactly.
    return;
  }
  found->second->portable_cancellation_complete = true;
  MaybeFinishImportCancellation(operation_id);
}

void ProfileBackupCoordinator::BeginTargetCleanup(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() || !found->second->cancellation_requested ||
      found->second->commit_authority_requested ||
      found->second->target_cleanup_complete) {
    return;
  }
  ImportOperation& operation = *found->second;
  if (operation.target_cleanup_in_flight) {
    // A second exact cancellation is an explicit retry request. Remember it
    // across the in-flight callback so a failed first cleanup cannot strand
    // the target after its presentation owner has already withdrawn.
    operation.target_cleanup_retry_requested = true;
    return;
  }
  if (!operation.target) {
    operation.target_cleanup_complete = true;
    return;
  }
  operation.target_cleanup_retry_requested = false;
  operation.target_cleanup_in_flight = true;
  operation.target->Abandon(
      base::BindOnce(&ProfileBackupCoordinator::OnTargetCleaned,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnTargetCleaned(const std::string& operation_id,
                                               bool cleaned) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    return;
  }
  ImportOperation& operation = *found->second;
  operation.target_cleanup_in_flight = false;
  if (!cleaned && operation.target_cleanup_retry_requested) {
    operation.target_cleanup_retry_requested = false;
    BeginTargetCleanup(operation_id);
    return;
  }
  operation.target_cleanup_retry_requested = false;
  operation.target_cleanup_complete = cleaned;
  MaybeFinishImportCancellation(operation_id);
}

void ProfileBackupCoordinator::MaybeFinishImportCancellation(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() || !found->second->cancellation_requested ||
      !found->second->portable_cancellation_complete ||
      !found->second->target_cleanup_complete) {
    return;
  }
  ImportOperation& operation = *found->second;
  const ProfileBackupError error = operation.cancellation_error;
  RestorePreviewCallback preview_callback = std::move(operation.callback);
  RestoreStageCallback stage_callback = std::move(operation.stage_callback);
  PrecommitCancellationCallback precommit_callback =
      std::move(operation.precommit_cancellation_callback);
  std::optional<ProfileBackupPrecommitCancellationReceipt> receipt;
  if (precommit_callback && !operation.commit_authority_requested &&
      !operation.commit_dispatched && !operation.source_profile_id.empty() &&
      !operation.target_profile_id.empty()) {
    receipt = ProfileBackupPrecommitCancellationReceipt(
        operation_id, operation.source_profile_id, operation.target_profile_id,
        operation.plan && operation.plan->binding
            ? operation.plan->binding.Clone()
            : core_mojom::BackupRestoreBindingPtr());
  }
  // A receipt means the target's explicit Abandon callback has completed.
  // Destroy the adapter now so its blocking owner and exclusive path lease
  // are gone before physical profile cleanup can begin.
  operation.target.reset();
  operation.workflow_interest.reset();
  imports_.erase(found);
  PostAbandon(stage_store_, operation_id);
  // No code after this helper touches the coordinator: any callback may
  // synchronously destroy the workflow that owns it.
  DeliverFinishedPrecommitCancellation(
      std::move(preview_callback), std::move(stage_callback),
      std::move(precommit_callback), error, std::move(receipt));
}

}  // namespace taffy
