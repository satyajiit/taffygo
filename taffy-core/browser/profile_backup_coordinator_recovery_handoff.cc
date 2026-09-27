// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"

namespace taffy {

void ProfileBackupCoordinator::CloseRestoreForRecovery(
    std::string operation_id,
    RestoreStageCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    return;
  }
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kInvalidArgument));
    return;
  }
  auto& operation = *found->second;
  if ((operation.phase != ImportOperation::Phase::kHiddenReview &&
       operation.phase != ImportOperation::Phase::kCommitRecoveryRequired) ||
      !operation.commit_authority_requested || !operation.commit_result ||
      operation.commit_callback || !operation.target ||
      operation.target_close_requested) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kBusy));
    return;
  }
  operation.target_close_requested = true;
  operation.close_callback = std::move(callback);
  operation.target->CloseForRecovery(
      base::BindOnce(&ProfileBackupCoordinator::OnRestoreClosedForRecovery,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnRestoreClosedForRecovery(
    const std::string& operation_id,
    bool closed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() || !found->second->close_callback) {
    return;
  }
  auto& operation = *found->second;
  auto callback = std::move(operation.close_callback);
  if (!closed) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kStorageUnavailable));
    return;
  }
  // CloseForRecovery's callback is the blocking destruction/lease barrier,
  // not merely cancellation of UI interest or a queued destructor. Dropping
  // this inert adapter cannot remove the retained candidate or replay work.
  operation.target.reset();
  operation.workflow_interest.reset();
  if (operation.cancellation_requested) {
    imports_.erase(found);
  }
  std::move(callback).Run(base::ok());
}

}  // namespace taffy
