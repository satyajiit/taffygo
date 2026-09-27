// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_target_android.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {
namespace {

using Error = BackupRestoreDormantTargetError;
using PhysicalState = BackupRestoreProfilePhysicalState;
using Quarantine = BackupRestoreProfileQuarantineStatus;
using WriteWitness = BackupRestorePreferenceWriteWitness;

base::expected<ResolvedDormantBackupRestoreTarget, Error>
ResolveDormantTargetImpl(ProfileManager* profile_manager,
                         PrefService* local_state,
                         const std::string& reservation_id) {
  if (!profile_manager || !local_state || reservation_id.empty() ||
      profile_manager->user_data_dir().empty() ||
      !profile_manager->user_data_dir().IsAbsolute()) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state);
  if (!reservations.has_value() || reservations->size() != 1u ||
      reservations->front().reservation_id != reservation_id ||
      reservations->front().physical_state != PhysicalState::kProfileCreated ||
      !reservations->front().target_profile_id) {
    return base::unexpected(Error::kRegistryRefused);
  }
  const BackupRestoreProfileReservation& reservation = reservations->front();
  const base::FilePath source_profile_path =
      profile_manager->user_data_dir().Append(
          reservation.source_profile_base_name);
  const base::FilePath target_profile_path =
      profile_manager->user_data_dir().Append(
          reservation.target_profile_base_name);
  if (source_profile_path == target_profile_path ||
      source_profile_path.DirName() != profile_manager->user_data_dir() ||
      target_profile_path.DirName() != profile_manager->user_data_dir() ||
      !profile_manager->IsAllowedProfilePath(source_profile_path) ||
      !profile_manager->IsAllowedProfilePath(target_profile_path)) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  Profile* const source =
      profile_manager->GetProfileByPath(source_profile_path);
  Profile* const target =
      profile_manager->GetProfileByPath(target_profile_path);
  ProfileAttributesEntry* const source_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(source_profile_path);
  ProfileAttributesEntry* const target_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_profile_path);
  if (!source || !target || source == target || source->IsOffTheRecord() ||
      target->IsOffTheRecord() || !profile_manager->IsValidProfile(source) ||
      !profile_manager->IsValidProfile(target) || !source_entry ||
      !target_entry || source_entry->IsOmitted() ||
      source_entry->IsEphemeral() || !target_entry->IsOmitted() ||
      !target_entry->IsEphemeral() ||
      profile_manager->GetLastUsedProfileDir() != source_profile_path ||
      BackupRestoreQuarantineForProfilePath(local_state, source_profile_path) !=
          Quarantine::kNotQuarantined ||
      BackupRestoreQuarantineForProfilePath(local_state, target_profile_path) !=
          Quarantine::kQuarantined) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  if (CoreServiceManagerFactory::HasExistingInstanceForProfile(target)) {
    return base::unexpected(Error::kCoreAlreadyStarted);
  }
  return ResolvedDormantBackupRestoreTarget{
      .reservation_id = reservation_id,
      .source_profile_path = source_profile_path,
      .target_profile_path = target_profile_path,
      .target_profile_id = *reservation.target_profile_id,
  };
}

std::vector<WriteWitness> CaptureDormantTargetWitnessesImpl(
    const PrefService& local_state,
    const base::FilePath& target_profile_path) {
  const std::string target_key = target_profile_path.BaseName().AsUTF8Unsafe();
  const base::Value* target_attributes =
      local_state.GetDict(prefs::kProfileAttributes).Find(target_key);
  if (!target_attributes) {
    return {};
  }
  std::vector<WriteWitness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .expected = base::Value(
           local_state
               .GetDict(
                   application_preferences::kBackupRestoreProfileReservations)
               .Clone())});
  witnesses.push_back({.dotted_path = base::StrCat(
                           {prefs::kProfileAttributes, ".", target_key}),
                       .expected = target_attributes->Clone()});
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesOrder,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  witnesses.push_back({.dotted_path = prefs::kProfilesNumCreated,
                       .expected = base::Value(local_state.GetInteger(
                           prefs::kProfilesNumCreated))});
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

