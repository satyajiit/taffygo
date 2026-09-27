// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_BACKUP_ARCHIVE_STAGE_STORE_LEASE_H_
#define TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_BACKUP_ARCHIVE_STAGE_STORE_LEASE_H_

#include <memory>

#include "base/files/file_path.h"

namespace taffy::storage::backup::internal {

// Process-local exclusive ownership of one canonical backup staging child.
// The stage store retains this through its destructor so no second owner can
// replace the directory while an old detached file operation still exists.
class BackupStageDirectoryLease {
 public:
  static std::unique_ptr<BackupStageDirectoryLease> Acquire(
      const base::FilePath& requested_directory);

  BackupStageDirectoryLease(const BackupStageDirectoryLease&) = delete;
  BackupStageDirectoryLease& operator=(const BackupStageDirectoryLease&) =
      delete;
  ~BackupStageDirectoryLease();

  const base::FilePath& directory() const { return directory_; }

 private:
  explicit BackupStageDirectoryLease(base::FilePath directory);

  const base::FilePath directory_;
};

}  // namespace taffy::storage::backup::internal

#endif  // TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_BACKUP_ARCHIVE_STAGE_STORE_LEASE_H_
