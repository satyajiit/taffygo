// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_PRECOMMIT_CLEANUP_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_PRECOMMIT_CLEANUP_INTERNAL_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/threading/sequence_bound.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {

class BackupRestoreReservationRetirement;

class BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner {
 public:
  PrecommitCleanupBlockingOwner();
  PrecommitCleanupBlockingOwner(const PrecommitCleanupBlockingOwner&) = delete;
  PrecommitCleanupBlockingOwner& operator=(
      const PrecommitCleanupBlockingOwner&) = delete;
  ~PrecommitCleanupBlockingOwner();

  base::expected<void, BackupRestorePrecommitCleanupError> Prepare(
      base::FilePath target_profile_path,
      std::optional<std::string> target_profile_id);
  bool VerifyAfterChromiumDeletion(bool chromium_deleted);
  bool Close();

 private:
  class StorageOwner;
  std::unique_ptr<StorageOwner> storage_owner_;
};

struct BrowserProfilesRestoreLifecycle::PendingPrecommitCleanup {
  PendingPrecommitCleanup();
  ~PendingPrecommitCleanup();

  OwnedPrecommitBackupRestoreReservation target;
  BackupRestoreProfileReservation reservation;
  std::optional<std::string> expected_source_profile_id;
  std::vector<BackupRestorePreferenceWriteWitness> persistence_witnesses;
  PrecommitCleanupCallback callback;
  base::SequenceBound<PrecommitCleanupBlockingOwner> blocking_owner;
  std::unique_ptr<BackupRestoreReservationRetirement> retirement;
  bool readback_started = false;
  bool physical_action_dispatched = false;
};

namespace restore_precommit_cleanup_internal {

base::expected<BackupRestoreProfileReservation,
               BackupRestorePrecommitCleanupError>
ExactReservation(const PrefService* local_state,
                 const OwnedPrecommitBackupRestoreReservation& target);

bool ExactCurrentState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target,
    const BackupRestoreProfileReservation& reservation,
    const std::optional<std::string>& expected_source_profile_id);

bool ExactDeletedState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target,
    const BackupRestoreProfileReservation& reservation,
    const std::optional<std::string>& expected_source_profile_id);

bool ExactDeletionReadyState(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const OwnedPrecommitBackupRestoreReservation& target,
    const BackupRestoreProfileReservation& reservation,
    const std::optional<std::string>& expected_source_profile_id);

std::vector<BackupRestorePreferenceWriteWitness> CaptureCurrentWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path);

std::vector<BackupRestorePreferenceWriteWitness> CaptureRetirementWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path);

void VerifyPreferences(
    ProfileManager* profile_manager,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses,
    base::OnceCallback<void(bool)> callback);

}  // namespace restore_precommit_cleanup_internal
}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_PRECOMMIT_CLEANUP_INTERNAL_H_
