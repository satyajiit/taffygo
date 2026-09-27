// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator_test_support.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"

namespace taffy {

namespace core_mojom = core_service::mojom;

BackupRestoreCallback ProfileBackupCoordinatorTestPeer::PendingPlanReply(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  return base::BindOnce(&ProfileBackupCoordinator::DeliverRestorePlanReply,
                        coordinator.weak_factory_.GetWeakPtr(),
                        coordinator.manager_, operation_id);
}

bool ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id,
    std::unique_ptr<ProfileBackupRestoreTarget> target,
    ProfileBackupCoordinator::RestorePreviewCallback callback) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end() || !coordinator.manager_ || !target) {
    return false;
  }
  auto interest =
      coordinator.manager_->backup_protocol().AcquireWorkflowInterest(
          base::BindOnce(&ProfileBackupCoordinator::OnImportCoreDisconnected,
                         coordinator.weak_factory_.GetWeakPtr(), operation_id));
  if (!interest) {
    return false;
  }
  auto& operation = *found->second;
  operation.phase =
      ProfileBackupCoordinator::ImportOperation::Phase::kPlanningRestore;
  operation.target_profile_id = target->target_profile_id();
  operation.target = std::move(target);
  operation.inspection = core_mojom::BackupManifestInspectResult::New();
  operation.inspection->status = core_mojom::BackupPlanningStatus::kSucceeded;
  operation.inspection->selection = {
      core_mojom::BackupRecordKind::kLibraryEntry};
  operation.inspection->snapshot_sha256.assign(32u, 2u);
  operation.workflow_interest.emplace(std::move(*interest));
  operation.callback = std::move(callback);
  return true;
}

bool ProfileBackupCoordinatorTestPeer::SetReadyRestore(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id,
    std::unique_ptr<ProfileBackupRestoreTarget> target,
    core_mojom::BackupRestorePlanResultPtr plan,
    base::File plaintext_payload) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end() || !coordinator.manager_ || !target ||
      !plan || !plan->binding || !plaintext_payload.IsValid()) {
    return false;
  }
  auto interest =
      coordinator.manager_->backup_protocol().AcquireWorkflowInterest(
          base::BindOnce(&ProfileBackupCoordinator::OnImportCoreDisconnected,
                         coordinator.weak_factory_.GetWeakPtr(), operation_id));
  if (!interest) {
    return false;
  }
  auto& operation = *found->second;
  operation.phase = ProfileBackupCoordinator::ImportOperation::Phase::kReady;
  operation.target_profile_id = target->target_profile_id();
  operation.target = std::move(target);
  operation.plan = std::move(plan);
  operation.imported = std::make_unique<HashedBackupImport>();
  operation.imported->verified.plaintext_payload = std::move(plaintext_payload);
  operation.workflow_interest.emplace(std::move(*interest));
  return true;
}

void ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id,
    core_mojom::BackupRestorePlanResultPtr result) {
  coordinator.OnRestorePlanned(operation_id, std::move(result));
}

bool ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end()) {
    return false;
  }
  const auto& operation = *found->second;
  if (!operation.confirmation_operation || !operation.plan ||
      !operation.plan->binding) {
    return false;
  }
  auto result = core_mojom::BackupRestoreStageAuthorizationResult::New();
  result->operation = operation.confirmation_operation.Clone();
  result->status = core_mojom::BackupRestoreProtocolStatus::kSucceeded;
  result->authorization = core_mojom::BackupRestoreStageAuthorization::New(
      operation.plan->binding.Clone(),
      operation.confirmation_operation.Clone());
  coordinator.OnRestoreConfirmed(operation_id, std::move(result));
  return true;
}

core_mojom::OperationEnvelopePtr
ProfileBackupCoordinatorTestPeer::ConfirmationOperation(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end() ||
      !found->second->confirmation_operation) {
    return nullptr;
  }
  return found->second->confirmation_operation.Clone();
}

void ProfileBackupCoordinatorTestPeer::DeliverImportFailure(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id,
    ProfileBackupError error) {
  coordinator.OnVerifiedImportRead(operation_id, nullptr, error);
}

void ProfileBackupCoordinatorTestPeer::DeliverCancellation(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id,
    core_mojom::BackupRestoreProtocolStatus status) {
  auto result = core_mojom::BackupRestoreProtocolResult::New();
  result->status = status;
  coordinator.OnImportCancelled(operation_id, std::move(result));
}

bool ProfileBackupCoordinatorTestPeer::HasImport(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  return coordinator.imports_.contains(operation_id);
}

bool ProfileBackupCoordinatorTestPeer::HoldsWorkflowInterest(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->workflow_interest.has_value();
}

bool ProfileBackupCoordinatorTestPeer::IsConfirming(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->phase == ProfileBackupCoordinator::ImportOperation::
                                     Phase::kConfirmingRestore;
}

bool ProfileBackupCoordinatorTestPeer::IsStaging(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->phase ==
             ProfileBackupCoordinator::ImportOperation::Phase::kStagingRestore;
}

bool ProfileBackupCoordinatorTestPeer::IsCancelling(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->phase == ProfileBackupCoordinator::ImportOperation::
                                     Phase::kCancellingRestore;
}

bool ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->cancellation_rpc_in_flight;
}

bool ProfileBackupCoordinatorTestPeer::IsTargetCleanupInFlight(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->target_cleanup_in_flight;
}

