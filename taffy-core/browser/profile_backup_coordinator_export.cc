// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"
#include "taffy/components/storage/browser/core_storage_broker.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

}  // namespace

void ProfileBackupCoordinator::OnCoreReadyForExport(
    const std::string& operation_id,
    bool ready) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = exports_.find(operation_id);
  if (found == exports_.end()) {
    return;
  }
  ExportOperation& operation = *found->second;
  if (!ready || !manager_ || !manager_->storage_broker_ ||
      operation.phase != ExportOperation::Phase::kStartingCore) {
    FinishExport(operation_id,
                 base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  operation.phase = ExportOperation::Phase::kReadingSnapshot;
  manager_->storage_broker_->ReadBackupSnapshot(
      operation.selection,
      base::BindOnce(&ProfileBackupCoordinator::OnSnapshotRead,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnSnapshotRead(
    const std::string& operation_id,
    storage::backup::BackupSnapshotResult snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = exports_.find(operation_id);
  if (found == exports_.end() ||
      found->second->phase != ExportOperation::Phase::kReadingSnapshot) {
    return;
  }
  found->second->phase = ExportOperation::Phase::kValidatingSnapshot;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ValidateBackupSnapshot, std::move(snapshot),
                     found->second->selection),
      base::BindOnce(
          [](base::WeakPtr<ProfileBackupCoordinator> coordinator,
             std::string id,
             base::expected<ValidatedBackupSnapshot, ProfileBackupError>
                 result) {
            if (!coordinator) {
              return;
            }
            if (!result) {
              coordinator->OnSnapshotValidated(id, nullptr, result.error());
              return;
            }
            coordinator->OnSnapshotValidated(
                id,
                std::make_unique<ValidatedBackupSnapshot>(std::move(*result)),
                std::nullopt);
          },
          weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnSnapshotValidated(
    const std::string& operation_id,
    std::unique_ptr<ValidatedBackupSnapshot> snapshot,
    std::optional<ProfileBackupError> error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = exports_.find(operation_id);
  if (found == exports_.end() ||
      found->second->phase != ExportOperation::Phase::kValidatingSnapshot) {
    return;
  }
  if (!snapshot || error || !manager_) {
    FinishExport(
        operation_id,
        base::unexpected(error.value_or(ProfileBackupError::kCoreUnavailable)));
    return;
  }
  ExportOperation& operation = *found->second;
  operation.snapshot = std::move(snapshot);
  core_mojom::OperationEnvelopePtr envelope = NewBackupOperation(
      manager_->service_generation_, "backup-prepare", operation_id);
  const base::Uuid backup_id = base::Uuid::GenerateRandomV4();
  if (!envelope || !backup_id.is_valid()) {
    FinishExport(operation_id,
                 base::unexpected(ProfileBackupError::kCoreUnavailable));
    return;
  }
  auto request = core_mojom::BackupManifestPrepareRequest::New();
  request->operation = std::move(envelope);
  request->backup_id = "backup-" + backup_id.AsLowercaseString();
  request->source_installation_id = source_installation_id_;
  request->created_at_utc = base::TimeFormatAsIso8601(base::Time::Now());
  request->selection = operation.selection;
  request->records = std::move(operation.snapshot->descriptors);
  operation.phase = ExportOperation::Phase::kPreparingManifest;
  manager_->backup_protocol().PrepareBackupManifest(
      std::move(request),
      base::BindOnce(&ProfileBackupCoordinator::OnManifestPrepared,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void ProfileBackupCoordinator::OnManifestPrepared(
    const std::string& operation_id,
    core_mojom::BackupManifestPrepareResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = exports_.find(operation_id);
  if (found == exports_.end() ||
      found->second->phase != ExportOperation::Phase::kPreparingManifest) {
    if (result) {
      crypto::SecureZeroBuffer(result->manifest_plaintext);
    }
    return;
  }
  ExportOperation& operation = *found->second;
  if (!result ||
      result->status != core_mojom::BackupPlanningStatus::kSucceeded ||
      result->snapshot_sha256.size() != 32u ||
      result->payload_plaintext_bytes !=
          operation.snapshot->payload_plaintext_bytes ||
      result->source_order.size() != operation.snapshot->records.size()) {
    if (result) {
      crypto::SecureZeroBuffer(result->manifest_plaintext);
    }
    FinishExport(operation_id,
                 base::unexpected(ProfileBackupError::kPlanRefused));
    return;
  }
  std::array<uint8_t, 32> snapshot_sha256{};
  std::ranges::copy(result->snapshot_sha256, snapshot_sha256.begin());
  storage::backup::Secret recovery_key = operation.recovery_key;
  crypto::SecureZeroBuffer(operation.recovery_key);
  std::vector<uint8_t> manifest = std::move(result->manifest_plaintext);
  std::vector<uint32_t> source_order = std::move(result->source_order);
  const uint64_t payload_plaintext_bytes = result->payload_plaintext_bytes;
  std::unique_ptr<ValidatedBackupSnapshot> snapshot =
      std::move(operation.snapshot);
  operation.phase = ExportOperation::Phase::kSealingArchive;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&SealValidatedBackupSnapshot, stage_store_, operation_id,
                     SensitiveBackupSecret(recovery_key), std::move(manifest),
                     snapshot_sha256, std::move(source_order),
                     payload_plaintext_bytes, std::move(*snapshot)),
      base::BindOnce(
          [](base::WeakPtr<ProfileBackupCoordinator> coordinator,
             scoped_refptr<storage::backup::BackupArchiveStageStore> store,
             std::string id, ExportResult seal_result) {
            if (coordinator) {
              coordinator->OnExportSealed(id, std::move(seal_result));
              return;
            }
            base::ThreadPool::PostTask(
                FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
                base::BindOnce(
                    [](scoped_refptr<storage::backup::BackupArchiveStageStore>
                           stage,
                       std::string operation) { stage->Abandon(operation); },
                    std::move(store), std::move(id)));
          },
          weak_factory_.GetWeakPtr(), stage_store_, operation_id));
  crypto::SecureZeroBuffer(recovery_key);
}

void ProfileBackupCoordinator::OnExportSealed(const std::string& operation_id,
                                              ExportResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = exports_.find(operation_id);
  if (found == exports_.end() ||
      found->second->phase != ExportOperation::Phase::kSealingArchive) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(
            [](scoped_refptr<storage::backup::BackupArchiveStageStore> store,
               std::string id) { store->Abandon(id); },
            stage_store_, operation_id));
    return;
  }
  FinishExport(operation_id, std::move(result));
}

}  // namespace taffy
