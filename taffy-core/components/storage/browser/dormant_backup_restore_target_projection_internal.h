// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_PROJECTION_INTERNAL_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_PROJECTION_INTERNAL_H_

#include <string_view>
#include <vector>

#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"

namespace taffy::storage::backup::restore_target_internal {

ProjectionMatch ExtendedImportedMetadataMatches(
    sql::Database* database,
    std::string_view target_profile_id,
    const core_service::mojom::BackupRestoreCandidateWitness& witness,
    const std::vector<BackupSnapshotRecord>& records);

}  // namespace taffy::storage::backup::restore_target_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_PROJECTION_INTERNAL_H_
