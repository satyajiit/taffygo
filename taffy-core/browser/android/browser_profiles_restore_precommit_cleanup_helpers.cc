// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy::restore_precommit_cleanup_internal {
namespace {

using Error = BackupRestorePrecommitCleanupError;
using Quarantine = BackupRestoreProfileQuarantineStatus;
using Witness = BackupRestorePreferenceWriteWitness;

bool ExactSource(ProfileManager* profile_manager,
                 PrefService* local_state,
                 const OwnedPrecommitBackupRestoreReservation& target,
                 const std::optional<std::string>& expected_source_profile_id) {
  if (!profile_manager || !local_state ||
      profile_manager->user_data_dir().empty() ||
      target.source_profile_path.DirName() !=
          profile_manager->user_data_dir() ||
      target.target_profile_path.DirName() !=
          profile_manager->user_data_dir() ||
      target.source_profile_path == target.target_profile_path ||
      !profile_manager->IsAllowedProfilePath(target.source_profile_path) ||
      !profile_manager->IsAllowedProfilePath(target.target_profile_path)) {
    return false;
  }
  Profile* const source =
      profile_manager->GetProfileByPath(target.source_profile_path);
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target.source_profile_path);
  if (!source || source->IsOffTheRecord() ||
      !profile_manager->IsValidProfile(source) || !entry ||
      entry->IsOmitted() || entry->IsEphemeral() ||
      profile_manager->GetLastUsedProfileDir() != target.source_profile_path ||
      BackupRestoreQuarantineForProfilePath(local_state,
                                            target.source_profile_path) !=
          Quarantine::kNotQuarantined) {
    return false;
  }
  if (!expected_source_profile_id) {
    return true;
  }
  CoreServiceManager* const manager =
      CoreServiceManagerFactory::GetForProfileIfExists(source);
  return manager &&
         manager->browser_profile_id() == *expected_source_profile_id;
}

bool EmptyHistory(const PrefService* local_state,
                  const std::string& reservation_id) {
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  return history && history->empty();
}

Witness TargetAttributesWitness(const PrefService& local_state,
                                const base::FilePath& target_profile_path) {
  const std::string key = target_profile_path.BaseName().AsUTF8Unsafe();
  const base::Value* attributes =
      local_state.GetDict(prefs::kProfileAttributes).Find(key);
  Witness witness{.dotted_path =
                      base::StrCat({prefs::kProfileAttributes, ".", key})};
  if (attributes) {
    witness.expected = attributes->Clone();
  } else {
    witness.must_be_absent = true;
  }
  return witness;
}

}  // namespace

base::expected<BackupRestoreProfileReservation, Error> ExactReservation(
    const PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target) {
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations || reservations->size() != 1u) {
    return base::unexpected(Error::kRegistryRefused);
  }
  const auto& reservation = reservations->front();
  if (reservation.reservation_id != target.reservation_id ||
      reservation.source_profile_base_name !=
          target.source_profile_path.BaseName() ||
      reservation.target_profile_base_name !=
          target.target_profile_path.BaseName() ||
      reservation.physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated ||
      reservation.target_profile_id != target.target_profile_id) {
    return base::unexpected(Error::kRegistryRefused);
  }
  return reservation;
}

bool ExactCurrentState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target,
    const BackupRestoreProfileReservation& reservation,
    const std::optional<std::string>& expected_source_profile_id) {
  auto current = ExactReservation(local_state, target);
  if (!current || *current != reservation ||
      !ExactSource(profile_manager, local_state, target,
                   expected_source_profile_id) ||
      !EmptyHistory(local_state, target.reservation_id) ||
      IsProfileDirectoryMarkedForDeletion(target.target_profile_path) ||
      BackupRestoreQuarantineForProfilePath(local_state,
                                            target.target_profile_path) !=
          Quarantine::kQuarantined) {
    return false;
  }
  Profile* const profile =
      profile_manager->GetProfileByPath(target.target_profile_path);
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target.target_profile_path);
  return profile &&
         profile !=
             profile_manager->GetProfileByPath(target.source_profile_path) &&
         !profile->IsOffTheRecord() &&
         profile_manager->IsValidProfile(profile) && entry &&
         entry->IsOmitted() && entry->IsEphemeral() &&
         !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile);
}

bool ExactDeletionReadyState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target,
    const BackupRestoreProfileReservation& reservation,
    const std::optional<std::string>& expected_source_profile_id) {
  auto current = ExactReservation(local_state, target);
  if (!current || *current != reservation ||
      !ExactSource(profile_manager, local_state, target,
                   expected_source_profile_id) ||
      !EmptyHistory(local_state, target.reservation_id) ||
      BackupRestoreQuarantineForProfilePath(local_state,
                                            target.target_profile_path) !=
          Quarantine::kQuarantined) {
    return false;
  }
  Profile* const profile =
      profile_manager->GetProfileByPath(target.target_profile_path);
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target.target_profile_path);
  return profile &&
         profile !=
             profile_manager->GetProfileByPath(target.source_profile_path) &&
         !profile->IsOffTheRecord() &&
         profile_manager->IsValidProfile(profile) && entry &&
         entry->IsOmitted() && entry->IsEphemeral() &&
         !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile);
}

bool ExactDeletedState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target,
    const BackupRestoreProfileReservation& reservation,
    const std::optional<std::string>& expected_source_profile_id) {
  auto current = ExactReservation(local_state, target);
  return current && *current == reservation &&
         ExactSource(profile_manager, local_state, target,
                     expected_source_profile_id) &&
         EmptyHistory(local_state, target.reservation_id) &&
         !profile_manager->GetProfileByPath(target.target_profile_path) &&
         !profile_manager->GetProfileAttributesStorage()
              .GetProfileAttributesWithPath(target.target_profile_path) &&
         CompleteProfileDirectoryDeletionMarker(target.target_profile_path);
}

std::vector<Witness> CaptureCurrentWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path) {
  Witness attributes =
      TargetAttributesWitness(local_state, target_profile_path);
  if (attributes.must_be_absent) {
    return {};
  }
  std::vector<Witness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .expected = base::Value(
           local_state
               .GetDict(
                   application_preferences::kBackupRestoreProfileReservations)
               .Clone())});
  witnesses.push_back(std::move(attributes));
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesOrder,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesDeleted,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesDeleted).Clone())});
  return witnesses;
}

std::vector<Witness> CaptureRetirementWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path) {
  std::vector<Witness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .must_be_absent = true});
  witnesses.push_back(
      TargetAttributesWitness(local_state, target_profile_path));
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesOrder,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesDeleted,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesDeleted).Clone())});
  return witnesses;
}

void VerifyPreferences(ProfileManager* profile_manager,
                       std::vector<Witness> witnesses,
                       base::OnceCallback<void(bool)> callback) {
  if (!profile_manager || witnesses.empty() || !callback) {
    if (callback) {
      std::move(callback).Run(false);
    }
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          &BackupRestorePreferenceFileMatchesAndSync,
          profile_manager->user_data_dir().Append(chrome::kLocalStateFilename),
          std::move(witnesses)),
      std::move(callback));
}

}  // namespace taffy::restore_precommit_cleanup_internal
