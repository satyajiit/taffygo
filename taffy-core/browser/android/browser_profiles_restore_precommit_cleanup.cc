// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"

namespace taffy {
namespace {

namespace cleanup = restore_precommit_cleanup_internal;
using Error = BackupRestorePrecommitCleanupError;

}  // namespace

BrowserProfilesRestoreLifecycle::PendingPrecommitCleanup::
    PendingPrecommitCleanup()
    : blocking_owner(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

BrowserProfilesRestoreLifecycle::PendingPrecommitCleanup::
    ~PendingPrecommitCleanup() = default;

void BrowserProfilesRestoreLifecycle::BeginPrecommitCleanupReadBack() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_ ||
      pending_precommit_cleanup_->readback_started ||
      pending_recovery_presentation_) {
    return;
  }
  auto& pending = *pending_precommit_cleanup_;
  pending.readback_started = true;
  pending.persistence_witnesses = cleanup::CaptureCurrentWitnesses(
      *local_state_, pending.target.target_profile_path);
  cleanup::VerifyPreferences(
      profile_manager_, std::move(pending.persistence_witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnPrecommitCleanupReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  const auto& pending = *pending_precommit_cleanup_;
  if (!matches ||
      !cleanup::ExactCurrentState(profile_manager_, local_state_,
                                  pending.target, pending.reservation,
                                  pending.expected_source_profile_id)) {
    FinishPrecommitCleanup(base::unexpected(
        matches ? Error::kProfileStateRefused : Error::kPersistenceFailed));
    return;
  }
  pending_precommit_cleanup_->blocking_owner
      .AsyncCall(&PrecommitCleanupBlockingOwner::Prepare)
      .WithArgs(pending.target.target_profile_path,
                pending.target.target_profile_id)
      .Then(base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnPrecommitCleanupStoragePrepared,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupStoragePrepared(
    base::expected<void, Error> result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  auto& pending = *pending_precommit_cleanup_;
  if (!result) {
    FinishPrecommitCleanup(base::unexpected(result.error()));
    return;
  }
  if (!cleanup::ExactCurrentState(profile_manager_, local_state_,
                                  pending.target, pending.reservation,
                                  pending.expected_source_profile_id)) {
    FinishPrecommitCleanup(base::unexpected(Error::kProfileStateRefused));
    return;
  }
  const base::FilePath& target_path = pending.target.target_profile_path;
  if (!ScheduleProfileDirectoryForDeletion(target_path) ||
      !PersistProfileDirectoryDeletionMarker(target_path)) {
    CancelProfileDeletion(target_path);
    FinishPrecommitCleanup(base::unexpected(Error::kRecoveryRequired));
    return;
  }
  pending.persistence_witnesses =
      cleanup::CaptureCurrentWitnesses(*local_state_, target_path);
  if (pending.persistence_witnesses.empty()) {
    CancelProfileDeletion(target_path);
    FinishPrecommitCleanup(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  local_state_->CommitPendingWrite(
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnPrecommitCleanupDeletionMarkerWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnPrecommitCleanupDeletionMarkerWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  auto witnesses = std::move(pending_precommit_cleanup_->persistence_witnesses);
  cleanup::VerifyPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnPrecommitCleanupDeletionMarkerReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupDeletionMarkerReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  auto& pending = *pending_precommit_cleanup_;
  const base::FilePath& target_path = pending.target.target_profile_path;
  if (!matches ||
      !cleanup::ExactDeletionReadyState(profile_manager_, local_state_,
                                        pending.target, pending.reservation,
                                        pending.expected_source_profile_id) ||
      !ArmProfileDirectoryForDeletion(target_path)) {
    CancelProfileDeletion(target_path);
    FinishPrecommitCleanup(base::unexpected(Error::kRecoveryRequired));
    return;
  }
  const bool accepted = profile_manager_->DeleteMarkedEphemeralProfileOnAndroid(
      target_path,
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnPrecommitCleanupDeleted,
          weak_factory_.GetWeakPtr()));
  if (!accepted) {
    CancelProfileDeletion(target_path);
    FinishPrecommitCleanup(base::unexpected(Error::kRecoveryRequired));
    return;
  }
  pending.physical_action_dispatched = true;
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupDeleted(bool deleted) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_ ||
      !pending_precommit_cleanup_->physical_action_dispatched) {
    return;
  }
  pending_precommit_cleanup_->blocking_owner
      .AsyncCall(&PrecommitCleanupBlockingOwner::VerifyAfterChromiumDeletion)
      .WithArgs(deleted)
      .Then(base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnPrecommitCleanupDeletionVerified,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupDeletionVerified(
    bool verified) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  if (!verified) {
    FinishPrecommitCleanup(base::unexpected(Error::kRecoveryRequired));
    return;
  }
  pending_precommit_cleanup_->blocking_owner
      .AsyncCall(&PrecommitCleanupBlockingOwner::Close)
      .Then(base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnPrecommitCleanupStorageClosed,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupStorageClosed(
    bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  auto& pending = *pending_precommit_cleanup_;
  if (!closed || !cleanup::ExactDeletedState(
                     profile_manager_, local_state_, pending.target,
                     pending.reservation, pending.expected_source_profile_id)) {
    FinishPrecommitCleanup(base::unexpected(Error::kRecoveryRequired));
    return;
  }
  auto retirement =
      BackupRestoreReservationRetirement::BeginPristinePrecommitCleanup(
          local_state_, pending.reservation);
  if (!retirement.has_value()) {
    FinishPrecommitCleanup(base::unexpected(Error::kRegistryRefused));
    return;
  }
  pending.retirement = std::move(*retirement);
  pending.persistence_witnesses = cleanup::CaptureRetirementWitnesses(
      *local_state_, pending.target.target_profile_path);
  local_state_->CommitPendingWrite(
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnPrecommitCleanupRetirementWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnPrecommitCleanupRetirementWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  auto witnesses = std::move(pending_precommit_cleanup_->persistence_witnesses);
  cleanup::VerifyPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnPrecommitCleanupRetirementReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnPrecommitCleanupRetirementReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_ || !pending_precommit_cleanup_->retirement) {
    return;
  }
  if (!matches ||
      !pending_precommit_cleanup_->retirement->ReleaseAfterVerifiedAbsence()) {
    FinishPrecommitCleanup(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  FinishPrecommitCleanup(base::ok());
}

void BrowserProfilesRestoreLifecycle::FinishPrecommitCleanup(
    PrecommitCleanupResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_precommit_cleanup_) {
    return;
  }
  const bool completed = result.has_value();
  PrecommitCleanupCallback callback =
      std::move(pending_precommit_cleanup_->callback);
  pending_precommit_cleanup_.reset();
  if (completed) {
    dormant_target_transferred_ = false;
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy
