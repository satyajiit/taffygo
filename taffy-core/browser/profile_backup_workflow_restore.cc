// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <ranges>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_internal.h"

namespace taffy {
namespace {

constexpr size_t kMaximumTargetLabelCodeUnits = 40u;

bool IsValidTargetLabel(std::u16string_view label) {
  return !label.empty() && label.size() <= kMaximumTargetLabelCodeUnits &&
         base::TrimWhitespace(label, base::TRIM_ALL) == label &&
         std::ranges::none_of(label, [](char16_t character) {
           return character < 0x20u || character == 0x7fu;
         });
}

ProfileBackupWorkflow::RestorePreparationStatus PreparationError(
    ProfileBackupError error) {
  switch (error) {
    case ProfileBackupError::kInvalidArgument:
    case ProfileBackupError::kSnapshotMismatch:
    case ProfileBackupError::kPlanRefused:
      return ProfileBackupWorkflow::RestorePreparationStatus::kRefused;
    case ProfileBackupError::kBusy:
    case ProfileBackupError::kCoreUnavailable:
    case ProfileBackupError::kStorageUnavailable:
    case ProfileBackupError::kIoFailure:
    case ProfileBackupError::kCancelled:
      return ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable;
  }
  return ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable;
}

}  // namespace

bool ProfileBackupWorkflow::BeginImportedRestorePreparation(
    WindowToken window,
    const std::string& operation_id,
    std::u16string target_profile_label,
    RestorePreparationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !coordinator_ || !IsActiveWindow(window) ||
      !IsValidTargetLabel(target_profile_label)) {
    return false;
  }
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kImportVerified ||
      found->second->restore_preparation_callback) {
    return false;
  }
  found->second->phase = Operation::Phase::kRestoreTargetPreparing;
  found->second->target_profile_label = std::move(target_profile_label);
  found->second->restore_preparation_callback = std::move(callback);
  return true;
}

bool ProfileBackupWorkflow::IsImportedRestorePreparationCurrent(
    WindowToken window,
    const std::string& operation_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::AutoLock guard(state_lock_);
  const auto found = operations_.find(operation_id);
  return found != operations_.end() && found->second->owner == window &&
         found->second->phase == Operation::Phase::kRestoreTargetPreparing &&
         found->second->restore_preparation_callback;
}

bool ProfileBackupWorkflow::AttachImportedRestoreTarget(
    WindowToken window,
    const std::string& operation_id,
    std::string reservation_id,
    std::unique_ptr<ProfileBackupRestoreTarget> target,
    RestorePresentationWriter presentation_writer,
    PrecommitCancellationCallback cancellation_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!coordinator_ || !target || reservation_id.empty() ||
      !presentation_writer || !cancellation_callback) {
    return false;
  }
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() || found->second->owner != window ||
        found->second->phase != Operation::Phase::kRestoreTargetPreparing) {
      return false;
    }
  }
  if (!coordinator_->ArmPrecommitCancellationReceipt(
          operation_id, target->target_profile_id(),
          std::move(cancellation_callback))) {
    return false;
  }
  bool state_still_current = false;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    state_still_current =
        found != operations_.end() && found->second->owner == window &&
        found->second->phase == Operation::Phase::kRestoreTargetPreparing;
    if (state_still_current) {
      found->second->reservation_id = std::move(reservation_id);
      found->second->restore_presentation_writer =
          std::move(presentation_writer);
      found->second->phase = Operation::Phase::kRestorePlanning;
    }
  }
  if (!state_still_current) {
    coordinator_->CancelBeforeCommit(operation_id);
    // ArmPrecommitCancellationReceipt transferred one-use cleanup custody.
    // Its callback may already have run synchronously; false is reserved for
    // paths on which no callback can run and the caller still owns cleanup.
    return true;
  }
  coordinator_->PlanImportedRestore(
      operation_id, std::move(target),
      base::BindOnce(&ProfileBackupWorkflow::OnImportedRestorePlanned,
                     weak_factory_.GetWeakPtr(), operation_id));
  return true;
}

void ProfileBackupWorkflow::FailImportedRestorePreparation(
    WindowToken window,
    const std::string& operation_id,
    RestorePreparationStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestorePreparationCallback callback;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() || found->second->owner != window ||
        (found->second->phase != Operation::Phase::kRestoreTargetPreparing &&
         found->second->phase != Operation::Phase::kRestorePlanning &&
         found->second->phase !=
             Operation::Phase::kRestorePresentationPersisting)) {
      return;
    }
    found->second->phase = Operation::Phase::kPrecommitCleanupRequired;
    callback = std::move(found->second->restore_preparation_callback);
  }
  if (callback) {
    std::move(callback).Run(RestorePreparation{.status = status});
  }
}

