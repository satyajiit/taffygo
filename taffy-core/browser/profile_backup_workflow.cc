// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_workflow.h"

#include <algorithm>
#include <ranges>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_backup_workflow_internal.h"

namespace taffy {
namespace {

constexpr size_t kMaximumOperations = 4u;

bool AllZero(const storage::backup::Secret& key) {
  return std::ranges::all_of(key, [](uint8_t byte) { return byte == 0u; });
}

ProfileBackupWorkflow::ExportPreparationStatus MapExportError(
    ProfileBackupError error) {
  switch (error) {
    case ProfileBackupError::kInvalidArgument:
    case ProfileBackupError::kSnapshotMismatch:
    case ProfileBackupError::kPlanRefused:
      return ProfileBackupWorkflow::ExportPreparationStatus::kRefused;
    case ProfileBackupError::kBusy:
    case ProfileBackupError::kCoreUnavailable:
    case ProfileBackupError::kStorageUnavailable:
    case ProfileBackupError::kIoFailure:
    case ProfileBackupError::kCancelled:
      return ProfileBackupWorkflow::ExportPreparationStatus::kUnavailable;
  }
  return ProfileBackupWorkflow::ExportPreparationStatus::kUnavailable;
}

}  // namespace

ProfileBackupWorkflow::Operation::Operation(WindowToken owner,
                                            Phase initial_phase)
    : owner(owner),
      phase(initial_phase),
      io_state(base::MakeRefCounted<ProfileBackupWorkflowIoState>()) {}

ProfileBackupWorkflow::Operation::~Operation() {
  io_state->Revoke();
  crypto::SecureZeroBuffer(key);
}

ProfileBackupWorkflow::ProfileBackupWorkflow(CoreServiceManager* manager,
                                             std::string source_installation_id)
    : manager_(manager),
      source_installation_id_(std::move(source_installation_id)),
      owner_task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {
  CHECK(manager_);
  CHECK(owner_task_runner_);
  weak_this_ = weak_factory_.GetWeakPtr();
}

ProfileBackupWorkflow::~ProfileBackupWorkflow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store;
  {
    base::AutoLock guard(state_lock_);
    // Revoke every detached I/O handle before the coordinator queues stage
    // cleanup. A late hash/decrypt may finish, but it cannot publish success.
    operations_.clear();
    stage_store = std::move(stage_store_);
  }
  coordinator_.reset();
  if (stage_store) {
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
        base::BindOnce(
            [](scoped_refptr<storage::backup::BackupArchiveStageStore> store) {
              store->AbandonAll();
            },
            std::move(stage_store)));
  }
}

bool ProfileBackupWorkflow::InstallStageStore(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!stage_store || coordinator_) {
    return false;
  }
  {
    base::AutoLock guard(state_lock_);
    if (stage_store_) {
      return false;
    }
    stage_store_ = stage_store;
  }
  coordinator_ = std::make_unique<ProfileBackupCoordinator>(
      manager_, std::move(stage_store), source_installation_id_);
  return true;
}

void ProfileBackupWorkflow::UnregisterWindow(WindowToken window) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!windows_.erase(window)) {
    return;
  }
  std::vector<std::string> operation_ids;
  {
    base::AutoLock guard(state_lock_);
    for (auto iterator = operations_.begin(); iterator != operations_.end();) {
      if (iterator->second->owner != window) {
        ++iterator;
        continue;
      }
      operation_ids.push_back(iterator->first);
      if (iterator->second->phase == Operation::Phase::kRestoreCommitting ||
          iterator->second->phase == Operation::Phase::kRestoreClosing) {
        // A consumptive commit may already be outside this object. Retain its
        // private operation solely to drain the terminal and close the target
        // writer. Its callback is an internal Android bookkeeping continuation;
        // the Android owner has already revoked its Java caller.
        iterator->second->restore_window_detached = true;
        ++iterator;
        continue;
      }
      iterator = operations_.erase(iterator);
    }
  }
  for (const std::string& operation_id : operation_ids) {
    ScheduleStageAbandon(operation_id);
    if (coordinator_) {
      coordinator_->Cancel(operation_id);
    }
  }
}

