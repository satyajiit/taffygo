// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_IO_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_IO_H_

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "base/memory/ref_counted.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"

namespace taffy {

// Shared revocation only. It contains no Profile, Core, key, path, or payload
// pointer, so a detached I/O handle can safely outlive its browser window.
class ProfileBackupWorkflowIoState
    : public base::RefCountedThreadSafe<ProfileBackupWorkflowIoState> {
 public:
  ProfileBackupWorkflowIoState();

  bool TryAcquire();
  void Release();
  void Revoke();
  bool active() const;

 private:
  friend class base::RefCountedThreadSafe<ProfileBackupWorkflowIoState>;
  ~ProfileBackupWorkflowIoState();

  std::atomic_bool active_{true};
  std::atomic_bool in_flight_{false};
};

// One short-lived, blocking archive verification. Its owned stage reference
// and revocation state are the complete lifetime closure used off the UI
// sequence. Publication still requires rejoining the exact live workflow.
class ProfileBackupWorkflowIoHandle {
 public:
  enum class Kind : uint8_t { kExportVerification, kImportInspection };

  ProfileBackupWorkflowIoHandle(const ProfileBackupWorkflowIoHandle&) = delete;
  ProfileBackupWorkflowIoHandle& operator=(
      const ProfileBackupWorkflowIoHandle&) = delete;
  ~ProfileBackupWorkflowIoHandle();

  void Run();
  std::optional<storage::backup::BackupStageStatus> result() const;
  bool Matches(Kind kind, const ProfileBackupWorkflowIoState* state) const;

 private:
  friend class ProfileBackupWorkflow;

  ProfileBackupWorkflowIoHandle(
      Kind kind,
      std::string operation_id,
      uint64_t actual_archive_bytes,
      scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
      scoped_refptr<ProfileBackupWorkflowIoState> state);

  const Kind kind_;
  const std::string operation_id_;
  const uint64_t actual_archive_bytes_;
  const scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_;
  const scoped_refptr<ProfileBackupWorkflowIoState> state_;
  std::optional<storage::backup::BackupStageStatus> result_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_WORKFLOW_IO_H_
