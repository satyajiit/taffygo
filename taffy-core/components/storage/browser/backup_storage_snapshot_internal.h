// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_STORAGE_SNAPSHOT_INTERNAL_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_STORAGE_SNAPSHOT_INTERNAL_H_

#include <vector>

#include "taffy/components/storage/browser/backup_storage_snapshot.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup::snapshot_internal {

bool ReadSavedWorkspaceOrProcedureKind(
    sql::Database* database,
    core_service::mojom::BackupRecordKind kind,
    std::vector<BackupSnapshotRecord>* output);

}  // namespace taffy::storage::backup::snapshot_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_STORAGE_SNAPSHOT_INTERNAL_H_
