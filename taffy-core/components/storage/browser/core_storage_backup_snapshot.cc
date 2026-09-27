// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/components/storage/browser/core_storage_backend.h"

namespace taffy {

void CoreStorageBroker::ReadBackupSnapshot(
    std::vector<core_service::mojom::BackupRecordKind> selection,
    BackupSnapshotCallback callback) {
  backend_.AsyncCall(&Backend::ReadBackupSnapshot)
      .WithArgs(std::move(selection))
      .Then(std::move(callback));
}

storage::backup::BackupSnapshotResult
CoreStorageBroker::Backend::ReadBackupSnapshot(
    std::vector<core_service::mojom::BackupRecordKind> selection) {
  if (!storage::backup::IsSupportedBackupStorageSelection(selection)) {
    return base::unexpected(
        storage::backup::BackupSnapshotError::kUnsupportedSelection);
  }
  if (ephemeral_ || !EnsureOpen()) {
    return base::unexpected(storage::backup::BackupSnapshotError::kUnavailable);
  }
  return storage::backup::ReadSelectedBackupRecords(&database_, selection);
}

}  // namespace taffy
