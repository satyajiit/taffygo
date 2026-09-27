// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_init_params.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
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

bool RegistryStillOwns(const PrefService* local_state,
                       const std::string& reservation_id,
                       const base::FilePath& target_profile_path,
                       PhysicalState expected_state) {
  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state);
  return reservations.has_value() && reservations->size() == 1u &&
         reservations->front().reservation_id == reservation_id &&
         reservations->front().target_profile_base_name ==
             target_profile_path.BaseName() &&
         reservations->front().physical_state == expected_state;
}

std::vector<WriteWitness> CaptureLocalStateWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path,
    bool include_profile_metadata) {
  std::vector<WriteWitness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .expected = base::Value(
           local_state
               .GetDict(
                   application_preferences::kBackupRestoreProfileReservations)
               .Clone())});
  witnesses.push_back({.dotted_path = prefs::kProfilesNumCreated,
                       .expected = base::Value(local_state.GetInteger(
                           prefs::kProfilesNumCreated))});
  if (include_profile_metadata) {
    const std::string target_key =
        target_profile_path.BaseName().AsUTF8Unsafe();
    const base::Value* target_attributes =
        local_state.GetDict(prefs::kProfileAttributes).Find(target_key);
    if (!target_attributes) {
      return {};
    }
    witnesses.push_back({.dotted_path = base::StrCat(
                             {prefs::kProfileAttributes, ".", target_key}),
                         .expected = target_attributes->Clone()});
    witnesses.push_back(
        {.dotted_path = prefs::kProfilesOrder,
         .expected =
             base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  }
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

void BrowserProfilesRestoreLifecycle::OnQuarantineWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      CaptureLocalStateWitnesses(*local_state_, pending_->target_profile_path,
                                 false),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnQuarantineReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnQuarantineReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  if (!matches) {
    Finish(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  if (!RegistryStillOwns(local_state_, pending_->reservation_id,
                         pending_->target_profile_path,
                         PhysicalState::kPathReserved)) {
    Finish(base::unexpected(Error::kRegistryRefused));
    return;
  }
  ProfileAttributesStorage& storage =
      profile_manager_->GetProfileAttributesStorage();
  if (storage.GetProfileAttributesWithPath(pending_->target_profile_path) ||
      profile_manager_->GetProfileByPath(pending_->target_profile_path)) {
    Finish(base::unexpected(Error::kTargetPathOccupied));
    return;
  }
  ProfileAttributesInitParams params;
  params.profile_path = pending_->target_profile_path;
  params.profile_name = pending_->display_name;
  params.icon_index = storage.ChooseAvatarIconIndexForNewProfile();
  // Chromium requires every omitted entry to be ephemeral. Android does not
  // auto-delete ephemeral profiles on last-keepalive or startup, so this flag
  // hides the candidate without relinquishing authorized deletion custody.
  params.is_ephemeral = true;
  params.is_omitted = true;
  storage.AddProfile(std::move(params));
  ProfileAttributesEntry* entry =
      storage.GetProfileAttributesWithPath(pending_->target_profile_path);
  if (!entry || !entry->IsOmitted() || !entry->IsEphemeral()) {
    Finish(base::unexpected(Error::kCreationFailed));
    return;
  }
  // The hidden Chromium entry must have an exact disk witness before Profile
  // initialization can start services. The registry already blocks Taffy's
  // Core factory.
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnHiddenMetadataWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnHiddenMetadataWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      CaptureLocalStateWitnesses(*local_state_, pending_->target_profile_path,
                                 true),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnHiddenMetadataReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnHiddenMetadataReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  ProfileAttributesEntry* entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(pending_->target_profile_path);
  if (!matches) {
    Finish(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  if (!entry || !entry->IsOmitted() || !entry->IsEphemeral() ||
      !RegistryStillOwns(local_state_, pending_->reservation_id,
                         pending_->target_profile_path,
                         PhysicalState::kPathReserved)) {
    Finish(base::unexpected(Error::kRegistryRefused));
    return;
  }
  // Recheck the private path after both Local State barriers. If anything
  // appeared since the initial check, keep the quarantine record and refuse
  // to load or delete bytes whose provenance is unknown.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&base::PathExists, pending_->target_profile_path),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnReservedPathCheckComplete,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnReservedPathCheckComplete(
    bool path_exists) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  if (path_exists || !RegistryStillOwns(local_state_, pending_->reservation_id,
                                        pending_->target_profile_path,
                                        PhysicalState::kPathReserved)) {
    Finish(base::unexpected(path_exists ? Error::kTargetPathOccupied
                                        : Error::kRegistryRefused));
    return;
  }
  profile_manager_->CreateProfileAsync(
      pending_->target_profile_path,
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnProfileCreated,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnProfileCreated(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  ProfileAttributesEntry* entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(pending_->target_profile_path);
  if (!profile || profile->IsOffTheRecord() ||
      profile->GetPath() != pending_->target_profile_path ||
      !profile_manager_->IsValidProfile(profile) || !entry ||
      !entry->IsOmitted() || !entry->IsEphemeral() ||
      profile_manager_->GetLastUsedProfileDir() == profile->GetPath() ||
      !RegistryStillOwns(local_state_, pending_->reservation_id,
                         pending_->target_profile_path,
                         PhysicalState::kPathReserved)) {
    Finish(base::unexpected(Error::kCreationFailed));
    return;
  }
  profile->GetPrefs()->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnProfilePreferencesWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnProfilePreferencesWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  Profile* profile =
      profile_manager_->GetProfileByPath(pending_->target_profile_path);
  if (!profile || !profile_manager_->IsValidProfile(profile)) {
    Finish(base::unexpected(Error::kCreationFailed));
    return;
  }
  std::vector<WriteWitness> witnesses;
  witnesses.push_back({.dotted_path = prefs::kProfileName,
                       .expected = base::Value(profile->GetPrefs()->GetString(
                           prefs::kProfileName))});
  VerifyPreferenceFile(
      pending_->target_profile_path.Append(chrome::kPreferencesFilename),
      std::move(witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnProfilePreferencesReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnProfilePreferencesReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  if (!matches) {
    Finish(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  Profile* profile =
      profile_manager_->GetProfileByPath(pending_->target_profile_path);
  if (!profile || !profile_manager_->IsValidProfile(profile)) {
    Finish(base::unexpected(Error::kCreationFailed));
    return;
  }
  auto marked =
      MarkBackupRestoreProfileCreated(local_state_, pending_->reservation_id);
  if (!marked.has_value()) {
    Finish(base::unexpected(Error::kRegistryRefused));
    return;
  }
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnCreatedStateWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCreatedStateWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      CaptureLocalStateWitnesses(*local_state_, pending_->target_profile_path,
                                 true),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnCreatedStateReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCreatedStateReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_) {
    return;
  }
  if (!matches) {
    Finish(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  if (!RegistryStillOwns(local_state_, pending_->reservation_id,
                         pending_->target_profile_path,
                         PhysicalState::kProfileCreated)) {
    Finish(base::unexpected(Error::kRegistryRefused));
    return;
  }
  Finish(ReservedBackupRestoreProfile{
      .reservation_id = pending_->reservation_id,
      .target_profile_path = pending_->target_profile_path,
  });
}

}  // namespace taffy
