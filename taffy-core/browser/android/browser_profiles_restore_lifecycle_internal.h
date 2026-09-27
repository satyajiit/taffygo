// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_LIFECYCLE_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_LIFECYCLE_INTERNAL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/threading/sequence_bound.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_recovery_codec.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"

namespace taffy {

class CoreServiceManager;

struct ResolvedDormantBackupRestoreTarget {
  std::string reservation_id;
  base::FilePath source_profile_path;
  base::FilePath target_profile_path;
  std::string target_profile_id;

  bool operator==(const ResolvedDormantBackupRestoreTarget&) const = default;
};

base::expected<ResolvedDormantBackupRestoreTarget,
               BackupRestoreDormantTargetError>
ResolveDormantBackupRestoreTarget(ProfileManager* profile_manager,
                                  PrefService* local_state,
                                  const std::string& reservation_id);

std::vector<BackupRestorePreferenceWriteWitness>
CaptureDormantBackupRestoreTargetPreferenceWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_profile_path);

void VerifyDormantBackupRestoreTargetPreferences(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const base::FilePath& target_profile_path,
    base::OnceCallback<void(bool)> callback);

struct BrowserProfilesRestoreLifecycle::PendingReservation {
  raw_ptr<Profile> source_profile = nullptr;
  std::u16string display_name;
  std::string reservation_id;
  base::FilePath target_profile_path;
  ReserveCallback callback;
};

struct BrowserProfilesRestoreLifecycle::PendingBinding {
  std::string reservation_id;
  std::string target_profile_id;
  BindCallback callback;
};

struct BrowserProfilesRestoreLifecycle::PendingDormantTarget {
  std::string reservation_id;
  base::FilePath source_profile_path;
  base::FilePath target_profile_path;
  std::string target_profile_id;
  std::optional<base::expected<void, BackupRestoreDormantTargetError>>
      initialization_result;
  DormantTargetCallback callback;
};

struct BrowserProfilesRestoreLifecycle::PendingRecoveryPresentation {
  std::string reservation_id;
  base::FilePath source_profile_path;
  base::FilePath target_profile_path;
  std::string target_profile_id;
  BackupRestoreRecoveryPresentation expected;
  std::vector<BackupRestorePreferenceWriteWitness> persistence_witnesses;
  RecoveryPresentationCallback callback;
};

struct OwnedPrecommitBackupRestoreReservation {
  std::string reservation_id;
  base::FilePath source_profile_path;
  base::FilePath target_profile_path;
  std::optional<std::string> target_profile_id;
};

class BrowserProfilesRestoreLifecycle::CommitReconciliationBlockingOwner {
 public:
  CommitReconciliationBlockingOwner();
  CommitReconciliationBlockingOwner(const CommitReconciliationBlockingOwner&) =
      delete;
  CommitReconciliationBlockingOwner& operator=(
      const CommitReconciliationBlockingOwner&) = delete;
  ~CommitReconciliationBlockingOwner();

  CommitReconciliationStorageResult Reconcile(
      base::FilePath target_profile_path,
      core_service::mojom::BackupRestoreTargetPtr exact_target,
      core_service::mojom::BackupRestoreCandidateWitnessPtr witness);
};

struct BrowserProfilesRestoreLifecycle::PendingCommitReconciliation {
  PendingCommitReconciliation();
  ~PendingCommitReconciliation();

  std::string reservation_id;
  base::FilePath source_profile_path;
  base::FilePath target_profile_path;
  std::string target_profile_id;
  BackupRestoreRecoveryRecords records;
  core_service::mojom::OperationEnvelopePtr inspection_operation;
  core_service::mojom::BackupRestoreRecoveryRecordPtr commit_intent;
  core_service::mojom::BackupRestoreCandidateWitnessPtr witness;
  core_service::mojom::BackupRestoreRecoveryRecordPtr outcome_record;
  core_service::mojom::BackupRestoreCommitOutcome physical_outcome =
      core_service::mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
  std::vector<BackupRestorePreferenceWriteWitness> persistence_witnesses;
  CommitReconciliationCallback callback;
  base::SequenceBound<CommitReconciliationBlockingOwner> blocking_owner;
};

namespace restore_reconciliation_internal {

BackupRestoreCommitReconciliationError ProjectStorageReconcileError(
    storage::backup::DormantBackupRestoreReconcileError error);

base::expected<ResolvedDormantBackupRestoreTarget,
               BackupRestoreCommitReconciliationError>
ResolveTarget(ProfileManager* profile_manager,
              PrefService* local_state,
              const std::string& reservation_id);

CoreServiceManager* LiveSourceManager(
    ProfileManager* profile_manager,
    const ResolvedDormantBackupRestoreTarget& target,
    const BackupRestoreRecoveryRecords& records);

bool ExactHistory(const BackupRestoreRecoveryRecords& left,
                  const BackupRestoreRecoveryRecords& right);

core_service::mojom::OperationEnvelopePtr NewInspectionOperation(
    uint64_t generation);

bool ValidInspectionResult(
    const core_service::mojom::OperationEnvelope* expected,
    const core_service::mojom::BackupRestoreRecoveryInspectionResult* result);

}  // namespace restore_reconciliation_internal

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_LIFECYCLE_INTERNAL_H_
