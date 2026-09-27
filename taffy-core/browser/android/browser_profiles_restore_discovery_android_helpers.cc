// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {

namespace restore_discovery_internal {

BackupRestoreRestartDiscoveryResult ProjectPhysicalFailure(
    BackupRestoreRestartPhysicalError error) {
  using DiscoveryError = BackupRestoreRestartDiscoveryError;
  using PhysicalError = BackupRestoreRestartPhysicalError;
  using Status = BackupRestoreRestartDiscoveryStatus;
  switch (error) {
    case PhysicalError::kSchemaMismatch:
      return BackupRestoreRestartDiscovery{.status = Status::kSchemaMismatch};
    case PhysicalError::kBusy:
    case PhysicalError::kCustodyAmbiguous:
      return BackupRestoreRestartDiscovery{.status = Status::kCustodyAmbiguous};
    case PhysicalError::kStorageUnavailable:
      return base::unexpected(DiscoveryError::kStorageUnavailable);
  }
  return BackupRestoreRestartDiscovery{.status = Status::kCustodyAmbiguous};
}

bool IsWellFormedDiscovery(
    const BackupRestoreRestartDiscovery& discovery_result) {
  using Action = BackupRestoreRestartCandidateAction;
  using Status = BackupRestoreRestartDiscoveryStatus;
  const bool rollback = discovery_result.status == Status::kRollbackAvailable;
  const bool cleanup = discovery_result.status == Status::kCleanupRequired;
  if (!rollback && !cleanup) {
    return !discovery_result.candidate.has_value();
  }
  if (!discovery_result.candidate) {
    return false;
  }
  return discovery_result.candidate->action ==
             (rollback ? Action::kReview : Action::kDiscardOnly) &&
         !discovery_result.candidate->reservation_id.empty();
}

}  // namespace restore_discovery_internal

bool BackupRestoreRestartDiscoveryOperation::ExactCurrentState() const {
  namespace reconciliation = restore_reconciliation_internal;
  namespace resolution = restore_resolution_internal;
  if (!presentation_) {
    return false;
  }
  auto target = resolution::ResolveTarget(profile_manager_, local_state_,
                                          reservation_.reservation_id);
  if (!target) {
    return false;
  }
  auto exact_reservation = resolution::ExactReservation(local_state_, *target);
  auto presentation = ReadBackupRestoreRecoveryPresentation(
      local_state_, reservation_.reservation_id);
  auto records = ReadBackupRestoreRecoveryJournal(local_state_,
                                                  reservation_.reservation_id);
  return *target == target_ && exact_reservation &&
         *exact_reservation == reservation_ && presentation &&
         *presentation == *presentation_ && records &&
         reconciliation::ExactHistory(*records, records_) &&
         resolution::ExactCurrentState(profile_manager_, local_state_, target_,
                                       reservation_, records_);
}

bool BackupRestoreRestartDiscoveryOperation::TargetIsHiddenAndCoreless() const {
  ProfileAttributesEntry* const entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_.target_profile_path);
  Profile* const profile =
      profile_manager_->GetProfileByPath(target_.target_profile_path);
  // Chromium does not persist IsOmitted. Across a process restart the exact
  // sole reservation and quarantine are the durable hidden-state proof;
  // ephemeral metadata prevents publication while discovery is in flight.
  return entry && entry->IsEphemeral() &&
         (!profile ||
          (!profile->IsOffTheRecord() &&
           profile_manager_->IsValidProfile(profile) &&
           !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile)));
}

bool BackupRestoreRestartDiscoveryOperation::TargetIsPublishedAndCoreless()
    const {
  ProfileAttributesEntry* const entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_.target_profile_path);
  Profile* const profile =
      profile_manager_->GetProfileByPath(target_.target_profile_path);
  return entry && !entry->IsOmitted() && !entry->IsEphemeral() &&
         (!profile ||
          (!profile->IsOffTheRecord() &&
           profile_manager_->IsValidProfile(profile) &&
           !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile)));
}

bool BackupRestoreRestartDiscoveryOperation::TargetIsVerifiedDeleted() const {
  return !profile_manager_->GetProfileAttributesStorage()
              .GetProfileAttributesWithPath(target_.target_profile_path) &&
         !profile_manager_->GetProfileByPath(target_.target_profile_path) &&
         CompleteProfileDirectoryDeletionMarker(target_.target_profile_path);
}

}  // namespace taffy
