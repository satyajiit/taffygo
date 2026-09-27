// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RESOLUTION_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RESOLUTION_H_

#include <memory>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup {

class DormantBackupRestoreTargetLease;

enum class DormantBackupRestoreResolutionError {
  kInvalidTarget,
  kTargetBusy,
  kTargetChanged,
  kSchemaMismatch,
  kInvalidWitness,
  kCandidateMismatch,
  kStageMismatch,
  kStorageUnavailable,
  kCleanupFailed,
};

// Read-only terminal bookkeeping witness. It acquires the same exclusive path
// lease and verifies only the current schema plus exact target identity. It
// deliberately does not require the original candidate projection: after a
// completed publication, later legitimate target-Core writes must not prevent
// recovery from finishing an interrupted browser-registry retirement.
base::expected<void, DormantBackupRestoreResolutionError>
VerifyPublishedDormantBackupRestoreTarget(
    const base::FilePath& reserved_profile_path,
    core_service::mojom::BackupRestoreTargetPtr exact_target);

// Exclusive read/cleanup custody for a committed hidden candidate. Opening
// re-verifies the exact target projection and, when present, the one retained
// isolated stage. It grants no publication authority and changes no target
// row. The browser must hold this owner across its durable Accept intent and
// final live-authority check.
class DormantBackupRestoreTargetFinalizer {
 public:
  static base::expected<std::unique_ptr<DormantBackupRestoreTargetFinalizer>,
                        DormantBackupRestoreResolutionError>
  OpenForAccept(
      const base::FilePath& reserved_profile_path,
      core_service::mojom::BackupRestoreTargetPtr exact_target,
      core_service::mojom::BackupRestoreCandidateWitnessPtr exact_witness);

  DormantBackupRestoreTargetFinalizer(
      const DormantBackupRestoreTargetFinalizer&) = delete;
  DormantBackupRestoreTargetFinalizer& operator=(
      const DormantBackupRestoreTargetFinalizer&) = delete;
  ~DormantBackupRestoreTargetFinalizer();

  const std::string& profile_id() const { return profile_id_; }

  // Re-verifies the committed projection, then removes only the validated
  // retained stage one entry at a time. Every removed directory entry is
  // followed by fsync of its surviving parent. Failure retains this object and
  // exact path custody for a same-operation retry; it never deletes the target
  // database or profile.
  base::expected<void, DormantBackupRestoreResolutionError>
  FinalizeForPublication();

  // Identity-only terminal witness while this owner still holds the path
  // lease. It intentionally does not compare the original projection.
  bool VerifyPublishedIdentity();

 private:
  DormantBackupRestoreTargetFinalizer(
      base::FilePath profile_path,
      base::FilePath database_path,
      std::string profile_id,
      core_service::mojom::BackupRestoreCandidateWitnessPtr witness,
      base::File held_core_directory,
      base::File held_database,
      std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease);

  base::expected<void, DormantBackupRestoreResolutionError>
  OpenAndValidateTarget();
  base::expected<void, DormantBackupRestoreResolutionError>
  LocateAndValidateRetainedStage();
  bool HasOwnedTargetPaths() const;
  bool HasOwnedStagePaths() const;
  bool RevalidateCandidate();

  // Released last, after every SQL/file/directory owner below.
  const std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease_;
  const base::FilePath profile_path_;
  const base::FilePath core_path_;
  const base::FilePath database_path_;
  const std::string profile_id_;
  const core_service::mojom::BackupRestoreCandidateWitnessPtr witness_;
  base::File held_core_directory_;
  base::File held_database_;
  std::unique_ptr<sql::Database> database_;
  bool stage_present_ = false;
  bool stage_database_unlinked_ = false;
  bool stage_child_unlinked_ = false;
  bool staging_parent_unlinked_ = false;
  base::FilePath staging_parent_path_;
  base::FilePath stage_child_path_;
  base::FilePath stage_database_path_;
  base::File held_staging_parent_;
  base::File held_stage_child_;
  base::File held_stage_database_;
  std::unique_ptr<sql::Database> stage_database_;
  SEQUENCE_CHECKER(sequence_checker_);
};

// Exclusive physical custody for one already-authorized hidden-candidate
// deletion. It validates the exact target identity, closes SQL before browser
// profile detachment, and keeps the path lease plus held inode until deletion
// has been observed and its surviving parent synchronized. Cache deletion is
// additionally proved by Chromium's result-bearing deletion callback.
class DormantBackupRestoreTargetDeletionCustody {
 public:
  static base::expected<
      std::unique_ptr<DormantBackupRestoreTargetDeletionCustody>,
      DormantBackupRestoreResolutionError>
  OpenForDiscard(const base::FilePath& reserved_profile_path,
                 core_service::mojom::BackupRestoreTargetPtr exact_target);

  DormantBackupRestoreTargetDeletionCustody(
      const DormantBackupRestoreTargetDeletionCustody&) = delete;
  DormantBackupRestoreTargetDeletionCustody& operator=(
      const DormantBackupRestoreTargetDeletionCustody&) = delete;
  ~DormantBackupRestoreTargetDeletionCustody();

  bool PrepareForProfileDeletion();
  bool VerifyProfileDeleted();

 private:
  DormantBackupRestoreTargetDeletionCustody(
      base::FilePath profile_path,
      base::FilePath database_path,
      std::string profile_id,
      base::File held_profile_parent,
      base::File held_database,
      std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease);
  bool HasOwnedTargetPath() const;

  // Released last, after the held unlinked database inode.
  const std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease_;
  const base::FilePath profile_path_;
  const base::FilePath database_path_;
  const std::string profile_id_;
  base::File held_profile_parent_;
  base::File held_database_;
  std::unique_ptr<sql::Database> database_;
  bool prepared_for_deletion_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RESOLUTION_H_
