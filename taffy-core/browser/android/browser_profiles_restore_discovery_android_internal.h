// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_DISCOVERY_ANDROID_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_DISCOVERY_ANDROID_INTERNAL_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/types/expected.h"
#include "taffy/browser/android/browser_profiles_restore_discovery.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {

class BackupRestoreReservationRetirement;

enum class BackupRestoreRestartPhysicalState : uint8_t {
  kPristine,
  kCommitted,
  kOutcomeUnknown,
  kSchemaExact,
  kVerifiedDeleted,
};

enum class BackupRestoreRestartPhysicalError : uint8_t {
  kBusy,
  kCustodyAmbiguous,
  kSchemaMismatch,
  kStorageUnavailable,
};

using BackupRestoreRestartPhysicalResult =
    base::expected<BackupRestoreRestartPhysicalState,
                   BackupRestoreRestartPhysicalError>;

namespace restore_discovery_internal {

BackupRestoreRestartDiscoveryResult ProjectPhysicalFailure(
    BackupRestoreRestartPhysicalError error);

bool IsWellFormedDiscovery(const BackupRestoreRestartDiscovery& discovery);

}  // namespace restore_discovery_internal

// One no-choice restart inspection. This object owns every callback and
// blocking lease until it posts exactly one completion back to the lifecycle.
class BackupRestoreRestartDiscoveryOperation {
 public:
  using CompletionCallback =
      base::OnceCallback<void(BackupRestoreRestartDiscoveryResult)>;

  BackupRestoreRestartDiscoveryOperation(ProfileManager* profile_manager,
                                         PrefService* local_state,
                                         base::FilePath requested_source_path,
                                         BackupRestoreRestartDiscoveryMode mode,
                                         CompletionCallback completion);
  BackupRestoreRestartDiscoveryOperation(
      const BackupRestoreRestartDiscoveryOperation&) = delete;
  BackupRestoreRestartDiscoveryOperation& operator=(
      const BackupRestoreRestartDiscoveryOperation&) = delete;
  ~BackupRestoreRestartDiscoveryOperation();

  void Start();

 private:
  class BlockingOwner {
   public:
    BlockingOwner();
    BlockingOwner(const BlockingOwner&) = delete;
    BlockingOwner& operator=(const BlockingOwner&) = delete;
    ~BlockingOwner();

    BackupRestoreRestartPhysicalResult ReconcileCommit(
        base::FilePath target_profile_path,
        core_service::mojom::BackupRestoreTargetPtr exact_target,
        core_service::mojom::BackupRestoreCandidateWitnessPtr witness);
    BackupRestoreRestartPhysicalResult ObserveResolution(
        base::FilePath target_profile_path,
        core_service::mojom::BackupRestoreTargetPtr exact_target,
        core_service::mojom::BackupRestoreResolutionChoice choice);
    bool Close();

   private:
    class StorageOwner;
    std::unique_ptr<StorageOwner> storage_owner_;
  };

  void OnPreferencesVerified(bool matches);
  void InspectCurrentHistory();
  void OnHistoryInspected(
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result);
  void HandleClassification();
  void BeginCommitReconciliation();
  void BeginResolutionObservation(
      core_service::mojom::BackupRestorePhysicalIntent intent);
  void OnPhysicalProbe(BackupRestoreRestartPhysicalResult result);
  void PersistObservedOutcome(
      core_service::mojom::BackupRestoreObservedOutcome outcome);
  void OnOutcomeWriteDrained();
  void OnOutcomeReadBack(bool matches);
  void BeginTerminalVerification(
      BackupRestoreRestartDiscoveryStatus terminal_status,
      core_service::mojom::BackupRestoreResolutionChoice choice);
  void OnTerminalPhysicalProbe(BackupRestoreRestartPhysicalResult result);
  void OnTerminalOwnerClosed(bool closed);
  void BeginTerminalRetirement();
  void OnRetirementWriteDrained();
  void OnRetirementReadBack(bool matches);
  void OnRetirementPhysicalProbe(BackupRestoreRestartPhysicalResult result);
  void OnRetirementOwnerClosed(bool closed);
  void Finish(BackupRestoreRestartDiscoveryResult result);
  void OnFinishOwnerClosed(bool closed);
  void DeliverFinished();

  bool ExactCurrentState() const;
  bool TargetIsHiddenAndCoreless() const;
  bool TargetIsPublishedAndCoreless() const;
  bool TargetIsVerifiedDeleted() const;
  bool HistoryHasUnknownForActiveIntent() const;

  const raw_ptr<ProfileManager> profile_manager_;
  const raw_ptr<PrefService> local_state_;
  const base::FilePath requested_source_path_;
  const BackupRestoreRestartDiscoveryMode mode_;
  CompletionCallback completion_;
  BackupRestoreProfileReservation reservation_;
  ResolvedDormantBackupRestoreTarget target_;
  std::optional<BackupRestoreRecoveryPresentation> presentation_;
  BackupRestoreRecoveryRecords records_;
  core_service::mojom::OperationEnvelopePtr inspection_operation_;
  core_service::mojom::BackupRestoreRecoveryClassificationPtr classification_;
  core_service::mojom::BackupRestoreRecoveryRecordPtr active_intent_;
  core_service::mojom::BackupRestoreRecoveryRecordPtr outcome_record_;
  std::vector<BackupRestorePreferenceWriteWitness> persistence_witnesses_;
  std::unique_ptr<BackupRestoreReservationRetirement> retirement_;
  std::optional<BackupRestoreRestartDiscoveryStatus> terminal_status_;
  std::optional<BackupRestoreRestartDiscoveryResult> finish_result_;
  std::optional<core_service::mojom::BackupRestorePhysicalIntent>
      probing_intent_;
  base::SequenceBound<BlockingOwner> blocking_owner_;
  bool physical_observation_attempted_ = false;
  bool blocking_owner_closed_ = false;
  base::WeakPtrFactory<BackupRestoreRestartDiscoveryOperation> weak_factory_{
      this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_DISCOVERY_ANDROID_INTERNAL_H_
