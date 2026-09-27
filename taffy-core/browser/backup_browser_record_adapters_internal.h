// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_BROWSER_RECORD_ADAPTERS_INTERNAL_H_
#define TAFFY_BROWSER_BACKUP_BROWSER_RECORD_ADAPTERS_INTERNAL_H_

#include <string>

#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"

namespace taffy::backup_browser_internal {

base::expected<storage::backup::BackupSnapshotRecord,
               storage::backup::BackupSnapshotError>
MakeSnapshotRecord(core_service::mojom::BackupRecordKind kind,
                   std::string stable_id,
                   storage::backup::EncodedBackupRecord encoded);

}  // namespace taffy::backup_browser_internal

#endif  // TAFFY_BROWSER_BACKUP_BROWSER_RECORD_ADAPTERS_INTERNAL_H_