std::optional<std::string> ProfileBackupWorkflow::OpenRecoveryKeySession(
    WindowToken window,
    KeyMode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!coordinator_ || !IsActiveWindow(window)) {
    return std::nullopt;
  }
  const base::Uuid nonce = base::Uuid::GenerateRandomV4();
  if (!nonce.is_valid()) {
    return std::nullopt;
  }
  const std::string operation_id =
      std::string(mode == KeyMode::kCreate ? "backup-export-"
                                           : "backup-import-") +
      nonce.AsLowercaseString();
  auto operation = std::make_unique<Operation>(
      window, mode == KeyMode::kCreate ? Operation::Phase::kCreateKey
                                       : Operation::Phase::kRestoreKey);
  if (mode == KeyMode::kCreate) {
    operation->key = storage::backup::GenerateSecret();
    if (AllZero(operation->key)) {
      return std::nullopt;
    }
  }
  base::AutoLock guard(state_lock_);
  if (operations_.size() >= kMaximumOperations ||
      operations_.contains(operation_id)) {
    return std::nullopt;
  }
  operations_.emplace(operation_id, std::move(operation));
  return operation_id;
}

std::optional<storage::backup::RecoveryKeyText>
ProfileBackupWorkflow::TakeGeneratedKeyForDisplay(
    WindowToken window,
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveWindow(window)) {
    return std::nullopt;
  }
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kCreateKey ||
      found->second->display_taken) {
    return std::nullopt;
  }
  auto text = storage::backup::FormatRecoveryKeyForDisplay(found->second->key);
  if (text) {
    found->second->display_taken = true;
  }
  return text;
}

ProfileBackupWorkflow::KeyAcceptance ProfileBackupWorkflow::ConfirmKeyRetained(
    WindowToken window,
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!coordinator_ || !IsActiveWindow(window)) {
    return KeyAcceptance::kUnavailable;
  }
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kCreateKey ||
      !found->second->display_taken) {
    return KeyAcceptance::kRefused;
  }
  found->second->phase = Operation::Phase::kCreateConfirmed;
  return KeyAcceptance::kAccepted;
}

ProfileBackupWorkflow::KeyAcceptance ProfileBackupWorkflow::AcceptEnteredKey(
    WindowToken window,
    const std::string& operation_id,
    base::span<const char16_t> entered_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!coordinator_ || !IsActiveWindow(window)) {
    return KeyAcceptance::kUnavailable;
  }
  std::optional<storage::backup::Secret> parsed =
      storage::backup::ParseRecoveryKeyFromInput(entered_key);
  if (!parsed) {
    return KeyAcceptance::kRefused;
  }
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() || found->second->owner != window ||
        found->second->phase != Operation::Phase::kRestoreKey) {
      crypto::SecureZeroBuffer(*parsed);
      return KeyAcceptance::kRefused;
    }
    found->second->phase = Operation::Phase::kImportStarting;
  }
  auto started = coordinator_->BeginImport(operation_id, *parsed);
  crypto::SecureZeroBuffer(*parsed);
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() ||
      found->second->phase != Operation::Phase::kImportStarting) {
    return KeyAcceptance::kUnavailable;
  }
  // base::expected disables its explicit operator bool when the value type is
  // constructible from bool, which uint64_t is. Ask for the value directly.
  if (!started.has_value()) {
    operations_.erase(found);
    return started.error() == ProfileBackupError::kInvalidArgument
               ? KeyAcceptance::kRefused
               : KeyAcceptance::kUnavailable;
  }
  found->second->phase = Operation::Phase::kImportReady;
  found->second->maximum_import_bytes = *started;
  return KeyAcceptance::kAccepted;
}