void ProfileBackupWorkflow::OnImportedRestorePlanned(
    const std::string& operation_id,
    ProfileBackupCoordinator::RestorePreviewResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestorePreparationCallback callback;
  RestorePresentationWriter writer;
  RestorePreparation response;
  std::string reservation_id;
  std::u16string target_profile_label;
  std::vector<core_service::mojom::BackupRecordKind> original_selection;
  core_service::mojom::BackupRestorePlanResultPtr exact_plan;
  bool cancel_precommit = false;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        found->second->phase != Operation::Phase::kRestorePlanning) {
      return;
    }
    Operation& operation = *found->second;
    if (!result) {
      callback = std::move(operation.restore_preparation_callback);
      operation.phase = Operation::Phase::kPrecommitCleanupRequired;
      response.status = PreparationError(result.error());
    } else {
      auto expected =
          result->plan
              ? backup_restore_recovery_presentation_internal::Build(
                    operation.target_profile_label, result->selection,
                    *result->plan)
              : BackupRestoreRecoveryPresentationResult(base::unexpected(
                    BackupRestoreProfileRegistryError::kInvalidArgument));
      if (!expected || operation.reservation_id.empty() ||
          !operation.restore_presentation_writer) {
        callback = std::move(operation.restore_preparation_callback);
        operation.phase = Operation::Phase::kPrecommitCleanupRequired;
        response.status = RestorePreparationStatus::kRefused;
        cancel_precommit = true;
      } else {
        operation.expected_restore_presentation = std::move(*expected);
        operation.phase = Operation::Phase::kRestorePresentationPersisting;
        writer = std::move(operation.restore_presentation_writer);
        reservation_id = operation.reservation_id;
        target_profile_label = operation.target_profile_label;
        original_selection = std::move(result->selection);
        exact_plan = std::move(result->plan);
      }
    }
  }
  if (writer) {
    std::move(writer).Run(
        std::move(reservation_id), std::move(target_profile_label),
        std::move(original_selection), std::move(exact_plan),
        base::BindOnce(
            &ProfileBackupWorkflow::OnImportedRestorePresentationPersisted,
            weak_factory_.GetWeakPtr(), operation_id));
    return;
  }
  base::WeakPtr<ProfileBackupWorkflow> weak_this = weak_factory_.GetWeakPtr();
  if (callback) {
    std::move(callback).Run(std::move(response));
  }
  if (weak_this && cancel_precommit) {
    weak_this->CancelCoordinatorOperation(operation_id);
  }
}

void ProfileBackupWorkflow::OnImportedRestorePresentationPersisted(
    const std::string& operation_id,
    BackupRestoreRecoveryPresentationResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RestorePreparationCallback callback;
  RestorePreparation response;
  bool cancel_precommit = false;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        found->second->phase !=
            Operation::Phase::kRestorePresentationPersisting) {
      return;
    }
    Operation& operation = *found->second;
    callback = std::move(operation.restore_preparation_callback);
    const bool exact = result && operation.expected_restore_presentation &&
                       *result == *operation.expected_restore_presentation;
    operation.expected_restore_presentation.reset();
    if (!exact || next_restore_review_token_ == 0u) {
      operation.phase = Operation::Phase::kPrecommitCleanupRequired;
      response.status = RestorePreparationStatus::kUnavailable;
      cancel_precommit = true;
    } else {
      const RestoreReviewToken token = next_restore_review_token_++;
      operation.restore_review_token = token;
      operation.confirmation_digest.assign(result->confirmation_sha256.begin(),
                                           result->confirmation_sha256.end());
      operation.restore_can_stage = result->can_stage;
      operation.phase = Operation::Phase::kRestorePreview;
      response.status = RestorePreparationStatus::kReady;
      response.review_token = token;
      response.target_profile_label = result->target_profile_label;
      response.has_conflicts = result->has_conflicts;
      response.can_stage = result->can_stage;
      response.selected_classes.reserve(result->selected_classes.size());
      for (const auto& row : result->selected_classes) {
        response.selected_classes.push_back(
            {.kind = row.kind, .action_counts = row.action_counts});
      }
    }
  }
  base::WeakPtr<ProfileBackupWorkflow> weak_this = weak_factory_.GetWeakPtr();
  if (callback) {
    std::move(callback).Run(std::move(response));
  }
  if (weak_this && cancel_precommit) {
    weak_this->CancelCoordinatorOperation(operation_id);
  }
}

}  // namespace taffy
