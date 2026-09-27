// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "mojo/public/cpp/bindings/clone_traits.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"

namespace taffy {
namespace {
namespace wire = core_service::mojom;
}

bool ProfileBackupCoordinatorTestPeer::MarkStaged(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end() || !coordinator.manager_ ||
      !found->second->plan || !found->second->plan->binding) {
    return false;
  }
  auto& operation = *found->second;
  operation.stage_authorization = wire::BackupRestoreStageAuthorization::New(
      operation.plan->binding.Clone(),
      NewBackupOperation(coordinator.manager_->service_generation(),
                         "test-stage", operation_id));
  operation.phase = ProfileBackupCoordinator::ImportOperation::Phase::kStaged;
  return true;
}

bool ProfileBackupCoordinatorTestPeer::RemoveStageAuthorizationForTesting(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end() ||
      found->second->phase !=
          ProfileBackupCoordinator::ImportOperation::Phase::kStaged) {
    return false;
  }
  found->second->stage_authorization.reset();
  return true;
}

std::vector<wire::SkillRecordPtr>
ProfileBackupCoordinatorTestPeer::StagedProcedures(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end()) {
    return {};
  }
  return mojo::Clone(found->second->staged_procedures);
}

bool ProfileBackupCoordinatorTestPeer::DeliverExactCommitAuthorization(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto result = ExactCommitReply(coordinator, operation_id);
  if (!result) {
    return false;
  }
  coordinator.OnRestoreCommitAuthorized(operation_id, std::move(result));
  return true;
}

CoreBackupProtocol::CommitAuthorizationCallback
ProfileBackupCoordinatorTestPeer::PendingCommitReply(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  return base::BindOnce(
      &ProfileBackupCoordinator::DeliverRestoreCommitAuthority,
      coordinator.weak_factory_.GetWeakPtr(), coordinator.manager_,
      operation_id);
}

wire::BackupRestoreCommitAuthorizationResultPtr
ProfileBackupCoordinatorTestPeer::ExactCommitReply(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  if (found == coordinator.imports_.end() || !found->second->commit_operation ||
      !found->second->plan || !found->second->plan->binding) {
    return nullptr;
  }
  const auto& operation = *found->second;
  auto result = wire::BackupRestoreCommitAuthorizationResult::New();
  result->operation = operation.commit_operation.Clone();
  result->status = wire::BackupRestoreProtocolStatus::kSucceeded;
  result->authorization = wire::BackupRestoreCommitAuthorization::New(
      operation.plan->binding.Clone(), operation.commit_operation.Clone());
  return result;
}

void ProfileBackupCoordinatorTestPeer::DeliverCommitReport(
    ProfileBackupCoordinator& coordinator,
    const std::string& operation_id,
    wire::BackupRestoreProtocolStatus status) {
  auto result = wire::BackupRestoreProtocolResult::New();
  result->status = status;
  coordinator.OnRestoreCommitReported(operation_id, std::move(result));
}

bool ProfileBackupCoordinatorTestPeer::IsHiddenReview(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->phase ==
             ProfileBackupCoordinator::ImportOperation::Phase::kHiddenReview;
}

bool ProfileBackupCoordinatorTestPeer::IsCommitRecoveryRequired(
    const ProfileBackupCoordinator& coordinator,
    const std::string& operation_id) {
  auto found = coordinator.imports_.find(operation_id);
  return found != coordinator.imports_.end() &&
         found->second->phase == ProfileBackupCoordinator::ImportOperation::
                                     Phase::kCommitRecoveryRequired;
}

}  // namespace taffy