std::vector<BackupRestorePreferenceWriteWitness>
CaptureDormantBackupRestoreTargetPreferenceWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path) {
  return CaptureDormantTargetWitnessesImpl(local_state, target_profile_path);
}

base::expected<ResolvedDormantBackupRestoreTarget, Error>
ResolveDormantBackupRestoreTarget(ProfileManager* profile_manager,
                                  PrefService* local_state,
                                  const std::string& reservation_id) {
  return ResolveDormantTargetImpl(profile_manager, local_state, reservation_id);
}

void VerifyDormantBackupRestoreTargetPreferences(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const base::FilePath& target_profile_path,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile_manager || !local_state || !callback) {
    if (callback) {
      std::move(callback).Run(false);
    }
    return;
  }
  VerifyPreferenceFile(
      profile_manager->user_data_dir().Append(chrome::kLocalStateFilename),
      CaptureDormantBackupRestoreTargetPreferenceWitnesses(*local_state,
                                                           target_profile_path),
      std::move(callback));
}

// Construction, like destruction, needs the complete private SQL owner for
// unique_ptr cleanup. Keep both on this side of the physical storage seam.
// Completeness is owed to every member, so the precommit-cleanup and
// resolution internal headers above are included for the two pending
// records this class holds by unique_ptr and for nothing else.
BrowserProfilesRestoreLifecycle::BrowserProfilesRestoreLifecycle(
    ProfileManager* profile_manager,
    PrefService* local_state)
    : profile_manager_(profile_manager), local_state_(local_state) {}

BrowserProfilesRestoreLifecycle::~BrowserProfilesRestoreLifecycle() = default;

