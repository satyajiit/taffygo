// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_internal.h"

namespace taffy {

scoped_refptr<storage::backup::BackupArchiveStageStore>
ProfileBackupWorkflow::StageStore() const {
  base::AutoLock guard(state_lock_);
  return stage_store_;
}

base::File ProfileBackupWorkflow::OpenEncryptedArchiveRead(
    WindowToken window,
    const std::string& operation_id) {
  scoped_refptr<storage::backup::BackupArchiveStageStore> store = StageStore();
  if (!store || !IsExportReady(window, operation_id, std::nullopt)) {
    return base::File();
  }
  return store->OpenEncryptedExport(operation_id);
}

base::File ProfileBackupWorkflow::OpenExportReadback(
    WindowToken window,
    const std::string& operation_id,
    uint64_t expected_archive_bytes) {
  scoped_refptr<storage::backup::BackupArchiveStageStore> store = StageStore();
  if (!store || expected_archive_bytes == 0u ||
      !IsExportReady(window, operation_id, expected_archive_bytes)) {
    return base::File();
  }
  return store->OpenExportReadback(operation_id, expected_archive_bytes);
}

std::unique_ptr<ProfileBackupWorkflowIoHandle>
ProfileBackupWorkflow::AcquireExportVerification(
    WindowToken window,
    const std::string& operation_id) {
  scoped_refptr<storage::backup::BackupArchiveStageStore> store = StageStore();
  if (!store) {
    return nullptr;
  }
  base::AutoLock guard(state_lock_);
  const auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kExportReady ||
      !found->second->io_state->TryAcquire()) {
    return nullptr;
  }
  return base::WrapUnique(new ProfileBackupWorkflowIoHandle(
      ProfileBackupWorkflowIoHandle::Kind::kExportVerification, operation_id,
      0u, std::move(store), found->second->io_state));
}

storage::backup::BackupStageStatus
ProfileBackupWorkflow::CompleteExportVerification(
    WindowToken window,
    const std::string& operation_id,
    const ProfileBackupWorkflowIoHandle& handle) {
  base::AutoLock guard(state_lock_);
  const auto found = operations_.find(operation_id);
  const auto result = handle.result();
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kExportReady || !result ||
      !handle.Matches(ProfileBackupWorkflowIoHandle::Kind::kExportVerification,
                      found->second->io_state.get())) {
    return storage::backup::BackupStageStatus::kUnavailable;
  }
  return *result;
}

base::File ProfileBackupWorkflow::OpenEncryptedImport(
    WindowToken window,
    const std::string& operation_id,
    uint64_t maximum_archive_bytes) {
  scoped_refptr<storage::backup::BackupArchiveStageStore> store = StageStore();
  if (!store || maximum_archive_bytes == 0u ||
      !IsImportReady(window, operation_id, maximum_archive_bytes)) {
    return base::File();
  }
  return store->OpenEncryptedImport(operation_id, maximum_archive_bytes);
}

std::unique_ptr<ProfileBackupWorkflowIoHandle>
ProfileBackupWorkflow::AcquireImportInspection(WindowToken window,
                                               const std::string& operation_id,
                                               uint64_t actual_archive_bytes) {
  scoped_refptr<storage::backup::BackupArchiveStageStore> store = StageStore();
  if (!store || actual_archive_bytes == 0u ||
      actual_archive_bytes >
          storage::backup::BackupArchiveStageStore::MaximumArchiveBytes()) {
    return nullptr;
  }
  base::AutoLock guard(state_lock_);
  const auto found = operations_.find(operation_id);
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kImportReady ||
      !found->second->io_state->TryAcquire()) {
    return nullptr;
  }
  return base::WrapUnique(new ProfileBackupWorkflowIoHandle(
      ProfileBackupWorkflowIoHandle::Kind::kImportInspection, operation_id,
      actual_archive_bytes, std::move(store), found->second->io_state));
}

storage::backup::BackupStageStatus
ProfileBackupWorkflow::CompleteImportInspection(
    WindowToken window,
    const std::string& operation_id,
    const ProfileBackupWorkflowIoHandle& handle) {
  base::AutoLock guard(state_lock_);
  auto found = operations_.find(operation_id);
  const auto result = handle.result();
  if (found == operations_.end() || found->second->owner != window ||
      found->second->phase != Operation::Phase::kImportReady || !result ||
      !handle.Matches(ProfileBackupWorkflowIoHandle::Kind::kImportInspection,
                      found->second->io_state.get())) {
    return storage::backup::BackupStageStatus::kUnavailable;
  }
  if (*result == storage::backup::BackupStageStatus::kVerified) {
    found->second->phase = Operation::Phase::kImportVerified;
  }
  return *result;
}

void ProfileBackupWorkflow::AbandonOperation(WindowToken window,
                                             const std::string& operation_id) {
  if (!WithdrawOperation(window, operation_id)) {
    return;
  }
  ScheduleStageAbandon(operation_id);
  owner_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ProfileBackupWorkflow::CancelCoordinatorOperation,
                     weak_this_, operation_id));
}

}  // namespace taffy
