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
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {
namespace {

using Error = BackupRestoreProfileReserveError;
using PhysicalState = BackupRestoreProfilePhysicalState;
using WriteWitness = BackupRestorePreferenceWriteWitness;

std::vector<WriteWitness> CaptureBindingWitness(
    const PrefService& local_state) {
  std::vector<WriteWitness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .expected = base::Value(
           local_state
               .GetDict(
                   application_preferences::kBackupRestoreProfileReservations)
               .Clone())});
  return witnesses;
}

void VerifyPreferenceFile(const base::FilePath& path,
                          std::vector<WriteWitness> witnesses,
                          base::OnceCallback<void(bool)> callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&BackupRestorePreferenceFileMatchesAndSync, path,
                     std::move(witnesses)),
      std::move(callback));
}

}  // namespace

BoundBackupRestoreProfileHandle::BoundBackupRestoreProfileHandle(
    base::Token lifecycle_instance,
    std::string reservation_id)
    : lifecycle_instance_(lifecycle_instance),
      reservation_id_(std::move(reservation_id)) {}

BoundBackupRestoreProfileHandle::BoundBackupRestoreProfileHandle(
    BoundBackupRestoreProfileHandle&& other) noexcept
    : lifecycle_instance_(
          std::exchange(other.lifecycle_instance_, base::Token())),
      reservation_id_(std::exchange(other.reservation_id_, std::string())) {}

BoundBackupRestoreProfileHandle& BoundBackupRestoreProfileHandle::operator=(
    BoundBackupRestoreProfileHandle&& other) noexcept {
  if (this != &other) {
    lifecycle_instance_ =
        std::exchange(other.lifecycle_instance_, base::Token());
    reservation_id_ = std::exchange(other.reservation_id_, std::string());
  }
  return *this;
}

BoundBackupRestoreProfileHandle::~BoundBackupRestoreProfileHandle() = default;

void BrowserProfilesRestoreLifecycle::BindTargetProfileId(
    std::string reservation_id,
    std::string target_profile_id,
    BindCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (operation_in_flight()) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state_);
  if (!profile_manager_ || !local_state_ || !owned_precommit_reservation_ ||
      owned_precommit_reservation_->reservation_id != reservation_id ||
      !reservations.has_value() || reservations->size() != 1u ||
      reservations->front().reservation_id != reservation_id ||
      reservations->front().physical_state != PhysicalState::kProfileCreated) {
    std::move(callback).Run(base::unexpected(Error::kRegistryRefused));
    return;
  }
  auto bound = BindBackupRestoreTargetProfileId(local_state_, reservation_id,
                                                target_profile_id);
  if (!bound.has_value()) {
    std::move(callback).Run(base::unexpected(Error::kRegistryRefused));
    return;
  }
  pending_binding_ = std::make_unique<PendingBinding>();
  pending_binding_->reservation_id = std::move(reservation_id);
  pending_binding_->target_profile_id = std::move(target_profile_id);
  pending_binding_->callback = std::move(callback);
  local_state_->CommitPendingWrite(
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnBindingWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnBindingWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_binding_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      CaptureBindingWitness(*local_state_),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnBindingReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnBindingReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_binding_) {
    return;
  }
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state_);
  if (!matches) {
    FinishBinding(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  if (!reservations.has_value() || reservations->size() != 1u ||
      reservations->front().reservation_id !=
          pending_binding_->reservation_id ||
      reservations->front().physical_state != PhysicalState::kProfileCreated ||
      reservations->front().target_profile_id !=
          pending_binding_->target_profile_id) {
    FinishBinding(base::unexpected(Error::kRegistryRefused));
    return;
  }
  FinishBinding(BoundBackupRestoreProfileHandle(
      handle_lifecycle_instance_, pending_binding_->reservation_id));
}

void BrowserProfilesRestoreLifecycle::FinishBinding(BindResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_binding_) {
    return;
  }
  const bool cancellation_requested =
      static_cast<bool>(pending_pretransfer_cleanup_callback_);
  if (result.has_value() && owned_precommit_reservation_ &&
      owned_precommit_reservation_->reservation_id ==
          pending_binding_->reservation_id) {
    owned_precommit_reservation_->target_profile_id =
        pending_binding_->target_profile_id;
  }
  BindCallback callback = std::move(pending_binding_->callback);
  pending_binding_.reset();
  base::WeakPtr<BrowserProfilesRestoreLifecycle> weak_this =
      weak_factory_.GetWeakPtr();
  std::move(callback).Run(cancellation_requested
                              ? BindResult(base::unexpected(Error::kCancelled))
                              : std::move(result));
  if (weak_this && cancellation_requested) {
    weak_this->BeginOwnedPrecommitCleanup();
  }
}

}  // namespace taffy
