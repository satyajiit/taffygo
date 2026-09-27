// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"
#include "taffy/browser/profile_backup_precommit_cancellation.h"

namespace taffy {
namespace {

namespace cleanup = restore_precommit_cleanup_internal;
namespace mojom = core_service::mojom;
using Error = BackupRestorePrecommitCleanupError;

bool IsExactOptionalCancelledBinding(const mojom::BackupRestoreBinding* binding,
                                     const std::string& source_profile_id,
                                     const std::string& target_profile_id) {
  if (!binding) {
    return true;
  }
  return binding->planning_operation &&
         IsValidBackupRestoreBinding(
             binding, binding->planning_operation->service_generation) &&
         binding->owner_profile_id == source_profile_id && binding->target &&
         binding->target->kind ==
             mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         binding->target->profile_id == target_profile_id;
}

}  // namespace

void BrowserProfilesRestoreLifecycle::CleanupNewPrecommitReservation(
    PrecommitBackupRestoreCleanupHandle handle,
    ProfileBackupPrecommitCancellationReceipt receipt,
    PrecommitCleanupCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (handle.lifecycle_instance_.is_zero() ||
      handle.lifecycle_instance_ != handle_lifecycle_instance_ ||
      handle.reservation_id_.empty() || handle.source_profile_path_.empty() ||
      handle.target_profile_path_.empty() ||
      handle.target_profile_id_.empty() || !dormant_target_transferred_) {
    std::move(callback).Run(base::unexpected(Error::kInvalidHandle));
    return;
  }
  if (pending_ || pending_binding_ || pending_dormant_target_ ||
      pending_commit_reconciliation_ || pending_candidate_resolution_ ||
      pending_precommit_cleanup_ || pending_pretransfer_cleanup_callback_ ||
      owned_precommit_reservation_ || dormant_target_state_) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  if (!receipt.valid_ ||
      !IsValidProfileBackupOperationId(receipt.operation_id_) ||
      receipt.source_profile_id_.empty() ||
      receipt.target_profile_id_ != handle.target_profile_id_ ||
      receipt.source_profile_id_ == receipt.target_profile_id_ ||
      !IsExactOptionalCancelledBinding(receipt.binding_.get(),
                                       receipt.source_profile_id_,
                                       receipt.target_profile_id_)) {
    std::move(callback).Run(base::unexpected(Error::kInvalidReceipt));
    return;
  }
  if (pending_recovery_presentation_ &&
      (pending_recovery_presentation_->reservation_id !=
           handle.reservation_id_ ||
       pending_recovery_presentation_->source_profile_path !=
           handle.source_profile_path_ ||
       pending_recovery_presentation_->target_profile_path !=
           handle.target_profile_path_ ||
       pending_recovery_presentation_->target_profile_id !=
           handle.target_profile_id_)) {
    std::move(callback).Run(base::unexpected(Error::kInvalidHandle));
    return;
  }

  OwnedPrecommitBackupRestoreReservation target{
      .reservation_id = std::move(handle.reservation_id_),
      .source_profile_path = std::move(handle.source_profile_path_),
      .target_profile_path = std::move(handle.target_profile_path_),
      .target_profile_id = std::move(handle.target_profile_id_),
  };
  auto reservation = cleanup::ExactReservation(local_state_, target);
  if (!reservation) {
    std::move(callback).Run(base::unexpected(reservation.error()));
    return;
  }
  auto history =
      ReadBackupRestoreRecoveryJournal(local_state_, target.reservation_id);
  if (!history) {
    std::move(callback).Run(base::unexpected(Error::kRecoveryRequired));
    return;
  }
  if (!history->empty()) {
    std::move(callback).Run(base::unexpected(Error::kHistoryPresent));
    return;
  }

  pending_precommit_cleanup_ = std::make_unique<PendingPrecommitCleanup>();
  pending_precommit_cleanup_->target = std::move(target);
  pending_precommit_cleanup_->reservation = std::move(*reservation);
  pending_precommit_cleanup_->expected_source_profile_id =
      std::move(receipt.source_profile_id_);
  pending_precommit_cleanup_->callback = std::move(callback);
  receipt.Invalidate();
  // Presentation persistence owns the same Local State file. Retain the
  // one-use cancellation receipt and exact physical handle, but do not race
  // its readback or expose its result. FinishRecoveryPresentation starts this
  // cleanup after that write reaches a stable success-or-failure checkpoint.
  BeginPrecommitCleanupReadBack();
}

}  // namespace taffy
