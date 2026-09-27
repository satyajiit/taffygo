// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_STORAGE_SNAPSHOT_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_STORAGE_SNAPSHOT_H_

#include <vector>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup {

// A selected, canonical active record or a content-free deletion marker.
// Only descriptor metadata can reach the isolated planner. The move-only
// plaintext buffer stays in browser custody and is wiped when released.
struct BackupSnapshotRecord {
  core_service::mojom::BackupRecordDescriptorPtr descriptor;
  std::vector<uint8_t> plaintext;

  BackupSnapshotRecord();
  BackupSnapshotRecord(BackupSnapshotRecord&&);
  BackupSnapshotRecord& operator=(BackupSnapshotRecord&&);
  ~BackupSnapshotRecord();
};

enum class BackupSnapshotError {
  kUnsupportedSelection,
  kUnavailable,
  kInvalidRecord,
  kCapacityExceeded,
};

using BackupSnapshotResult =
    base::expected<std::vector<BackupSnapshotRecord>, BackupSnapshotError>;

// The whole request is checked before opening a transaction or reading rows.
// The reader supports exactly the six implemented typed codecs. Selecting any
// other class refuses the request, rather than omitting it.
bool IsSupportedBackupStorageSelection(
    base::span<const core_service::mojom::BackupRecordKind> selection);
BackupSnapshotResult ReadSelectedBackupRecords(
    sql::Database* database,
    base::span<const core_service::mojom::BackupRecordKind> selection);

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_STORAGE_SNAPSHOT_H_
