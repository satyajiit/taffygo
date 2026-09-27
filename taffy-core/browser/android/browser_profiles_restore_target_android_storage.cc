// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <utility>

#include "taffy/browser/android/browser_profiles_restore_target_android.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"

namespace taffy {
namespace {

using CreationError = storage::backup::DormantBackupRestoreTargetError;
using CommitError = storage::backup::DormantBackupRestoreCommitError;
using CommitOutcome = storage::backup::DormantBackupRestoreCommitOutcome;
using StageError = storage::backup::DormantBackupRestoreStageError;
using StorageTarget = storage::backup::DormantBackupRestoreTarget;
using TargetError = ProfileBackupRestoreTargetError;
using InitializationError = BackupRestoreDormantTargetError;

InitializationError MapCreationError(CreationError error) {
  switch (error) {
    case CreationError::kInvalidTarget:
      return InitializationError::kProfileStateRefused;
    case CreationError::kTargetOccupied:
      return InitializationError::kTargetOccupied;
    case CreationError::kStorageUnavailable:
      return InitializationError::kStorageUnavailable;
  }
  return InitializationError::kStorageUnavailable;
}

TargetError MapStageError(StageError error) {
  switch (error) {
    case StageError::kInvalidAuthorization:
    case StageError::kPlanRefused:
    case StageError::kUnsupportedRecord:
      return TargetError::kInvalidAuthorization;
    case StageError::kAuthorizationConsumed:
    case StageError::kStagingPathOccupied:
      return TargetError::kBusy;
    case StageError::kTargetChanged:
      return TargetError::kUnavailable;
    case StageError::kPayloadMismatch:
      return TargetError::kPayloadMismatch;
    case StageError::kStorageUnavailable:
    case StageError::kCleanupFailed:
      return TargetError::kStorageUnavailable;
  }
  return TargetError::kStorageUnavailable;
}

bool ConsumedAuthorization(StageError error) {
  return error != StageError::kInvalidAuthorization;
}

TargetError MapCommitError(CommitError error) {
  switch (error) {
    case CommitError::kInvalidAuthorization:
      return TargetError::kInvalidAuthorization;
    case CommitError::kAuthorizationConsumed:
    case CommitError::kCommitNotPrepared:
      return TargetError::kBusy;
    case CommitError::kStageUnavailable:
    case CommitError::kTargetChanged:
    case CommitError::kTargetNotPristine:
      return TargetError::kUnavailable;
    case CommitError::kWitnessMismatch:
      return TargetError::kPayloadMismatch;
    case CommitError::kStorageUnavailable:
      return TargetError::kStorageUnavailable;
  }
  return TargetError::kStorageUnavailable;
}

}  // namespace

class BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::
    StorageOwner {
 public:
  std::unique_ptr<StorageTarget> target;
};

BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::
    BlockingOwner()
    : storage_owner_(std::make_unique<StorageOwner>()) {}

BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::
    ~BlockingOwner() = default;

BrowserProfilesRestoreLifecycle::DormantTargetState::InitializationResult
BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::Initialize(
    base::FilePath target_profile_path,
    std::string target_profile_id) {
  if (storage_owner_->target) {
    return base::unexpected(InitializationError::kBusy);
  }
  auto created = StorageTarget::Create(target_profile_path,
                                       std::move(target_profile_id), false);
  if (!created.has_value()) {
    return base::unexpected(MapCreationError(created.error()));
  }
  storage_owner_->target = std::move(*created);
  return base::ok();
}

BrowserProfilesRestoreLifecycle::DormantTargetState::StageExecutionResult
BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::Stage(
    core_service::mojom::BackupRestorePlanResultPtr plan,
    core_service::mojom::BackupRestoreStageAuthorizationPtr authorization,
    base::File plaintext_payload,
    std::array<uint8_t, 32> expected_snapshot_sha256,
    uint64_t expected_record_count) {
  if (!storage_owner_->target) {
    return {.result = base::unexpected(TargetError::kUnavailable)};
  }
  auto staged = storage_owner_->target->StageAuthorized(
      std::move(plan), std::move(authorization), std::move(plaintext_payload));
  if (!staged.has_value()) {
    return {
        .result = base::unexpected(MapStageError(staged.error())),
        .authorization_consumed = ConsumedAuthorization(staged.error()),
    };
  }
  if (staged->snapshot_sha256 != expected_snapshot_sha256 ||
      staged->record_count != expected_record_count) {
    return {
        .result = base::unexpected(TargetError::kPayloadMismatch),
        .authorization_consumed = true,
    };
  }
  ProfileBackupRestoreStageObservation observation;
  observation.procedures = std::move(staged->procedures);
  return {
      .result = std::move(observation),
      .authorization_consumed = true,
  };
}

BrowserProfilesRestoreLifecycle::DormantTargetState::CommitPreparationResult
BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::
    PrepareCommitWitness() {
  if (!storage_owner_->target) {
    return base::unexpected(TargetError::kUnavailable);
  }
  auto witness = storage_owner_->target->PrepareCommitWitness();
  if (!witness.has_value()) {
    return base::unexpected(MapCommitError(witness.error()));
  }
  if (!*witness) {
    return base::unexpected(TargetError::kStorageUnavailable);
  }
  return std::move(*witness);
}

core_service::mojom::BackupRestoreCommitOutcome
BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::Commit(
    core_service::mojom::BackupRestorePlanResultPtr plan,
    core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
    core_service::mojom::BackupRestoreCandidateWitnessPtr witness) {
  if (!storage_owner_->target) {
    return core_service::mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
  }
  auto outcome = storage_owner_->target->CommitAuthorized(
      std::move(plan), std::move(authorization), std::move(witness));
  if (!outcome.has_value() || *outcome == CommitOutcome::kOutcomeUnknown) {
    return core_service::mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
  }
  return core_service::mojom::BackupRestoreCommitOutcome::kCommitted;
}

bool BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::
    Abandon() {
  if (!storage_owner_ || !storage_owner_->target) {
    return false;
  }
  return storage_owner_->target->AbandonStage().has_value();
}

bool BrowserProfilesRestoreLifecycle::DormantTargetState::BlockingOwner::
    CloseForRecovery(bool retain_stage_for_recovery) {
  if (!storage_owner_) {
    return false;
  }
  if (retain_stage_for_recovery && storage_owner_->target &&
      !storage_owner_->target->PrepareCommitWitness().has_value()) {
    return false;
  }
  storage_owner_.reset();
  return true;
}

}  // namespace taffy
