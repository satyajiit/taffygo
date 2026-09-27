// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_H_

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/task/sequenced_task_runner.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/backup_recovery_key_codec.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class CoreServiceManager;
class ProfileBackupWorkflowTestPeer;
class ProfileBackupWorkflowRestoreTestPeer;
class ProfileBackupWorkflowPresentationTestPeer;
class ProfileBackupWorkflowIoHandle;

// Browser-owned profile/window custody for backup creation and encrypted SAF
// staging. Portable planning stays in the source Core. Android receives only
// detached encrypted file descriptors plus opaque review handles and bounded,
// content-free restore summaries; paths, digests and Core ids remain native.
class ProfileBackupWorkflow {
 public:
  using WindowToken = uint64_t;

  enum class KeyMode : uint8_t { kCreate, kRestore };
  enum class KeyAcceptance : uint8_t { kAccepted, kRefused, kUnavailable };
  enum class ExportPreparationStatus : uint8_t {
    kReady,
    kRefused,
    kUnavailable,
  };

  struct ExportPreparation {
    ExportPreparationStatus status = ExportPreparationStatus::kUnavailable;
    uint64_t archive_bytes = 0;
  };

  using RestoreReviewToken = uint64_t;

  enum class RestorePreparationStatus : uint8_t {
    kReady,
    kRefused,
    kUnavailable,
  };
  enum class RestoreStageStatus : uint8_t {
    kStaged,
    kRefused,
    kUnavailable,
  };
  enum class RestoreCommitStatus : uint8_t {
    kHiddenCandidate,
    kDefinitelyNotCommitted,
    kRecoveryRequired,
    kRefused,
    kUnavailable,
    // Internal Android bookkeeping only. The existing UI result remains
    // unavailable while the exact precommit receipt drains physical cleanup.
    kPrecommitCleanupRequired,
  };
  enum class RestoreResolutionStatus : uint8_t {
    kPublished,
    kVerifiedDeleted,
    kDefinitelyNotCompleted,
    kRecoveryRequired,
    kRefused,
    kUnavailable,
  };

  struct RestoreClassSummary {
    core_service::mojom::BackupRecordKind kind;
    // Indexed by the generated closed BackupRestoreAction wire value.
    std::array<uint32_t, 6> action_counts{};
  };

  struct RestorePreparation {
    RestorePreparationStatus status = RestorePreparationStatus::kUnavailable;
    RestoreReviewToken review_token = 0;
    std::u16string target_profile_label;
    std::vector<RestoreClassSummary> selected_classes;
    bool has_conflicts = false;
    bool can_stage = false;
  };

  using ExportCallback = base::OnceCallback<void(ExportPreparation)>;
  using RestorePreparationCallback =
      base::OnceCallback<void(RestorePreparation)>;
  using RestorePresentationCallback =
      base::OnceCallback<void(BackupRestoreRecoveryPresentationResult)>;
  using RestorePresentationWriter = base::OnceCallback<void(
      std::string reservation_id,
      std::u16string target_profile_label,
      std::vector<core_service::mojom::BackupRecordKind> original_selection,
      core_service::mojom::BackupRestorePlanResultPtr exact_plan,
      RestorePresentationCallback callback)>;
  using RestoreStageCallback = base::OnceCallback<void(RestoreStageStatus)>;
  using RestoreCommitCallback = base::OnceCallback<void(RestoreCommitStatus)>;
  using RestoreResolutionCallback =
      base::OnceCallback<void(RestoreResolutionStatus)>;
  using PrecommitCancellationCallback =
      ProfileBackupCoordinator::PrecommitCancellationCallback;

  ProfileBackupWorkflow(CoreServiceManager* manager,
                        std::string source_installation_id);
  ProfileBackupWorkflow(const ProfileBackupWorkflow&) = delete;
  ProfileBackupWorkflow& operator=(const ProfileBackupWorkflow&) = delete;
  ~ProfileBackupWorkflow();

