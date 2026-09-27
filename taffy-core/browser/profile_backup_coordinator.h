// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_H_

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "taffy/browser/profile_backup_precommit_cancellation.h"
#include "taffy/browser/profile_backup_restore_target.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/components/storage/browser/encrypted_backup_crypto.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class CoreServiceManager;
class ProfileBackupCoordinatorTestPeer;
struct HashedBackupImport;
struct ValidatedBackupSnapshot;
struct VerifiedBackupImportPayload;

namespace storage::backup {
class BackupArchiveStageStore;
}

enum class ProfileBackupError : uint8_t {
  kInvalidArgument,
  kBusy,
  kCoreUnavailable,
  kStorageUnavailable,
  kSnapshotMismatch,
  kPlanRefused,
  kIoFailure,
  kCancelled,
};

struct PreparedProfileBackupExport {
  uint64_t archive_bytes = 0;
  std::array<uint8_t, 32> snapshot_sha256{};
};

struct ProfileBackupRestorePreview {
  core_service::mojom::BackupRestorePlanResultPtr plan;
  std::string source_installation_id;
  std::string created_at_utc;
  std::vector<core_service::mojom::BackupRecordKind> selection;

  ProfileBackupRestorePreview();
  ProfileBackupRestorePreview(ProfileBackupRestorePreview&&);
  ProfileBackupRestorePreview& operator=(ProfileBackupRestorePreview&&);
  ~ProfileBackupRestorePreview();
};

struct ProfileBackupRestoreCommitReceipt {
  ProfileBackupRestoreCommitObservation physical;
  // False retains quarantine/recovery custody; it is not a repeat-write hint.
  bool source_acknowledged = false;
};

// One source profile's browser-owned backup workflow. Recovery keys and most
// typed record payloads stay in this process. Its live Core owns portable
// restore phase authority while the target remains a separate browser-reserved,
// dormant profile. Only bounded descriptors, canonical manifest bytes, and
// staged hashes normally cross the isolated Core Service contract. Selected
// procedure definitions additionally cross for transient bootstrap-equivalent
// admission before commit authority, never to replace the source catalogue.
// Android sees encrypted detached file descriptors through the stage store;
// the workflow above this coordinator may additionally project a content-free
// bounded summary. Neither layer exposes bindings, digests, paths or payloads.
class ProfileBackupCoordinator {
 public:
  using ExportResult =
      base::expected<PreparedProfileBackupExport, ProfileBackupError>;
  using ExportCallback = base::OnceCallback<void(ExportResult)>;
  using RestorePreviewResult =
      base::expected<ProfileBackupRestorePreview, ProfileBackupError>;
  using RestorePreviewCallback = base::OnceCallback<void(RestorePreviewResult)>;
  using RestoreStageResult = base::expected<void, ProfileBackupError>;
  using RestoreStageCallback = base::OnceCallback<void(RestoreStageResult)>;
  using RestoreCommitResult =
      base::expected<ProfileBackupRestoreCommitReceipt, ProfileBackupError>;
  using RestoreCommitCallback = base::OnceCallback<void(RestoreCommitResult)>;
  using PrecommitCancellationResult =
      base::expected<ProfileBackupPrecommitCancellationReceipt,
                     ProfileBackupError>;
  using PrecommitCancellationCallback =
      base::OnceCallback<void(PrecommitCancellationResult)>;

  ProfileBackupCoordinator(
      CoreServiceManager* manager,
      scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
      std::string source_installation_id);
  ProfileBackupCoordinator(const ProfileBackupCoordinator&) = delete;
  ProfileBackupCoordinator& operator=(const ProfileBackupCoordinator&) = delete;
  ~ProfileBackupCoordinator();

  // Reads one atomic typed snapshot, asks the sandboxed planner for its
  // canonical manifest/order, then seals exactly that order into native stage
  // custody. The callback fires only after local archive authentication.
  void PrepareExport(
      std::string operation_id,
      storage::backup::Secret recovery_key,
      std::vector<core_service::mojom::BackupRecordKind> selection,
      ExportCallback callback);

  // Associates a trusted recovery key with one native import stage before the
  // document provider can write. The returned constant is native-authorized;
  // Kotlin must pass it back exactly when opening the destination FD.
  base::expected<uint64_t, ProfileBackupError> BeginImport(
      std::string operation_id,
      storage::backup::Secret recovery_key);

  // Called only after the encrypted import has been closed and authenticated
  // by BackupArchiveStageStore. This coordinator belongs to the active source
  // profile; the move-only target owner supplies its durably bound distinct
  // identity. The live source Core binds it into the retained portable plan.
  void PlanImportedRestore(std::string operation_id,
                           std::unique_ptr<ProfileBackupRestoreTarget> target,
                           RestorePreviewCallback callback);

  // Consumes exact user confirmation through the source Core, then builds and
  // reads back one isolated typed target stage. Success is still reversible:
  // it neither requests commit authority nor changes selected target records.
  void ConfirmAndStageImportedRestore(std::string operation_id,
                                      std::vector<uint8_t> confirmed_digest,
                                      RestoreStageCallback callback);

  // Runs only after exact confirmation and successful isolated readback.
  // Requests one consumptive commit decision, then hands it to the dormant
  // owner's durable-intent/typed-write path. The candidate remains hidden;
  // success never publishes it, and unknown results cannot trigger replay.
  void CommitStagedImportedRestore(std::string operation_id,
                                   RestoreCommitCallback callback);

