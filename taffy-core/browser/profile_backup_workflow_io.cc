// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_workflow_io.h"

#include <utility>

namespace taffy {

ProfileBackupWorkflowIoState::ProfileBackupWorkflowIoState() = default;
ProfileBackupWorkflowIoState::~ProfileBackupWorkflowIoState() = default;

bool ProfileBackupWorkflowIoState::TryAcquire() {
  bool expected = false;
  return active() && in_flight_.compare_exchange_strong(
                         expected, true, std::memory_order_acq_rel);
}

void ProfileBackupWorkflowIoState::Release() {
  in_flight_.store(false, std::memory_order_release);
}

void ProfileBackupWorkflowIoState::Revoke() {
  active_.store(false, std::memory_order_release);
}

bool ProfileBackupWorkflowIoState::active() const {
  return active_.load(std::memory_order_acquire);
}

ProfileBackupWorkflowIoHandle::ProfileBackupWorkflowIoHandle(
    Kind kind,
    std::string operation_id,
    uint64_t actual_archive_bytes,
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    scoped_refptr<ProfileBackupWorkflowIoState> state)
    : kind_(kind),
      operation_id_(std::move(operation_id)),
      actual_archive_bytes_(actual_archive_bytes),
      stage_store_(std::move(stage_store)),
      state_(std::move(state)) {}

ProfileBackupWorkflowIoHandle::~ProfileBackupWorkflowIoHandle() {
  state_->Release();
}

void ProfileBackupWorkflowIoHandle::Run() {
  if (result_) {
    return;
  }
  if (!state_->active()) {
    result_ = storage::backup::BackupStageStatus::kUnavailable;
    return;
  }
  switch (kind_) {
    case Kind::kExportVerification:
      result_ = stage_store_->VerifyExportReadback(operation_id_);
      break;
    case Kind::kImportInspection:
      result_ = stage_store_->InspectImportedArchive(operation_id_,
                                                     actual_archive_bytes_);
      break;
  }
  if (!state_->active()) {
    result_ = storage::backup::BackupStageStatus::kUnavailable;
  }
}

std::optional<storage::backup::BackupStageStatus>
ProfileBackupWorkflowIoHandle::result() const {
  return result_;
}

bool ProfileBackupWorkflowIoHandle::Matches(
    Kind kind,
    const ProfileBackupWorkflowIoState* state) const {
  return kind_ == kind && state_.get() == state && state_->active();
}

}  // namespace taffy