  // Installs the asynchronously-created, exact profile staging owner once.
  // Until then all key/session admission fails unavailable.
  bool InstallStageStore(
      scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store);

  WindowToken RegisterWindow();
  bool ActivateWindow(WindowToken window);
  void DeactivateWindow(WindowToken window);
  void UnregisterWindow(WindowToken window);
  // Read-only membership used by the Android restart-review owner. It grants
  // no workflow operation and reconstructs no prior restore authority.
  bool IsActiveWindow(WindowToken window) const;

  std::optional<std::string> OpenRecoveryKeySession(WindowToken window,
                                                    KeyMode mode);
  std::optional<storage::backup::RecoveryKeyText> TakeGeneratedKeyForDisplay(
      WindowToken window,
      const std::string& operation_id);
  KeyAcceptance ConfirmKeyRetained(WindowToken window,
                                   const std::string& operation_id);
  KeyAcceptance AcceptEnteredKey(WindowToken window,
                                 const std::string& operation_id,
                                 base::span<const char16_t> entered_key);

  // Consumes the retained create key. Selection is revalidated and sorted by
  // the generated closed enum before it reaches the coordinator.
  bool PrepareExport(
      WindowToken window,
      const std::string& operation_id,
      std::vector<core_service::mojom::BackupRecordKind> selection,
      ExportCallback callback);
  std::optional<uint64_t> MaximumImportBytes(
      WindowToken window,
      const std::string& operation_id) const;

  // These methods are safe on Android's I/O dispatcher. Bulk verification
  // runs through a detached handle whose only shared state is revocation and
  // stage custody; completion rejoins this exact live workflow before success.
  base::File OpenEncryptedArchiveRead(WindowToken window,
                                      const std::string& operation_id);
  base::File OpenExportReadback(WindowToken window,
                                const std::string& operation_id,
                                uint64_t expected_archive_bytes);
  std::unique_ptr<ProfileBackupWorkflowIoHandle> AcquireExportVerification(
      WindowToken window,
      const std::string& operation_id);
  storage::backup::BackupStageStatus CompleteExportVerification(
      WindowToken window,
      const std::string& operation_id,
      const ProfileBackupWorkflowIoHandle& handle);
  base::File OpenEncryptedImport(WindowToken window,
                                 const std::string& operation_id,
                                 uint64_t maximum_archive_bytes);
  std::unique_ptr<ProfileBackupWorkflowIoHandle> AcquireImportInspection(
      WindowToken window,
      const std::string& operation_id,
      uint64_t actual_archive_bytes);
  storage::backup::BackupStageStatus CompleteImportInspection(
      WindowToken window,
      const std::string& operation_id,
      const ProfileBackupWorkflowIoHandle& handle);

  // Begins the Android-owned hidden-profile creation. The callback is retained
  // against the exact window/operation until AttachImportedRestoreTarget or a
  // fail-closed completion. No path, profile id, digest or Core identity is
  // projected through the result.
  bool BeginImportedRestorePreparation(WindowToken window,
                                       const std::string& operation_id,
                                       std::u16string target_profile_label,
                                       RestorePreparationCallback callback);
  bool IsImportedRestorePreparationCurrent(
      WindowToken window,
      const std::string& operation_id) const;
  // Returns true once the coordinator owns either the target or the one-use
  // precommit-cancellation receipt. A synchronous planning refusal may run
  // both callbacks before this method returns, but a false return never does.
  bool AttachImportedRestoreTarget(
      WindowToken window,
      const std::string& operation_id,
      std::string reservation_id,
      std::unique_ptr<ProfileBackupRestoreTarget> target,
      RestorePresentationWriter presentation_writer,
      PrecommitCancellationCallback cancellation_callback);
  void FailImportedRestorePreparation(WindowToken window,
                                      const std::string& operation_id,
                                      RestorePreparationStatus status);
  bool ConfirmAndStageImportedRestore(WindowToken window,
                                      RestoreReviewToken review_token,
                                      RestoreStageCallback callback);
  bool CommitStagedImportedRestore(WindowToken window,
                                   RestoreReviewToken review_token,
                                   RestoreCommitCallback callback);
  std::optional<std::string> BeginImportedRestoreResolution(
      WindowToken window,
      RestoreReviewToken review_token,
      core_service::mojom::BackupRestoreResolutionChoice choice,
      RestoreResolutionCallback callback);
  void CompleteImportedRestoreResolution(WindowToken window,
                                         RestoreReviewToken review_token,
                                         RestoreResolutionStatus status);
  void AbandonOperation(WindowToken window, const std::string& operation_id);

