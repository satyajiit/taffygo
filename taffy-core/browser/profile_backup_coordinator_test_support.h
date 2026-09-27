// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_TEST_SUPPORT_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_TEST_SUPPORT_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/functional/callback.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_restore_target.h"

namespace taffy {

// The sole friend used by coordinator tests. It places a real import at an
// otherwise asynchronous protocol phase, then delivers synthetic replies and
// authority through the same private handlers production callbacks use.
// Physical results come from the separately controlled target adapter below;
// actual Rust and physical-stage evidence belongs to their own suites.
class ProfileBackupCoordinatorTestPeer final {
 public:
  static ProfileBackupPrecommitCancellationReceipt
  MintPrecommitCancellationReceiptForTesting(
      std::string operation_id,
      std::string source_profile_id,
      std::string target_profile_id,
      core_service::mojom::BackupRestoreBindingPtr binding) {
    return ProfileBackupPrecommitCancellationReceipt(
        std::move(operation_id), std::move(source_profile_id),
        std::move(target_profile_id), std::move(binding));
  }

  static BackupRestoreCallback PendingPlanReply(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);

  static bool SetPlanningRestore(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id,
      std::unique_ptr<ProfileBackupRestoreTarget> target,
      ProfileBackupCoordinator::RestorePreviewCallback callback);

  static bool SetReadyRestore(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id,
      std::unique_ptr<ProfileBackupRestoreTarget> target,
      core_service::mojom::BackupRestorePlanResultPtr plan,
      base::File plaintext_payload);

  static void DeliverRestorePlan(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id,
      core_service::mojom::BackupRestorePlanResultPtr result);
  static bool DeliverExactStageAuthorization(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static bool MarkStaged(ProfileBackupCoordinator& coordinator,
                         const std::string& operation_id);
  static bool RemoveStageAuthorizationForTesting(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static std::vector<core_service::mojom::SkillRecordPtr> StagedProcedures(
      const ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static bool DeliverExactCommitAuthorization(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static CoreBackupProtocol::CommitAuthorizationCallback PendingCommitReply(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static core_service::mojom::BackupRestoreCommitAuthorizationResultPtr
  ExactCommitReply(const ProfileBackupCoordinator& coordinator,
                   const std::string& operation_id);
  static void DeliverCommitReport(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id,
      core_service::mojom::BackupRestoreProtocolStatus status);
  static bool IsHiddenReview(const ProfileBackupCoordinator& coordinator,
                             const std::string& operation_id);
  static bool IsCommitRecoveryRequired(
      const ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static core_service::mojom::OperationEnvelopePtr ConfirmationOperation(
      const ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static void DeliverImportFailure(ProfileBackupCoordinator& coordinator,
                                   const std::string& operation_id,
                                   ProfileBackupError error);
  static void DeliverCancellation(
      ProfileBackupCoordinator& coordinator,
      const std::string& operation_id,
      core_service::mojom::BackupRestoreProtocolStatus status);

  static bool HasImport(const ProfileBackupCoordinator& coordinator,
                        const std::string& operation_id);
  static bool HoldsWorkflowInterest(const ProfileBackupCoordinator& coordinator,
                                    const std::string& operation_id);
  static bool IsConfirming(const ProfileBackupCoordinator& coordinator,
                           const std::string& operation_id);
  static bool IsStaging(const ProfileBackupCoordinator& coordinator,
                        const std::string& operation_id);
  static bool IsCancelling(const ProfileBackupCoordinator& coordinator,
                           const std::string& operation_id);
  static bool IsPortableCancellationInFlight(
      const ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
  static bool IsTargetCleanupInFlight(
      const ProfileBackupCoordinator& coordinator,
      const std::string& operation_id);
};

// Observable state for a test adapter at the production restore-target seam.
// The state must outlive the move-only owner returned by TakeOwner().
class ProfileBackupRestoreTargetTestState final {
 public:
  explicit ProfileBackupRestoreTargetTestState(std::string target_profile_id);
  ProfileBackupRestoreTargetTestState(
      const ProfileBackupRestoreTargetTestState&) = delete;
  ProfileBackupRestoreTargetTestState& operator=(
      const ProfileBackupRestoreTargetTestState&) = delete;
  ~ProfileBackupRestoreTargetTestState();

  std::unique_ptr<ProfileBackupRestoreTarget> TakeOwner();
  void SucceedStageInline();
  void FailStageInline(ProfileBackupRestoreTargetError error);
  void CompleteCleanupInline(bool cleaned);
  void CompleteStage(ProfileBackupRestoreTarget::StageResult result);
  void CompleteCommit(ProfileBackupRestoreTarget::CommitResult result);
  void CompleteCloseForRecovery(bool closed);
  void CompleteCleanup(bool cleaned);

  int stage_calls() const { return stage_calls_; }
  int cleanup_calls() const { return cleanup_calls_; }
  int commit_calls() const { return commit_calls_; }
  int close_calls() const { return close_calls_; }
  bool stage_pending() const { return static_cast<bool>(stage_callback_); }
  bool cleanup_pending() const { return static_cast<bool>(cleanup_callback_); }
  bool commit_pending() const { return static_cast<bool>(commit_callback_); }
  bool owner_live() const { return owner_live_; }
  bool staged_payload_was_valid() const { return staged_payload_was_valid_; }
  const core_service::mojom::BackupRestorePlanResult* staged_plan() const {
    return staged_plan_.get();
  }
  const core_service::mojom::BackupRestoreStageAuthorization*
  staged_authorization() const {
    return staged_authorization_.get();
  }

 private:
  class Target;

  void RecordStage(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreStageAuthorizationPtr authorization,
      base::File plaintext_payload,
      ProfileBackupRestoreTarget::StageCallback callback);
  void RecordCleanup(ProfileBackupRestoreTarget::CleanupCallback callback);
  void RecordCloseForRecovery(
      ProfileBackupRestoreTarget::CleanupCallback callback);
  void RecordCommit(
      core_service::mojom::BackupRestorePlanResultPtr plan,
      core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
      ProfileBackupRestoreTarget::CommitCallback callback);
  void OwnerDestroyed();

  const std::string target_profile_id_;
  bool owner_taken_ = false;
  bool owner_live_ = false;
  int stage_calls_ = 0;
  int cleanup_calls_ = 0;
  int commit_calls_ = 0;
  int close_calls_ = 0;
  bool staged_payload_was_valid_ = false;
  bool stage_inline_ = false;
  std::optional<ProfileBackupRestoreTargetError> stage_inline_error_;
  std::optional<bool> cleanup_inline_result_;
  core_service::mojom::BackupRestorePlanResultPtr staged_plan_;
  core_service::mojom::BackupRestoreStageAuthorizationPtr staged_authorization_;
  ProfileBackupRestoreTarget::StageCallback stage_callback_;
  ProfileBackupRestoreTarget::CommitCallback commit_callback_;
  ProfileBackupRestoreTarget::CleanupCallback cleanup_callback_;
  ProfileBackupRestoreTarget::CleanupCallback close_callback_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_TEST_SUPPORT_H_