bool ProfileBackupWorkflow::PrepareExport(
    WindowToken window,
    const std::string& operation_id,
    std::vector<core_service::mojom::BackupRecordKind> selection,
    ExportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !coordinator_ || !IsActiveWindow(window) ||
      !storage::backup::IsSupportedBackupStorageSelection(selection)) {
    return false;
  }
  std::ranges::sort(selection);
  storage::backup::Secret key{};
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() || found->second->owner != window ||
        found->second->phase != Operation::Phase::kCreateConfirmed) {
      return false;
    }
    key = found->second->key;
    crypto::SecureZeroBuffer(found->second->key);
    found->second->phase = Operation::Phase::kExportPreparing;
  }
  coordinator_->PrepareExport(
      operation_id, key, std::move(selection),
      base::BindOnce(&ProfileBackupWorkflow::OnExportPrepared,
                     weak_factory_.GetWeakPtr(), operation_id,
                     std::move(callback)));
  crypto::SecureZeroBuffer(key);
  return true;
}

void ProfileBackupWorkflow::OnExportPrepared(
    const std::string& operation_id,
    ExportCallback callback,
    ProfileBackupCoordinator::ExportResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ExportPreparation response;
  {
    base::AutoLock guard(state_lock_);
    auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        found->second->phase != Operation::Phase::kExportPreparing) {
      return;
    }
    if (result) {
      found->second->phase = Operation::Phase::kExportReady;
      found->second->archive_bytes = result->archive_bytes;
      response.status = ExportPreparationStatus::kReady;
      response.archive_bytes = result->archive_bytes;
    } else {
      response.status = MapExportError(result.error());
      operations_.erase(found);
    }
  }
  std::move(callback).Run(response);
}

std::optional<uint64_t> ProfileBackupWorkflow::MaximumImportBytes(
    WindowToken window,
    const std::string& operation_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kImportReady ||
      found->second->maximum_import_bytes == 0u) {
    return std::nullopt;
  }
  return found->second->maximum_import_bytes;
}

bool ProfileBackupWorkflow::ready_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return coordinator_ != nullptr;
}

size_t ProfileBackupWorkflow::operation_count_for_testing() const {
  base::AutoLock guard(state_lock_);
  return operations_.size();
}

bool ProfileBackupWorkflow::IsExportReady(
    WindowToken window,
    const std::string& operation_id,
    std::optional<uint64_t> expected_bytes) const {
  base::AutoLock guard(state_lock_);
  const auto found = operations_.find(operation_id);
  return found != operations_.end() && found->second->owner == window &&
         found->second->phase == Operation::Phase::kExportReady &&
         (!expected_bytes || *expected_bytes == found->second->archive_bytes);
}

bool ProfileBackupWorkflow::IsImportReady(
    WindowToken window,
    const std::string& operation_id,
    std::optional<uint64_t> expected_bytes) const {
  base::AutoLock guard(state_lock_);
  const auto found = operations_.find(operation_id);
  return found != operations_.end() && found->second->owner == window &&
         found->second->phase == Operation::Phase::kImportReady &&
         (!expected_bytes ||
          *expected_bytes == found->second->maximum_import_bytes);
}

bool ProfileBackupWorkflow::WithdrawOperation(WindowToken window,
                                              const std::string& operation_id) {
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window) {
    return false;
  }
  if (found->second->phase == Operation::Phase::kRestoreCommitting ||
      found->second->phase == Operation::Phase::kRestoreClosing) {
    found->second->restore_window_detached = true;
    return true;
  }
  operations_.erase(found);
  return true;
}

void ProfileBackupWorkflow::ScheduleStageAbandon(std::string operation_id) {
  scoped_refptr<storage::backup::BackupArchiveStageStore> store = StageStore();
  if (!store) {
    return;
  }
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](scoped_refptr<storage::backup::BackupArchiveStageStore> stage,
             std::string id) { stage->Abandon(id); },
          std::move(store), std::move(operation_id)));
}

void ProfileBackupWorkflow::CancelCoordinatorOperation(
    std::string operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (coordinator_ && !coordinator_->CancelBeforeCommit(operation_id)) {
    coordinator_->Cancel(operation_id);
  }
}

}  // namespace taffy