  // After the commit callback has drained, closes the dormant writer and its
  // exclusive physical lease without deleting its stage, journal or profile.
  // Completion is the handoff barrier before a separate recovery lifecycle
  // can inspect or resolve the hidden candidate. It grants no new authority
  // and does not require a surviving source Core.
  void CloseRestoreForRecovery(std::string operation_id,
                               RestoreStageCallback callback);

  // Registers one observer before a physical target is transferred into
  // planning. It is fulfilled only by a fully drained precommit cancellation,
  // or settled without a receipt when commit authority is first requested.
  bool ArmPrecommitCancellationReceipt(const std::string& operation_id,
                                       std::string target_profile_id,
                                       PrecommitCancellationCallback callback);

  // Cancels one exact retained plan, verifies isolated target cleanup and
  // destroys its writer before returning a one-use physical cleanup receipt.
  // Admission is refused after commit authority has been requested.
  bool CancelBeforeCommit(const std::string& operation_id);

  // Withdraws further work. Physical target custody remains retryable until
  // isolated-stage cleanup is verified; a retained portable plan stays under
  // workflow interest until exact cancellation succeeds or its source Core
  // disconnects. A precommit callback receives kCancelled exactly once,
  // which reports workflow cancellation, not deletion of the target profile.
  // A dispatched commit instead drains and reports its physical result;
  // cancellation neither guesses a rollback nor removes its retained stage.
  void Cancel(const std::string& operation_id);
  void CancelAll();

 private:
  friend class ProfileBackupCoordinatorTestPeer;

  struct ExportOperation;
  struct ImportOperation;

  void OnCoreReadyForExport(const std::string& operation_id, bool ready);
  void OnSnapshotRead(const std::string& operation_id,
                      storage::backup::BackupSnapshotResult snapshot);
  void OnSnapshotValidated(const std::string& operation_id,
                           std::unique_ptr<ValidatedBackupSnapshot> snapshot,
                           std::optional<ProfileBackupError> error);
  void OnManifestPrepared(
      const std::string& operation_id,
      core_service::mojom::BackupManifestPrepareResultPtr result);
  void OnExportSealed(const std::string& operation_id, ExportResult result);

  void OnCoreReadyForImport(const std::string& operation_id, bool ready);
  void OnImportCoreDisconnected(const std::string& operation_id);
  void OnVerifiedImportRead(
      const std::string& operation_id,
      std::unique_ptr<VerifiedBackupImportPayload> imported,
      std::optional<ProfileBackupError> error);
  void OnManifestInspected(
      const std::string& operation_id,
      core_service::mojom::BackupManifestInspectResultPtr result);
  void OnImportHashed(const std::string& operation_id,
                      std::unique_ptr<HashedBackupImport> imported,
                      std::optional<ProfileBackupError> error);
  static void DeliverRestorePlanReply(
      base::WeakPtr<ProfileBackupCoordinator> coordinator,
      base::WeakPtr<CoreServiceManager> manager,
      std::string operation_id,
      core_service::mojom::BackupRestorePlanResultPtr result);
  static void RetireRestorePlan(
      base::WeakPtr<CoreServiceManager> manager,
      std::string operation_id,
      core_service::mojom::BackupRestoreBindingPtr binding);
  void OnRestorePlanned(const std::string& operation_id,
                        core_service::mojom::BackupRestorePlanResultPtr result);
  void OnRestoreConfirmed(
      const std::string& operation_id,
      core_service::mojom::BackupRestoreStageAuthorizationResultPtr result);
  void OnRestoreStaged(const std::string& operation_id,
                       ProfileBackupRestoreTarget::StageResult result);
  static void DeliverRestoreCommitAuthority(
      base::WeakPtr<ProfileBackupCoordinator> coordinator,
      base::WeakPtr<CoreServiceManager> manager,
      std::string operation_id,
      core_service::mojom::BackupRestoreCommitAuthorizationResultPtr result);
  void OnRestoreCommitAuthorized(
      const std::string& operation_id,
      core_service::mojom::BackupRestoreCommitAuthorizationResultPtr result);
  void OnRestoreCommitted(const std::string& operation_id,
                          ProfileBackupRestoreTarget::CommitResult result);
  void OnRestoreCommitReported(
      const std::string& operation_id,
      core_service::mojom::BackupRestoreProtocolResultPtr result);
  void FinishRestoreCommit(const std::string& operation_id,
                           bool source_acknowledged);
  void OnRestoreClosedForRecovery(const std::string& operation_id, bool closed);
  void BeginImportCancellation(const std::string& operation_id);
  void ContinueImportCancellation(const std::string& operation_id);
  void BeginTargetCleanup(const std::string& operation_id);
  void OnTargetCleaned(const std::string& operation_id, bool cleaned);
  void MaybeFinishImportCancellation(const std::string& operation_id);
  void OnImportCancelled(
      const std::string& operation_id,
      core_service::mojom::BackupRestoreProtocolResultPtr result);
  void FinishExport(const std::string& operation_id, ExportResult result);
  void FinishImport(const std::string& operation_id,
                    RestorePreviewResult result);

  const base::WeakPtr<CoreServiceManager> manager_;
  const scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_;
  const std::string source_installation_id_;
  base::flat_map<std::string, std::unique_ptr<ExportOperation>> exports_;
  base::flat_map<std::string, std::unique_ptr<ImportOperation>> imports_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileBackupCoordinator> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_H_
