// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator.h"

#include <utility>

// The coordinator header only forward-declares CoreServiceManager on purpose;
// this file calls browser_profile_id() on it, so it needs the definition.
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"

namespace taffy {

bool ProfileBackupCoordinator::ArmPrecommitCancellationReceipt(
    const std::string& operation_id,
    std::string target_profile_id,
    PrecommitCancellationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || target_profile_id.empty() || !manager_ ||
      manager_->browser_profile_id().empty()) {
    return false;
  }
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    return false;
  }
  ImportOperation& operation = *found->second;
  if (operation.precommit_cancellation_callback ||
      operation.commit_authority_requested || operation.commit_dispatched ||
      operation.phase != ImportOperation::Phase::kAwaitingCopy) {
    return false;
  }
  operation.source_profile_id = manager_->browser_profile_id();
  operation.target_profile_id = std::move(target_profile_id);
  operation.precommit_cancellation_callback = std::move(callback);
  return true;
}

bool ProfileBackupCoordinator::CancelBeforeCommit(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    return false;
  }
  ImportOperation& operation = *found->second;
  if (!operation.precommit_cancellation_callback ||
      operation.commit_authority_requested || operation.commit_dispatched ||
      operation.phase == ImportOperation::Phase::kAuthorizingCommit ||
      operation.phase == ImportOperation::Phase::kCommittingRestore ||
      operation.phase == ImportOperation::Phase::kReportingCommit ||
      operation.phase == ImportOperation::Phase::kHiddenReview ||
      operation.phase == ImportOperation::Phase::kCommitRecoveryRequired) {
    return false;
  }
  operation.cancellation_error = ProfileBackupError::kCancelled;
  Cancel(operation_id);
  return true;
}

}  // namespace taffy
