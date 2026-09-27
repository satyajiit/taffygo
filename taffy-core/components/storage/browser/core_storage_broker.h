// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_BROKER_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_BROKER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "taffy/common/public/page_intelligence_service.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// The only three answers a durable action-journal read may produce. A missing
// row is different from an open intent: the former means the caller's claimed
// dispatch identity was never committed, while the latter is the ambiguity
// reconciliation exists to handle.
enum class TaskActionJournalState : uint8_t {
  kMissing = 0,
  kIntentOnly = 1,
  kTerminal = 2,
};

struct TaskActionJournalLookup {
  TaskActionJournalState state = TaskActionJournalState::kMissing;
  // Meaningful only for kTerminal. The storage edge validates the closed
  // ActionResultCode before returning it to the UI sequence.
  ActionResultCode result_code = ActionResultCode::kInternalError;
};

// The profile's only physical writer for core checkpoints, journal batches,
// and effect intent/result records. All SQL and filesystem access stays on one
// blocking sequenced runner in the browser process.
class CoreStorageBroker : public TaskJournalSink {
 public:
  // Browser-only metadata used to reconcile the SQL checkpoint with the
  // platform credential vault before any account state is restored to Rust.
  // Token material never enters this type or the core journal.
  struct AccountReconciliationState {
    core_service::mojom::AccountSessionHandlePtr committed_session;
    bool has_pending_session_mutation = false;
  };

  using BootstrapCallback =
      base::OnceCallback<void(core_service::mojom::CoreBootstrapPtr)>;
  using BackupSnapshotCallback =
      base::OnceCallback<void(storage::backup::BackupSnapshotResult)>;
  using BackupRestoreStageCallback = base::OnceCallback<void(
      base::expected<void, storage::backup::BackupRestoreStageError>)>;
  using AccountReconciliationCallback =
      base::OnceCallback<void(std::optional<AccountReconciliationState>)>;
  using JournalCallback = base::OnceCallback<void(bool)>;
  using TaskActionLookupCallback =
      base::OnceCallback<void(std::optional<TaskActionJournalLookup>)>;
  using CompletionCallback =
      base::OnceCallback<void(core_service::mojom::EffectResultPtr)>;

  CoreStorageBroker(base::FilePath database_path, bool ephemeral);
  CoreStorageBroker(const CoreStorageBroker&) = delete;
  CoreStorageBroker& operator=(const CoreStorageBroker&) = delete;
  ~CoreStorageBroker() override;

  void LoadBootstrap(uint64_t generation,
                     bool private_profile,
                     BootstrapCallback callback);
  void LoadAccountReconciliationState(AccountReconciliationCallback callback);
  // Called only after the account adapter has made the canonical vault empty
  // while account dispatch is quiescent. The SQL half is idempotent so a crash
  // between vault cleanup and this transaction safely repeats the protocol.
  void FinishAccountReconciliation(JournalCallback callback);
  void CommitIntent(const core_service::mojom::EffectEnvelope& effect,
                    JournalCallback callback);
  void CommitResult(const core_service::mojom::EffectResult& result,
                    JournalCallback callback);
  // TaskJournalSink. Both methods return immediately on the UI sequence; the
  // completion runs only after this profile's one sequenced writer answers.
  void RecordDispatching(
      DispatchIntentRecord record,
      std::unique_ptr<TaskJournalAppendCallback> callback) override;
  void RecordTerminalResult(
      ActionResult result,
      std::unique_ptr<TaskJournalAppendCallback> callback) override;
  // Reads exactly one dispatch claim. nullopt means the storage read itself
  // failed; a successful read that found no row returns kMissing.
  void ReadTaskActionJournal(DispatchId dispatch_id,
                             TaskId task_id,
                             ActionId action_id,
                             TaskActionLookupCallback callback);
  void DispatchStorage(core_service::mojom::EffectEnvelopePtr effect,
                       CompletionCallback callback);
  // Settles only the identities the live broker still owns. `generation` is
  // correlation metadata within one browser session, not a durable process
  // incarnation and therefore never defines a sweep by itself.
  void MarkEffectsLost(uint64_t generation,
                       std::vector<std::string> effect_ids);

  // Reads only the explicitly selected, implemented backup record families.
  // Private profiles and any unsupported selection are refused as a whole.
  void ReadBackupSnapshot(
      std::vector<core_service::mojom::BackupRecordKind> selection,
      BackupSnapshotCallback callback);

  // One isolated restore candidate, owned by the same sequenced physical
  // writer as the target profile. Success means staged, never committed.
  void StageBackupRestore(
      const core_service::mojom::BackupRestorePlanResult& plan,
      std::vector<uint8_t> confirmed_digest,
      base::File plaintext_payload,
      BackupRestoreStageCallback callback);
  void VerifyBackupRestore(
      const core_service::mojom::OperationEnvelope& operation,
      std::vector<uint8_t> snapshot_sha256,
      std::vector<uint8_t> confirmation_sha256,
      JournalCallback callback);
  void AbandonBackupRestore(std::string operation_id, JournalCallback callback);

 private:
  class Backend;

  void OnStorageCommitted(core_service::mojom::EffectEnvelopePtr effect,
                          CompletionCallback callback,
                          bool committed);

  base::SequenceBound<Backend> backend_;
  base::WeakPtrFactory<CoreStorageBroker> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_BROKER_H_
