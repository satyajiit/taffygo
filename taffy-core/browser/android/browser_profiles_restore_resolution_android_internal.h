// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_RESOLUTION_ANDROID_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_RESOLUTION_ANDROID_INTERNAL_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/threading/sequence_bound.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {

class BackupRestoreReservationRetirement;

class BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner {
 public:
  CandidateResolutionBlockingOwner();
  CandidateResolutionBlockingOwner(const CandidateResolutionBlockingOwner&) =
      delete;
  CandidateResolutionBlockingOwner& operator=(
      const CandidateResolutionBlockingOwner&) = delete;
  ~CandidateResolutionBlockingOwner();

  base::expected<void, BackupRestoreCandidateResolutionError> Prepare(
      base::FilePath target_profile_path,
      core_service::mojom::BackupRestoreTargetPtr exact_target,
      core_service::mojom::BackupRestoreCandidateWitnessPtr exact_witness,
      core_service::mojom::BackupRestoreResolutionChoice choice);
  bool FinalizeAccept();
  bool VerifyDiscardAfterChromiumDeletion(bool chromium_deleted);
  bool VerifyTerminalPhysical(
      base::FilePath target_profile_path,
      core_service::mojom::BackupRestoreTargetPtr exact_target,
      core_service::mojom::BackupRestoreResolutionChoice choice);
  bool Close();

 private:
  class StorageOwner;
  std::unique_ptr<StorageOwner> storage_owner_;
};

struct BrowserProfilesRestoreLifecycle::PendingCandidateResolution {
  PendingCandidateResolution();
  ~PendingCandidateResolution();

  ResolvedDormantBackupRestoreTarget target;
  BackupRestoreProfileReservation reservation;
  BackupRestoreRecoveryRecords records;
  core_service::mojom::BackupRestoreResolutionChoice choice =
      core_service::mojom::BackupRestoreResolutionChoice::kAcceptCandidate;
  std::string intent_id;
  core_service::mojom::OperationEnvelopePtr operation;
  core_service::mojom::BackupRestoreRecoveryResolutionAuthorizationPtr
      authorization;
  core_service::mojom::BackupRestoreRecoveryRecordPtr intent;
  core_service::mojom::BackupRestoreRecoveryRecordPtr outcome_record;
  core_service::mojom::BackupRestoreRecoveryClassificationPtr classification;
  std::vector<BackupRestorePreferenceWriteWitness> persistence_witnesses;
  CandidateResolutionCallback callback;
  base::SequenceBound<CandidateResolutionBlockingOwner> blocking_owner;
  std::unique_ptr<BackupRestoreReservationRetirement> retirement;
  std::optional<CandidateResolutionResult> finish_result;
  bool physical_action_dispatched = false;
  bool blocking_owner_closed = false;
};

namespace restore_resolution_internal {

base::expected<ResolvedDormantBackupRestoreTarget,
               BackupRestoreCandidateResolutionError>
ResolveTarget(ProfileManager* profile_manager,
              PrefService* local_state,
              const std::string& reservation_id);

base::expected<BackupRestoreProfileReservation,
               BackupRestoreCandidateResolutionError>
ExactReservation(const PrefService* local_state,
                 const ResolvedDormantBackupRestoreTarget& target);

bool ExactCurrentState(ProfileManager* profile_manager,
                       PrefService* local_state,
                       const ResolvedDormantBackupRestoreTarget& target,
                       const BackupRestoreProfileReservation& reservation,
                       const BackupRestoreRecoveryRecords& records);

core_service::mojom::BackupRestoreCandidateWitnessPtr WitnessFromHistory(
    const BackupRestoreRecoveryRecords& records);

core_service::mojom::OperationEnvelopePtr NewOperation(uint64_t generation,
                                                       std::string_view kind);

std::vector<BackupRestorePreferenceWriteWitness> CaptureResolutionWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path,
    bool require_target_attributes);

void VerifyResolutionPreferences(
    ProfileManager* profile_manager,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses,
    base::OnceCallback<void(bool)> callback);

}  // namespace restore_resolution_internal
}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_RESOLUTION_ANDROID_INTERNAL_H_