class ProfileBackupRestoreTargetTestState::Target final
    : public ProfileBackupRestoreTarget {
 public:
  Target(ProfileBackupRestoreTargetTestState* state, std::string profile_id)
      : state_(state), profile_id_(std::move(profile_id)) {
    CHECK(state_);
  }
  ~Target() override { state_->OwnerDestroyed(); }

  const std::string& target_profile_id() const override { return profile_id_; }

  void Stage(core_mojom::BackupRestorePlanResultPtr plan,
             core_mojom::BackupRestoreStageAuthorizationPtr authorization,
             base::File plaintext_payload,
             StageCallback callback) override {
    state_->RecordStage(std::move(plan), std::move(authorization),
                        std::move(plaintext_payload), std::move(callback));
  }

  void Abandon(CleanupCallback callback) override {
    state_->RecordCleanup(std::move(callback));
  }

  void CloseForRecovery(CleanupCallback callback) override {
    state_->RecordCloseForRecovery(std::move(callback));
  }

  void Commit(core_mojom::BackupRestorePlanResultPtr plan,
              core_mojom::BackupRestoreCommitAuthorizationPtr authorization,
              CommitCallback callback) override {
    state_->RecordCommit(std::move(plan), std::move(authorization),
                         std::move(callback));
  }

 private:
  const raw_ptr<ProfileBackupRestoreTargetTestState> state_;
  const std::string profile_id_;
};

ProfileBackupRestoreTargetTestState::ProfileBackupRestoreTargetTestState(
    std::string target_profile_id)
    : target_profile_id_(std::move(target_profile_id)) {
  CHECK(!target_profile_id_.empty());
}

ProfileBackupRestoreTargetTestState::~ProfileBackupRestoreTargetTestState() {
  CHECK(!owner_live_);
}

std::unique_ptr<ProfileBackupRestoreTarget>
ProfileBackupRestoreTargetTestState::TakeOwner() {
  CHECK(!owner_taken_);
  owner_taken_ = true;
  owner_live_ = true;
  return std::make_unique<Target>(this, target_profile_id_);
}

void ProfileBackupRestoreTargetTestState::SucceedStageInline() {
  CHECK_EQ(stage_calls_, 0);
  stage_inline_ = true;
  stage_inline_error_.reset();
}

void ProfileBackupRestoreTargetTestState::FailStageInline(
    ProfileBackupRestoreTargetError error) {
  CHECK_EQ(stage_calls_, 0);
  stage_inline_ = true;
  stage_inline_error_ = error;
}

void ProfileBackupRestoreTargetTestState::CompleteCleanupInline(bool cleaned) {
  CHECK_EQ(cleanup_calls_, 0);
  cleanup_inline_result_ = cleaned;
}

void ProfileBackupRestoreTargetTestState::CompleteStage(
    ProfileBackupRestoreTarget::StageResult result) {
  CHECK(stage_callback_);
  std::move(stage_callback_).Run(std::move(result));
}

void ProfileBackupRestoreTargetTestState::CompleteCleanup(bool cleaned) {
  CHECK(cleanup_callback_);
  std::move(cleanup_callback_).Run(cleaned);
}

void ProfileBackupRestoreTargetTestState::CompleteCommit(
    ProfileBackupRestoreTarget::CommitResult result) {
  CHECK(commit_callback_);
  std::move(commit_callback_).Run(std::move(result));
}

void ProfileBackupRestoreTargetTestState::CompleteCloseForRecovery(
    bool closed) {
  CHECK(close_callback_);
  std::move(close_callback_).Run(closed);
}

void ProfileBackupRestoreTargetTestState::RecordCloseForRecovery(
    ProfileBackupRestoreTarget::CleanupCallback callback) {
  CHECK(!close_callback_);
  ++close_calls_;
  close_callback_ = std::move(callback);
}

void ProfileBackupRestoreTargetTestState::RecordCommit(
    core_mojom::BackupRestorePlanResultPtr plan,
    core_mojom::BackupRestoreCommitAuthorizationPtr authorization,
    ProfileBackupRestoreTarget::CommitCallback callback) {
  CHECK(!commit_callback_);
  CHECK(plan);
  CHECK(authorization);
  ++commit_calls_;
  commit_callback_ = std::move(callback);
}

void ProfileBackupRestoreTargetTestState::RecordStage(
    core_mojom::BackupRestorePlanResultPtr plan,
    core_mojom::BackupRestoreStageAuthorizationPtr authorization,
    base::File plaintext_payload,
    ProfileBackupRestoreTarget::StageCallback callback) {
  CHECK(!stage_callback_);
  ++stage_calls_;
  staged_plan_ = std::move(plan);
  staged_authorization_ = std::move(authorization);
  staged_payload_was_valid_ = plaintext_payload.IsValid();
  if (stage_inline_) {
    if (stage_inline_error_) {
      std::move(callback).Run(base::unexpected(*stage_inline_error_));
    } else {
      std::move(callback).Run(ProfileBackupRestoreStageObservation{});
    }
    return;
  }
  stage_callback_ = std::move(callback);
}

void ProfileBackupRestoreTargetTestState::RecordCleanup(
    ProfileBackupRestoreTarget::CleanupCallback callback) {
  CHECK(!cleanup_callback_);
  ++cleanup_calls_;
  if (cleanup_inline_result_) {
    const bool cleaned = *cleanup_inline_result_;
    std::move(callback).Run(cleaned);
    return;
  }
  cleanup_callback_ = std::move(callback);
}

void ProfileBackupRestoreTargetTestState::OwnerDestroyed() {
  stage_callback_.Reset();
  commit_callback_.Reset();
  cleanup_callback_.Reset();
  close_callback_.Reset();
  owner_live_ = false;
}

}  // namespace taffy
