// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_BACKEND_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_BACKEND_H_

#include <stdint.h>

#include "base/files/file_path.h"
#include "sql/database.h"
#include "taffy/components/storage/browser/core_storage_broker.h"

namespace taffy {

// Blocking implementation owned by CoreStorageBroker's single sequenced
// runner. Split from the UI-sequence facade so schema/bootstrap and commit
// logic remain separately reviewable, while all access still shares one SQL
// connection and one physical-writer lifetime.
class CoreStorageBroker::Backend {
 public:
  Backend(base::FilePath database_path, bool ephemeral);
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;
  ~Backend();

  core_service::mojom::CoreBootstrapPtr LoadBootstrap(uint64_t generation,
                                                      bool private_profile);
  std::optional<AccountReconciliationState> LoadAccountReconciliationState();
  bool FinishAccountReconciliation();
  bool CommitIntent(core_service::mojom::EffectEnvelopePtr effect);
  bool CommitResult(core_service::mojom::EffectResultPtr result);
  bool AppendTaskActionIntent(DispatchIntentRecord record);
  bool AppendTaskActionResult(ActionResult result);
  std::optional<TaskActionJournalLookup> ReadTaskActionJournal(
      DispatchId dispatch_id,
      TaskId task_id,
      ActionId action_id);
  bool CommitStorage(core_service::mojom::EffectEnvelopePtr effect);
  void MarkEffectsLost(uint64_t generation,
                       std::vector<std::string> effect_ids);
  storage::backup::BackupSnapshotResult ReadBackupSnapshot(
      std::vector<core_service::mojom::BackupRecordKind> selection);
  base::expected<void, storage::backup::BackupRestoreStageError>
  StageBackupRestore(core_service::mojom::BackupRestorePlanResultPtr plan,
                     std::vector<uint8_t> confirmed_digest,
                     base::File plaintext_payload);
  bool VerifyBackupRestore(core_service::mojom::OperationEnvelopePtr operation,
                           std::vector<uint8_t> snapshot_sha256,
                           std::vector<uint8_t> confirmation_sha256);
  bool AbandonBackupRestore(std::string operation_id);

 private:
  bool EnsureOpen();
  bool InitializeRestoreStaging();
  bool IsPristineRestoreTarget(std::string_view profile_id);
  void ObserveBackupRestoreGeneration(uint64_t generation);
  bool DeletePendingRestoreStage();
  bool ReapRevokedRestoreStage();

  const base::FilePath database_path_;
  const bool ephemeral_;
  sql::Database database_;
  bool schema_verified_ = false;
  bool restore_staging_initialized_ = false;
  bool restore_staging_cleanup_pending_ = false;
  uint64_t backup_restore_generation_ = 0;
  bool restore_stage_revoked_ = false;
  std::unique_ptr<storage::backup::BackupRestoreStage> restore_stage_;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_BACKEND_H_
