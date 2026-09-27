// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

base::expected<HashedBackupImport, ProfileBackupError> ReadAndHashImport(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id,
    core_mojom::BackupManifestInspectResultPtr inspection) {
  auto imported =
      ReadVerifiedBackupImport(std::move(stage_store), operation_id);
  if (!imported) {
    return base::unexpected(imported.error());
  }
  return HashBackupImportPayload(std::move(*imported), *inspection);
}

}  // namespace

void ProfileBackupCoordinator::OnCoreReadyForImport(
    const std::string& operation_id,
    bool ready) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kStartingCore) {
    return;
  }
  ImportOperation& operation = *found->second;
  if (!ready || !manager_) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  if (manager_->browser_profile_id().empty() ||
      operation.target_profile_id == manager_->browser_profile_id()) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kPlanRefused));
    return;
  }
  auto interest = manager_->backup_protocol().AcquireWorkflowInterest(
      base::BindOnce(&ProfileBackupCoordinator::OnImportCoreDisconnected,
                     weak_factory_.GetWeakPtr(), operation_id));
  if (!interest) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  operation.workflow_interest.emplace(std::move(*interest));
  operation.phase = ImportOperation::Phase::kReadingVerifiedStage;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadVerifiedBackupImport, stage_store_, operation_id),
      base::BindOnce(
          [](base::WeakPtr<ProfileBackupCoordinator> coordinator,
             std::string id,
             base::expected<VerifiedBackupImportPayload, ProfileBackupError>
                 result) {
            if (!coordinator) {
              return;
            }
            if (!result) {
              coordinator->OnVerifiedImportRead(id, nullptr, result.error());
              return;
            }
            coordinator->OnVerifiedImportRead(
                id,
                std::make_unique<VerifiedBackupImportPayload>(
                    std::move(*result)),
                std::nullopt);
          },
          weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnImportCoreDisconnected(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    return;
  }
  if (found->second->commit_authority_requested) {
    // The physical owner, not this Core incarnation, owns an already-started
    // terminal. A durable intent with no reply remains recovery custody.
    auto& operation = *found->second;
    if (operation.phase == ImportOperation::Phase::kCommittingRestore &&
        operation.commit_dispatched) {
      return;
    }
    if (!operation.commit_result) {
      operation.commit_result =
          base::unexpected(ProfileBackupRestoreTargetError::kUnavailable);
    }
    FinishRestoreCommit(operation_id, false);
    return;
  }
  found->second->portable_cancellation_complete = true;
  found->second->cancellation_rpc_in_flight = false;
  found->second->cancellation_requested = true;
  found->second->cancellation_error = ProfileBackupError::kCoreUnavailable;
  found->second->phase = ImportOperation::Phase::kCancellingRestore;
  Cancel(operation_id);
}

void ProfileBackupCoordinator::OnVerifiedImportRead(
    const std::string& operation_id,
    std::unique_ptr<VerifiedBackupImportPayload> imported,
    std::optional<ProfileBackupError> error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kReadingVerifiedStage) {
    return;
  }
  if (!imported || error || !manager_) {
    FinishImport(
        operation_id,
        base::unexpected(error.value_or(ProfileBackupError::kCoreUnavailable)));
    return;
  }
  core_mojom::OperationEnvelopePtr envelope = NewBackupOperation(
      manager_->service_generation_, "backup-inspect", operation_id);
  if (!envelope) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  auto request = core_mojom::BackupManifestInspectRequest::New();
  request->operation = std::move(envelope);
  request->manifest_plaintext =
      std::move(imported->verified.manifest_plaintext);
  found->second->phase = ImportOperation::Phase::kInspectingManifest;
  manager_->backup_protocol().InspectBackupManifest(
      std::move(request),
      base::BindOnce(&ProfileBackupCoordinator::OnManifestInspected,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnManifestInspected(
    const std::string& operation_id,
    core_mojom::BackupManifestInspectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kInspectingManifest) {
    return;
  }
  if (!result ||
      result->status != core_mojom::BackupPlanningStatus::kSucceeded ||
      !IsSupportedBackupManifestSelection(result->selection)) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kPlanRefused));
    return;
  }
  ImportOperation& operation = *found->second;
  operation.inspection = std::move(result);
  operation.phase = ImportOperation::Phase::kHashingPayload;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadAndHashImport, stage_store_, operation_id,
                     operation.inspection.Clone()),
      base::BindOnce(
          [](base::WeakPtr<ProfileBackupCoordinator> coordinator,
             std::string id,
             base::expected<HashedBackupImport, ProfileBackupError> result) {
            if (!coordinator) {
              return;
            }
            if (!result) {
              coordinator->OnImportHashed(id, nullptr, result.error());
              return;
            }
            coordinator->OnImportHashed(
                id, std::make_unique<HashedBackupImport>(std::move(*result)),
                std::nullopt);
          },
          weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnImportHashed(
    const std::string& operation_id,
    std::unique_ptr<HashedBackupImport> imported,
    std::optional<ProfileBackupError> error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kHashingPayload) {
    return;
  }
  if (!imported || error || !manager_) {
    FinishImport(
        operation_id,
        base::unexpected(error.value_or(ProfileBackupError::kCoreUnavailable)));
    return;
  }
  ImportOperation& operation = *found->second;
  operation.imported = std::move(imported);
  core_mojom::OperationEnvelopePtr envelope = NewBackupOperation(
      manager_->service_generation_, "backup-restore-plan", operation_id);
  if (!envelope) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  auto request = core_mojom::BackupRestorePlanRequest::New();
  request->operation = std::move(envelope);
  request->manifest_plaintext =
      std::move(operation.imported->verified.manifest_plaintext);
  request->staged_records = std::move(operation.imported->staged_records);
  request->target = core_mojom::BackupRestoreTarget::New(
      core_mojom::BackupRestoreTargetKind::kNewRegularProfile,
      operation.target_profile_id);
  operation.phase = ImportOperation::Phase::kPlanningRestore;
  manager_->backup_protocol().PlanBackupRestore(
      std::move(request),
      base::BindOnce(&ProfileBackupCoordinator::DeliverRestorePlanReply,
                     weak_factory_.GetWeakPtr(), manager_, operation_id));
}

