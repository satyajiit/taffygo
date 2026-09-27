// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RECONCILER_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RECONCILER_H_

#include <memory>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup {

class DormantBackupRestoreTargetLease;

enum class DormantBackupRestoreReconcileError {
  kInvalidTarget,
  kTargetBusy,
  kTargetChanged,
  kSchemaMismatch,
  kInvalidWitness,
  kStorageUnavailable,
};

enum class DormantBackupRestoreReconcileState {
  kPristine,
  kCommitted,
  kOutcomeUnknown,
};

// Read-only recovery view of a browser-reserved dormant restore target. The
// caller MUST already hold the durable reservation for |reserved_profile_path|
// and |exact_target|. This type never reserves, migrates, razes, stages,
// commits, publishes or exposes commit authority.
//
// Opening requires the exact generated head schema and target identity. A
// completed exact read may classify a genuinely ambiguous projection as an
// unknown outcome. Schema, SQL, lease and path-custody failures remain typed
// errors; callers must not persist those failures as physical observations.
class DormantBackupRestoreTargetReconciler {
 public:
  static base::expected<std::unique_ptr<DormantBackupRestoreTargetReconciler>,
                        DormantBackupRestoreReconcileError>
  OpenForRecovery(const base::FilePath& reserved_profile_path,
                  core_service::mojom::BackupRestoreTargetPtr exact_target);

  DormantBackupRestoreTargetReconciler(
      const DormantBackupRestoreTargetReconciler&) = delete;
  DormantBackupRestoreTargetReconciler& operator=(
      const DormantBackupRestoreTargetReconciler&) = delete;
  ~DormantBackupRestoreTargetReconciler();

  const std::string& profile_id() const { return profile_id_; }

  // Compares every supported and forbidden target table against the durable,
  // content-free witness. Exact witness matching takes precedence over
  // pristine classification, so an explicit zero-record restore reconciles as
  // committed when its empty projection is intact.
  base::expected<DormantBackupRestoreReconcileState,
                 DormantBackupRestoreReconcileError>
  Reconcile(
      core_service::mojom::BackupRestoreCandidateWitnessPtr expected_witness);

 private:
  DormantBackupRestoreTargetReconciler(
      base::FilePath database_path,
      std::string profile_id,
      base::File held_database,
      std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease);
  bool HasOwnedDatabasePath() const;

  // Released last, after the read-only SQL and held descriptor are closed.
  const std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease_;
  const base::FilePath database_path_;
  const std::string profile_id_;
  base::File held_database_;
  const std::unique_ptr<sql::Database> database_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RECONCILER_H_
