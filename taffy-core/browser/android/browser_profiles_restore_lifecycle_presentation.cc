// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"

namespace taffy {
namespace {

using Error = BackupRestoreProfileRegistryError;

Error MapTargetError(BackupRestoreDormantTargetError error) {
  switch (error) {
    case BackupRestoreDormantTargetError::kBusy:
      return Error::kBusy;
    case BackupRestoreDormantTargetError::kInvalidHandle:
      return Error::kInvalidArgument;
    case BackupRestoreDormantTargetError::kRegistryRefused:
      return Error::kCorrupt;
    case BackupRestoreDormantTargetError::kPersistenceFailed:
    case BackupRestoreDormantTargetError::kStorageUnavailable:
    case BackupRestoreDormantTargetError::kCancelled:
      return Error::kUnavailable;
    case BackupRestoreDormantTargetError::kProfileStateRefused:
    case BackupRestoreDormantTargetError::kCoreAlreadyStarted:
    case BackupRestoreDormantTargetError::kTargetOccupied:
      return Error::kWrongPhysicalState;
  }
  return Error::kUnavailable;
}

}  // namespace

void BrowserProfilesRestoreLifecycle::PersistRecoveryPresentation(
    std::string reservation_id,
    std::u16string target_profile_label,
    std::vector<core_service::mojom::BackupRecordKind> original_selection,
    core_service::mojom::BackupRestorePlanResultPtr exact_plan,
    RecoveryPresentationCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (reservation_id.empty() || !exact_plan) {
    std::move(callback).Run(base::unexpected(Error::kInvalidArgument));
    return;
  }
  if (!dormant_target_transferred_ || pending_ || pending_binding_ ||
      pending_dormant_target_ || pending_recovery_presentation_ ||
      pending_commit_reconciliation_ || pending_candidate_resolution_ ||
      pending_precommit_cleanup_ || pending_pretransfer_cleanup_callback_) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  auto resolved = ResolveDormantBackupRestoreTarget(
      profile_manager_, local_state_, reservation_id);
  if (!resolved) {
    std::move(callback).Run(base::unexpected(MapTargetError(resolved.error())));
    return;
  }
  auto attached = AttachBackupRestoreRecoveryPresentation(
      local_state_, reservation_id, std::move(target_profile_label),
      original_selection, *exact_plan);
  if (!attached) {
    std::move(callback).Run(base::unexpected(attached.error()));
    return;
  }
  auto witnesses = CaptureDormantBackupRestoreTargetPreferenceWitnesses(
      *local_state_, resolved->target_profile_path);
  if (witnesses.empty()) {
    std::move(callback).Run(base::unexpected(Error::kUnavailable));
    return;
  }

  pending_recovery_presentation_ =
      std::make_unique<PendingRecoveryPresentation>();
  pending_recovery_presentation_->reservation_id = std::move(reservation_id);
  pending_recovery_presentation_->source_profile_path =
      resolved->source_profile_path;
  pending_recovery_presentation_->target_profile_path =
      resolved->target_profile_path;
  pending_recovery_presentation_->target_profile_id =
      resolved->target_profile_id;
  pending_recovery_presentation_->expected = std::move(*attached);
  pending_recovery_presentation_->persistence_witnesses = std::move(witnesses);
  pending_recovery_presentation_->callback = std::move(callback);
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnRecoveryPresentationWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnRecoveryPresentationWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_recovery_presentation_) {
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          &BackupRestorePreferenceFileMatchesAndSync,
          profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
          std::move(pending_recovery_presentation_->persistence_witnesses)),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnRecoveryPresentationReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnRecoveryPresentationReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_recovery_presentation_) {
    return;
  }
  if (!matches) {
    FinishRecoveryPresentation(base::unexpected(Error::kUnavailable));
    return;
  }
  auto resolved = ResolveDormantBackupRestoreTarget(
      profile_manager_, local_state_,
      pending_recovery_presentation_->reservation_id);
  if (!resolved ||
      resolved->source_profile_path !=
          pending_recovery_presentation_->source_profile_path ||
      resolved->target_profile_path !=
          pending_recovery_presentation_->target_profile_path ||
      resolved->target_profile_id !=
          pending_recovery_presentation_->target_profile_id) {
    FinishRecoveryPresentation(
        base::unexpected(resolved ? Error::kWrongPhysicalState
                                  : MapTargetError(resolved.error())));
    return;
  }
  auto persisted = ReadBackupRestoreRecoveryPresentation(
      local_state_, pending_recovery_presentation_->reservation_id);
  if (!persisted || *persisted != pending_recovery_presentation_->expected) {
    FinishRecoveryPresentation(base::unexpected(
        persisted ? Error::kWrongPhysicalState : persisted.error()));
    return;
  }
  FinishRecoveryPresentation(std::move(*persisted));
}

void BrowserProfilesRestoreLifecycle::FinishRecoveryPresentation(
    BackupRestoreRecoveryPresentationResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_recovery_presentation_) {
    return;
  }
  RecoveryPresentationCallback callback =
      std::move(pending_recovery_presentation_->callback);
  pending_recovery_presentation_.reset();
  // A window withdrawal can finish portable precommit cancellation while the
  // presentation fsync is still in flight. The one-use receipt remains in the
  // pending cleanup and starts only after this Local State operation settles.
  BeginPrecommitCleanupReadBack();
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy
