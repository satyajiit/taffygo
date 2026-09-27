// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {
namespace {

using CleanupError = BackupRestorePrecommitCleanupError;
using CleanupResult = BrowserProfilesRestoreLifecycle::PrecommitCleanupResult;
namespace cleanup = restore_precommit_cleanup_internal;

}  // namespace

void BrowserProfilesRestoreLifecycle::CancelPendingNewReservation(
    PrecommitCleanupCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (pending_pretransfer_cleanup_callback_ || pending_precommit_cleanup_ ||
      pending_commit_reconciliation_ || pending_candidate_resolution_) {
    std::move(callback).Run(base::unexpected(CleanupError::kBusy));
    return;
  }
  if (dormant_target_transferred_) {
    std::move(callback).Run(base::unexpected(CleanupError::kInvalidHandle));
    return;
  }
  if (!pending_ && !pending_binding_ && !pending_dormant_target_ &&
      !owned_precommit_reservation_) {
    auto reservations = ReadBackupRestoreProfileReservations(local_state_);
    std::move(callback).Run(
        reservations && reservations->empty()
            ? CleanupResult(base::ok())
            : CleanupResult(base::unexpected(CleanupError::kRecoveryRequired)));
    return;
  }
  pending_pretransfer_cleanup_callback_ = std::move(callback);
  if (!pending_ && !pending_binding_ && !pending_dormant_target_) {
    BeginOwnedPrecommitCleanup();
  }
}

void BrowserProfilesRestoreLifecycle::BeginOwnedPrecommitCleanup() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_pretransfer_cleanup_callback_ || pending_ || pending_binding_ ||
      pending_dormant_target_ || pending_precommit_cleanup_) {
    return;
  }
  if (!owned_precommit_reservation_) {
    PrecommitCleanupCallback callback =
        std::move(pending_pretransfer_cleanup_callback_);
    auto reservations = ReadBackupRestoreProfileReservations(local_state_);
    std::move(callback).Run(
        reservations && reservations->empty()
            ? CleanupResult(base::ok())
            : CleanupResult(base::unexpected(CleanupError::kRecoveryRequired)));
    return;
  }

  // A binding callback may have lost its persistence witness after the exact
  // in-memory write. Adopt only the target UUID from the still-single exact
  // reservation; every other field remains the creation lifecycle's custody.
  auto reservations = ReadBackupRestoreProfileReservations(local_state_);
  if (reservations && reservations->size() == 1u &&
      reservations->front().reservation_id ==
          owned_precommit_reservation_->reservation_id &&
      reservations->front().source_profile_base_name ==
          owned_precommit_reservation_->source_profile_path.BaseName() &&
      reservations->front().target_profile_base_name ==
          owned_precommit_reservation_->target_profile_path.BaseName() &&
      !owned_precommit_reservation_->target_profile_id) {
    owned_precommit_reservation_->target_profile_id =
        reservations->front().target_profile_id;
  }
  auto reservation =
      cleanup::ExactReservation(local_state_, *owned_precommit_reservation_);
  if (!reservation) {
    PrecommitCleanupCallback callback =
        std::move(pending_pretransfer_cleanup_callback_);
    std::move(callback).Run(base::unexpected(CleanupError::kRecoveryRequired));
    return;
  }

  pending_precommit_cleanup_ = std::make_unique<PendingPrecommitCleanup>();
  pending_precommit_cleanup_->target = std::move(*owned_precommit_reservation_);
  pending_precommit_cleanup_->reservation = std::move(*reservation);
  pending_precommit_cleanup_->callback =
      std::move(pending_pretransfer_cleanup_callback_);
  owned_precommit_reservation_.reset();
  BeginPrecommitCleanupReadBack();
}

}  // namespace taffy
