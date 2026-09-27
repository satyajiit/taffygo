// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"

namespace taffy {
namespace {

constexpr size_t kMaximumCoordinatorOperations = 4u;

bool AllZero(const storage::backup::Secret& key) {
  return std::ranges::all_of(key, [](uint8_t byte) { return byte == 0u; });
}

bool CanonicalUuid(std::string_view value) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(value);
  return parsed.is_valid() && parsed.AsLowercaseString() == value &&
         value[14] == '4' &&
         (value[19] == '8' || value[19] == '9' || value[19] == 'a' ||
          value[19] == 'b');
}

void AbandonStage(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id) {
  stage_store->Abandon(operation_id);
}

void AbandonAllStages(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store) {
  stage_store->AbandonAll();
}

void PostAbandon(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&AbandonStage, std::move(stage_store),
                     std::move(operation_id)));
}

}  // namespace

ProfileBackupRestorePreview::ProfileBackupRestorePreview() = default;
ProfileBackupRestorePreview::ProfileBackupRestorePreview(
    ProfileBackupRestorePreview&&) = default;
ProfileBackupRestorePreview& ProfileBackupRestorePreview::operator=(
    ProfileBackupRestorePreview&&) = default;
ProfileBackupRestorePreview::~ProfileBackupRestorePreview() = default;

ProfileBackupCoordinator::ProfileBackupCoordinator(
    CoreServiceManager* manager,
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string source_installation_id)
    : manager_(manager ? manager->weak_factory_.GetWeakPtr()
                       : base::WeakPtr<CoreServiceManager>()),
      stage_store_(std::move(stage_store)),
      source_installation_id_(std::move(source_installation_id)) {
  CHECK(manager_);
  CHECK(stage_store_);
}

ProfileBackupCoordinator::~ProfileBackupCoordinator() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  for (auto& [operation_id, operation] : imports_) {
    if (operation->plan && operation->plan->binding &&
        !operation->commit_authority_requested &&
        !operation->portable_cancellation_complete) {
      RetireRestorePlan(manager_, operation_id,
                        operation->plan->binding.Clone());
    }
  }
  exports_.clear();
  imports_.clear();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&AbandonAllStages, stage_store_));
}

void ProfileBackupCoordinator::PrepareExport(
    std::string operation_id,
    storage::backup::Secret recovery_key,
    std::vector<core_service::mojom::BackupRecordKind> selection,
    ExportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool invalid = !manager_ || !CanonicalUuid(source_installation_id_) ||
                       !IsValidProfileBackupOperationId(operation_id) ||
                       AllZero(recovery_key) ||
                       !IsSupportedBackupManifestSelection(selection);
  if (invalid) {
    crypto::SecureZeroBuffer(recovery_key);
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kInvalidArgument));
    return;
  }
  if (exports_.contains(operation_id) || imports_.contains(operation_id) ||
      exports_.size() + imports_.size() >= kMaximumCoordinatorOperations) {
    crypto::SecureZeroBuffer(recovery_key);
    std::move(callback).Run(base::unexpected(ProfileBackupError::kBusy));
    return;
  }
  auto operation = std::make_unique<ExportOperation>();
  operation->recovery_key = recovery_key;
  crypto::SecureZeroBuffer(recovery_key);
  operation->selection = std::move(selection);
  operation->callback = std::move(callback);
  exports_.emplace(operation_id, std::move(operation));
  manager_->PrepareForCoreApi(
      base::BindOnce(&ProfileBackupCoordinator::OnCoreReadyForExport,
                     weak_factory_.GetWeakPtr(), operation_id));
}

base::expected<uint64_t, ProfileBackupError>
ProfileBackupCoordinator::BeginImport(std::string operation_id,
                                      storage::backup::Secret recovery_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!manager_ || !IsValidProfileBackupOperationId(operation_id) ||
      AllZero(recovery_key)) {
    crypto::SecureZeroBuffer(recovery_key);
    return base::unexpected(ProfileBackupError::kInvalidArgument);
  }
  if (exports_.contains(operation_id) || imports_.contains(operation_id) ||
      exports_.size() + imports_.size() >= kMaximumCoordinatorOperations) {
    crypto::SecureZeroBuffer(recovery_key);
    return base::unexpected(ProfileBackupError::kBusy);
  }
  if (!stage_store_->PrepareImport(operation_id, recovery_key)) {
    crypto::SecureZeroBuffer(recovery_key);
    return base::unexpected(ProfileBackupError::kStorageUnavailable);
  }
  crypto::SecureZeroBuffer(recovery_key);
  imports_.emplace(operation_id, std::make_unique<ImportOperation>());
  return stage_store_->MaximumArchiveBytes();
}

void ProfileBackupCoordinator::PlanImportedRestore(
    std::string operation_id,
    std::unique_ptr<ProfileBackupRestoreTarget> target,
    RestorePreviewCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (!callback) {
    return;
  }
  if (!manager_ || found == imports_.end() || !target ||
      !CanonicalUuid(target->target_profile_id()) ||
      target->target_profile_id().size() >
          core_service::mojom::kMaxBackupIdBytes ||
      (found != imports_.end() &&
       !found->second->target_profile_id.empty() &&
       found->second->target_profile_id != target->target_profile_id())) {
    std::move(callback).Run(
        base::unexpected(ProfileBackupError::kInvalidArgument));
    return;
  }
  ImportOperation& operation = *found->second;
  if (operation.phase != ImportOperation::Phase::kAwaitingCopy) {
    std::move(callback).Run(base::unexpected(ProfileBackupError::kBusy));
    return;
  }
  operation.source_profile_id = manager_->browser_profile_id();
  operation.target_profile_id = target->target_profile_id();
  operation.phase = ImportOperation::Phase::kStartingCore;
  operation.target = std::move(target);
  operation.callback = std::move(callback);
  manager_->PrepareForCoreApi(
      base::BindOnce(&ProfileBackupCoordinator::OnCoreReadyForImport,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::FinishExport(const std::string& operation_id,
                                            ExportResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = exports_.find(operation_id);
  if (found == exports_.end()) {
    return;
  }
  ExportCallback callback = std::move(found->second->callback);
  exports_.erase(found);
  if (!result) {
    PostAbandon(stage_store_, operation_id);
  }
  std::move(callback).Run(std::move(result));
}

void ProfileBackupCoordinator::FinishImport(const std::string& operation_id,
                                            RestorePreviewResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = imports_.find(operation_id);
  if (found == imports_.end()) {
    return;
  }
  if (!result && (found->second->target || found->second->plan) &&
      !(found->second->cancellation_requested &&
        found->second->portable_cancellation_complete &&
        found->second->target_cleanup_complete)) {
    // A rejected preview does not transfer or discharge physical custody.
    // Keep the owner retryable, including failures before a plan exists.
    ImportOperation& operation = *found->second;
    operation.cancellation_error = result.error();
    operation.cancellation_requested = true;
    if (!operation.plan) {
      operation.portable_cancellation_complete = true;
    }
    operation.phase = ImportOperation::Phase::kCancellingRestore;
    Cancel(operation_id);
    return;
  }
  RestorePreviewCallback callback = std::move(found->second->callback);
  if (!result) {
    imports_.erase(found);
    PostAbandon(stage_store_, operation_id);
  }
  if (callback) {
    std::move(callback).Run(std::move(result));
  }
}

}  // namespace taffy
