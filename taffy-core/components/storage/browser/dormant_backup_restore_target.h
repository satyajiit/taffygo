// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup {

class BackupRestoreStage;
class DormantBackupRestoreCommitTestPeer;
class DormantBackupRestoreTargetLease;
class DormantBackupRestoreTargetTestPeer;

enum class DormantBackupRestoreTargetError {
  kInvalidTarget,
  kTargetOccupied,
  kStorageUnavailable,
};

enum class DormantBackupRestoreStageError {
  kInvalidAuthorization,
  kAuthorizationConsumed,
  kTargetChanged,
  kStagingPathOccupied,
  kPlanRefused,
  kUnsupportedRecord,
  kPayloadMismatch,
  kStorageUnavailable,
  kCleanupFailed,
};

struct DormantBackupRestoreStageReceipt {
  std::array<uint8_t, 32> snapshot_sha256{};
  uint64_t record_count = 0;
  std::vector<core_service::mojom::SkillRecordPtr> procedures;
};

enum class DormantBackupRestoreCommitError {
  kInvalidAuthorization,
  kAuthorizationConsumed,
  kCommitNotPrepared,
  kStageUnavailable,
  kWitnessMismatch,
  kTargetChanged,
  kTargetNotPristine,
  kStorageUnavailable,
};

enum class DormantBackupRestoreCommitOutcome {
  kCommitted,
  kOutcomeUnknown,
};

// One blocking-sequence SQL owner for a browser-reserved hidden profile.
// The browser must durably quarantine the target path AND bind its fresh UUID
// before calling Create. This class neither reserves nor publishes a profile;
// it cannot create a Core, load account state or dispatch journaled effects.
//
// Creation is exclusive and accepts no archive database. Failure may leave an
// incomplete owned candidate. Destruction closes SQL and never deletes the
// reserved profile or target database; an owned transient stage may be removed
// best-effort, while failed cleanup remains under the browser's durable
// reservation for explicit reconciliation.
// Isolated staging and commit consume exact Core authority. Publication and
// profile activation are deliberately absent.
// The lifecycle adapter must be the sole writer of this app-private profile
// directory: SQLite opens by pathname, not by the exclusive descriptor below.
class DormantBackupRestoreTarget {
 public:
  static base::expected<std::unique_ptr<DormantBackupRestoreTarget>,
                        DormantBackupRestoreTargetError>
  Create(const base::FilePath& reserved_profile_path,
         std::string reserved_target_profile_id,
         bool private_profile);

  DormantBackupRestoreTarget(const DormantBackupRestoreTarget&) = delete;
  DormantBackupRestoreTarget& operator=(const DormantBackupRestoreTarget&) =
      delete;
  ~DormantBackupRestoreTarget();

  const std::string& profile_id() const { return profile_id_; }
  const base::FilePath& database_path() const { return database_path_; }

  // Consumes one exact source-Core authorization and builds a typed,
  // reversible stage beneath this reserved target. A valid authorization is
  // consumed before physical I/O and can never be reused by this owner,
  // including after failure or abandonment. Its decision operation must be
  // live when this call begins; the older planning deadline is identity only.
  // Success changes no row in the target database and returns the digest,
  // count, and selected SkillRecord DTOs re-read from the same isolated typed
  // stage. The records are observations for source-Core catalogue admission;
  // they grant no target authority.
  base::expected<DormantBackupRestoreStageReceipt,
                 DormantBackupRestoreStageError>
  StageAuthorized(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreStageAuthorizationPtr authorization,
      base::File plaintext_payload);

  // Re-reads the exact owned stage and returns only its canonical descriptor
  // witness. The first successful result is frozen for this owner. Before
  // calling CommitAuthorized, the reservation owner MUST durably record the
  // unchanged source-Core binding and this witness as one commit intent. This
  // storage owner cannot observe or replace that browser-journal precondition.
  base::expected<core_service::mojom::BackupRestoreCandidateWitnessPtr,
                 DormantBackupRestoreCommitError>
  PrepareCommitWitness();

  // Consumes one exact live source-Core authorization and attempts one atomic
  // physical write of the already staged, prepared projection. |plan| and
  // |authorization| remain the authority; |expected_witness| is observational
  // readback and grants nothing. Once a target transaction can have started,
  // every unverified terminal result is kOutcomeUnknown and this owner never
  // retries it. Success retains the isolated stage for explicit cleanup.
  base::expected<DormantBackupRestoreCommitOutcome,
                 DormantBackupRestoreCommitError>
  CommitAuthorized(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
      core_service::mojom::BackupRestoreCandidateWitnessPtr expected_witness);

  // Removes only this owner's exact isolated stage. Repetition after verified
  // cleanup is harmless. A cleanup failure retains path custody for retry and
  // never removes the candidate profile or its target database.
  base::expected<void, DormantBackupRestoreStageError> AbandonStage();

 private:
  friend class DormantBackupRestoreCommitTestPeer;
  friend class DormantBackupRestoreTargetTestPeer;

  DormantBackupRestoreTarget(
      base::FilePath database_path,
      std::string profile_id,
      base::File exclusive_file,
      std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease);
  bool HasOwnedDatabasePath() const;
  bool HasOwnedStagingPath() const;
  bool HasPristineTargetContents() const;
  bool IsPristineTarget() const;
  bool Initialize();

  // Declared first so reverse member destruction releases the physical lease
  // only after stage, SQL and held-file ownership have ended.
  const std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease_;
  const base::FilePath database_path_;
  const std::string profile_id_;
  base::File exclusive_file_;
  const std::unique_ptr<sql::Database> database_;
  bool stage_authorization_consumed_ = false;
  bool commit_authorization_consumed_ = false;
  bool staging_parent_unlinked_ = false;
  base::FilePath staging_parent_path_;
  base::File staging_parent_directory_;
  std::unique_ptr<BackupRestoreStage> restore_stage_;
  core_service::mojom::BackupRestoreCandidateWitnessPtr
      prepared_commit_witness_;
  base::OnceClosure after_commit_for_testing_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_H_