  bool ready_for_testing() const;
  size_t operation_count_for_testing() const;

 private:
  friend class ProfileBackupWorkflowTestPeer;
  friend class ProfileBackupWorkflowRestoreTestPeer;
  friend class ProfileBackupWorkflowPresentationTestPeer;

  struct WindowState {
    bool active = false;
  };
  struct Operation;

  void OnExportPrepared(const std::string& operation_id,
                        ExportCallback callback,
                        ProfileBackupCoordinator::ExportResult result);
  void OnImportedRestorePlanned(
      const std::string& operation_id,
      ProfileBackupCoordinator::RestorePreviewResult result);
  void OnImportedRestorePresentationPersisted(
      const std::string& operation_id,
      BackupRestoreRecoveryPresentationResult result);
  void OnImportedRestoreStaged(
      const std::string& operation_id,
      ProfileBackupCoordinator::RestoreStageResult result);
  void OnImportedRestoreCommitted(
      const std::string& operation_id,
      ProfileBackupCoordinator::RestoreCommitResult result);
  void OnImportedRestoreClosed(
      const std::string& operation_id,
      RestoreCommitStatus projected_status,
      ProfileBackupCoordinator::RestoreStageResult result);
  // The Locked suffix is a claim about the caller, and the annotation is what
  // makes the compiler check it at every call site rather than only inside the
  // definition. base::Lock is not reentrant, so a caller that forgot would
  // deadlock rather than race — which is the failure this catches first.
  Operation* FindRestoreReviewLocked(WindowToken window,
                                     RestoreReviewToken review_token)
      EXCLUSIVE_LOCKS_REQUIRED(state_lock_);
  const Operation* FindRestoreReviewLocked(WindowToken window,
                                           RestoreReviewToken review_token)
      const EXCLUSIVE_LOCKS_REQUIRED(state_lock_);
  bool IsExportReady(WindowToken window,
                     const std::string& operation_id,
                     std::optional<uint64_t> expected_bytes) const;
  bool IsImportReady(WindowToken window,
                     const std::string& operation_id,
                     std::optional<uint64_t> expected_bytes) const;
  bool WithdrawOperation(WindowToken window, const std::string& operation_id);
  void ScheduleStageAbandon(std::string operation_id);
  void CancelCoordinatorOperation(std::string operation_id);
  scoped_refptr<storage::backup::BackupArchiveStageStore> StageStore() const;

  const raw_ptr<CoreServiceManager> manager_;
  const std::string source_installation_id_;
  scoped_refptr<base::SequencedTaskRunner> owner_task_runner_;
  std::unique_ptr<ProfileBackupCoordinator> coordinator_;
  base::flat_map<WindowToken, WindowState> windows_;
  WindowToken next_window_token_ = 1;
  RestoreReviewToken next_restore_review_token_ = 1;

  mutable base::Lock state_lock_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_
      GUARDED_BY(state_lock_);
  base::flat_map<std::string, std::unique_ptr<Operation>> operations_
      GUARDED_BY(state_lock_);

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtr<ProfileBackupWorkflow> weak_this_;
  base::WeakPtrFactory<ProfileBackupWorkflow> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_H_