void BrowserProfilesRestoreLifecycle::InitializeDormantTarget(
    BoundBackupRestoreProfileHandle handle,
    DormantTargetCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (handle.lifecycle_instance_.is_zero() ||
      handle.lifecycle_instance_ != handle_lifecycle_instance_ ||
      handle.reservation_id_.empty() || !owned_precommit_reservation_ ||
      owned_precommit_reservation_->reservation_id != handle.reservation_id_ ||
      !owned_precommit_reservation_->target_profile_id) {
    std::move(callback).Run(base::unexpected(Error::kInvalidHandle));
    return;
  }
  if (operation_in_flight() || dormant_target_state_ ||
      dormant_target_transferred_) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  auto resolved = ResolveDormantBackupRestoreTarget(
      profile_manager_, local_state_, handle.reservation_id_);
  if (!resolved.has_value()) {
    std::move(callback).Run(base::unexpected(resolved.error()));
    return;
  }
  pending_dormant_target_ = std::make_unique<PendingDormantTarget>();
  pending_dormant_target_->reservation_id = resolved->reservation_id;
  pending_dormant_target_->source_profile_path = resolved->source_profile_path;
  pending_dormant_target_->target_profile_path = resolved->target_profile_path;
  pending_dormant_target_->target_profile_id = resolved->target_profile_id;
  pending_dormant_target_->callback = std::move(callback);
  VerifyDormantBackupRestoreTargetPreferences(
      profile_manager_, local_state_, resolved->target_profile_path,
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnDormantTargetReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnDormantTargetReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_dormant_target_) {
    return;
  }
  if (!matches) {
    FinishDormantTarget(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  auto resolved = ResolveDormantBackupRestoreTarget(
      profile_manager_, local_state_, pending_dormant_target_->reservation_id);
  if (!resolved.has_value() ||
      resolved->source_profile_path !=
          pending_dormant_target_->source_profile_path ||
      resolved->target_profile_path !=
          pending_dormant_target_->target_profile_path ||
      resolved->target_profile_id !=
          pending_dormant_target_->target_profile_id) {
    FinishDormantTarget(base::unexpected(
        resolved.has_value() ? Error::kRegistryRefused : resolved.error()));
    return;
  }
  dormant_target_state_ = std::make_unique<DormantTargetState>(
      profile_manager_, local_state_, std::move(*resolved));
  dormant_target_state_->Initialize(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnDormantTargetInitialized,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnDormantTargetInitialized(
    base::expected<void, BackupRestoreDormantTargetError> result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_dormant_target_) {
    return;
  }
  pending_dormant_target_->initialization_result = std::move(result);
  VerifyDormantBackupRestoreTargetPreferences(
      profile_manager_, local_state_,
      pending_dormant_target_->target_profile_path,
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnDormantTargetPostReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnDormantTargetPostReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_dormant_target_ ||
      !pending_dormant_target_->initialization_result) {
    return;
  }
  if (pending_pretransfer_cleanup_callback_) {
    dormant_target_state_->CloseForPrecommitCleanup(
        base::BindOnce(&BrowserProfilesRestoreLifecycle::
                           OnPendingDormantTargetClosedForCleanup,
                       weak_factory_.GetWeakPtr()));
    return;
  }
  if (!matches) {
    FinishDormantTarget(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  auto resolved = ResolveDormantBackupRestoreTarget(
      profile_manager_, local_state_, pending_dormant_target_->reservation_id);
  if (!resolved.has_value() ||
      resolved->source_profile_path !=
          pending_dormant_target_->source_profile_path ||
      resolved->target_profile_path !=
          pending_dormant_target_->target_profile_path ||
      resolved->target_profile_id !=
          pending_dormant_target_->target_profile_id) {
    FinishDormantTarget(base::unexpected(
        resolved.has_value() ? Error::kRegistryRefused : resolved.error()));
    return;
  }
  if (!pending_dormant_target_->initialization_result->has_value()) {
    FinishDormantTarget(base::unexpected(
        pending_dormant_target_->initialization_result->error()));
    return;
  }
  std::unique_ptr<ProfileBackupRestoreTarget> target =
      std::move(dormant_target_state_);
  PrecommitBackupRestoreCleanupHandle cleanup(
      handle_lifecycle_instance_, pending_dormant_target_->reservation_id,
      pending_dormant_target_->source_profile_path,
      pending_dormant_target_->target_profile_path,
      pending_dormant_target_->target_profile_id);
  owned_precommit_reservation_.reset();
  dormant_target_transferred_ = true;
  FinishDormantTarget(InitializedDormantBackupRestoreTarget(
      std::move(target), std::move(cleanup)));
}

void BrowserProfilesRestoreLifecycle::FinishDormantTarget(
    DormantTargetResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_dormant_target_) {
    return;
  }
  const bool cancellation_requested =
      static_cast<bool>(pending_pretransfer_cleanup_callback_);
  DormantTargetCallback callback = std::move(pending_dormant_target_->callback);
  pending_dormant_target_.reset();
  base::WeakPtr<BrowserProfilesRestoreLifecycle> weak_this =
      weak_factory_.GetWeakPtr();
  std::move(callback).Run(
      cancellation_requested
          ? DormantTargetResult(base::unexpected(Error::kCancelled))
          : std::move(result));
  if (weak_this && cancellation_requested) {
    weak_this->BeginOwnedPrecommitCleanup();
  }
}

void BrowserProfilesRestoreLifecycle::OnPendingDormantTargetClosedForCleanup(
    bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_dormant_target_ || !pending_pretransfer_cleanup_callback_) {
    return;
  }
  dormant_target_state_.reset();
  DormantTargetCallback callback = std::move(pending_dormant_target_->callback);
  pending_dormant_target_.reset();
  base::WeakPtr<BrowserProfilesRestoreLifecycle> weak_this =
      weak_factory_.GetWeakPtr();
  std::move(callback).Run(
      base::unexpected(BackupRestoreDormantTargetError::kCancelled));
  if (!weak_this) {
    return;
  }
  if (!closed) {
    PrecommitCleanupCallback cleanup_callback =
        std::move(weak_this->pending_pretransfer_cleanup_callback_);
    std::move(cleanup_callback)
        .Run(base::unexpected(
            BackupRestorePrecommitCleanupError::kRecoveryRequired));
    return;
  }
  weak_this->BeginOwnedPrecommitCleanup();
}

}  // namespace taffy