// This delivery closure is deliberately not weak-bound as a member receiver.
// If the browser workflow owner disappears while planning is in flight, a
// successful reply still mints a portable plan and must be retired by its
// exact binding instead of being silently dropped.
void ProfileBackupCoordinator::DeliverRestorePlanReply(
    base::WeakPtr<ProfileBackupCoordinator> coordinator,
    base::WeakPtr<CoreServiceManager> manager,
    std::string operation_id,
    core_mojom::BackupRestorePlanResultPtr result) {
  if (coordinator) {
    coordinator->OnRestorePlanned(operation_id, std::move(result));
    return;
  }
  if (!result ||
      result->status != core_mojom::BackupPlanningStatus::kSucceeded ||
      !result->binding) {
    return;
  }
  RetireRestorePlan(std::move(manager), std::move(operation_id),
                    result->binding.Clone());
}

void ProfileBackupCoordinator::RetireRestorePlan(
    base::WeakPtr<CoreServiceManager> manager,
    std::string operation_id,
    core_mojom::BackupRestoreBindingPtr binding) {
  if (!manager || !binding) {
    return;
  }
  // Independent protocol custody survives destruction of this coordinator,
  // including a cancellation already in flight. It joins that exact request
  // and retries a refused result without relying on eventual idle teardown.
  manager->backup_protocol().RetireBackupRestorePlan(std::move(operation_id),
                                                     std::move(binding));
}

void ProfileBackupCoordinator::OnRestorePlanned(
    const std::string& operation_id,
    core_mojom::BackupRestorePlanResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end() ||
      found->second->phase != ImportOperation::Phase::kPlanningRestore) {
    return;
  }
  ImportOperation& operation = *found->second;
  // The protocol has validated any successful reply against the request.
  // Even if the local preview checks below refuse it, retain its binding so
  // the portable plan is cancelled before this operation releases custody.
  if (result &&
      result->status == core_mojom::BackupPlanningStatus::kSucceeded &&
      result->binding) {
    operation.plan = result.Clone();
  }
  if (operation.cancellation_requested) {
    if (!operation.plan) {
      // A refused/malformed reply minted no portable binding. Settle only the
      // protocol side of the existing cancellation; this late completion is
      // not a fresh request to retry a failed physical target cleanup.
      operation.portable_cancellation_complete = true;
    }
    // A successful reply may still be locally unusable for presentation, but
    // its exact binding must be retired. ContinueImportCancellation never
    // begins physical cleanup; only the original or a fresh explicit Cancel
    // may do that.
    ContinueImportCancellation(operation_id);
    return;
  }
  if (!manager_ || !result ||
      result->status != core_mojom::BackupPlanningStatus::kSucceeded ||
      !result->target ||
      result->target->kind !=
          core_mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      result->target->profile_id != operation.target_profile_id ||
      !result->binding ||
      result->binding->owner_profile_id != manager_->browser_profile_id() ||
      !result->binding->target ||
      result->binding->target->kind != result->target->kind ||
      result->binding->target->profile_id != operation.target_profile_id ||
      result->has_conflicts ||
      result->snapshot_sha256 != operation.inspection->snapshot_sha256 ||
      result->entries.size() != operation.inspection->record_count) {
    FinishImport(operation_id,
                 base::unexpected(ProfileBackupError::kPlanRefused));
    return;
  }
  ProfileBackupRestorePreview preview;
  preview.plan = result.Clone();
  preview.source_installation_id = operation.inspection->source_installation_id;
  preview.created_at_utc = operation.inspection->created_at_utc;
  preview.selection = operation.inspection->selection;
  operation.plan = std::move(result);
  operation.phase = ImportOperation::Phase::kReady;
  FinishImport(operation_id, std::move(preview));
}

}  // namespace taffy
