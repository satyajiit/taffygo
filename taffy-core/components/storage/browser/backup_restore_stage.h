// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RESTORE_STAGE_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RESTORE_STAGE_H_

#include <memory>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {

inline constexpr char kBackupRestoreStageDirectoryPrefix[] =
    "taffy-restore-stage-";

class BackupRestoreStageTestPeer;
class DormantBackupRestoreTarget;

enum class BackupRestoreStageError {
  kPlanRefused,
  kUnsupportedRecord,
  kPayloadMismatch,
  kStorageUnavailable,
  kStageBusy,
};

// An isolated, typed restore candidate, never a live profile writer. Creation
// requires a Core-verified, conflict-free new-profile plan and the exact digest
// confirmed by the person. It reads bounded authenticated ranges again, decodes
// the versioned allowlist, and verifies the resulting SQL projection by reading
// it back through the same typed backup readers. No account/session/journal or
// source profile database is copied. Unsupported classes refuse the whole plan.
//
// Use only on the storage owner's blocking sequence. The caller supplies an
// already-owned staging parent, not a destination profile or provider path.
// Before commit preparation, destruction removes only the unique temporary
// child created by this object. After preparation, destruction deliberately
// retains that child under the browser's durable reservation for restart
// recovery; only explicit verified cleanup removes it. Commit/rollback
// decisions belong to the portable Core protocol. A browser storage owner
// consumes that authority; this class cannot mint it.
class BackupRestoreStage {
 public:
  static base::expected<std::unique_ptr<BackupRestoreStage>,
                        BackupRestoreStageError>
  Create(const base::FilePath& staging_parent,
         const core_service::mojom::BackupRestorePlanResult& plan,
         base::span<const uint8_t> confirmed_digest,
         base::File plaintext_payload);

  BackupRestoreStage(const BackupRestoreStage&) = delete;
  BackupRestoreStage& operator=(const BackupRestoreStage&) = delete;
  ~BackupRestoreStage();

  // Only typed selected records may be copied out at commit. Never adopt this
  // complete file as a profile database: the archive grants no authority over
  // unselected tables, even if a staging file is changed after creation.
  const base::FilePath& database_path() const { return database_path_; }
  const core_service::mojom::BackupRestorePlanResult& plan() const {
    return *plan_;
  }

  // Reopens the exact staging database and compares every typed descriptor.
  // A caller must renew this check before publishing/committing a candidate.
  bool Verify() const;

  // Returns only typed records whose exact descriptors still match the plan.
  // Record buffers wipe on destruction; no other staging tables are returned.
  // A physical commit must separately hold portable authorization and
  // revalidate the target inside its own write transaction.
  base::expected<std::vector<BackupSnapshotRecord>, BackupRestoreStageError>
  ReadVerifiedRecords() const;

  // Returns the exact bounded SkillRecord DTOs from the verified stage in
  // canonical identity order. The source Core must still run its portable
  // definition decoder and structural policy validation before issuing
  // consumptive commit authority; this observation grants nothing by itself.
  base::expected<std::vector<core_service::mojom::SkillRecordPtr>,
                 BackupRestoreStageError>
  ReadVerifiedSkillRecords() const;

  // Removes the exact unique staging child. A failed deletion retains path
  // custody so the owner can retry; callers must not destroy this object until
  // deletion succeeds or a higher-level shutdown cleanup takes responsibility.
  // Repeating a successful deletion is harmless.
  bool Delete();

 private:
  friend class BackupRestoreStageTestPeer;
  friend class DormantBackupRestoreTarget;

  explicit BackupRestoreStage(
      core_service::mojom::BackupRestorePlanResultPtr plan);
  // Transfers implicit-destruction cleanup back to the durable profile
  // reservation. Explicit Delete() remains available and verified.
  void RetainForRecoveryOnDestruction();
  base::expected<void, BackupRestoreStageError> Build(
      const base::FilePath& staging_parent,
      base::File plaintext_payload);

  base::ScopedTempDir directory_;
  base::FilePath database_path_;
  core_service::mojom::BackupRestorePlanResultPtr plan_;
  bool retain_for_recovery_on_destruction_ = false;
  base::RepeatingCallback<bool()> deletion_gate_for_testing_;
};

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RESTORE_STAGE_H_
