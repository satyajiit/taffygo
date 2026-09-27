// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_SKILL_PROJECTION_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_SKILL_PROJECTION_H_

#include <vector>

#include "base/types/expected.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {

enum class BackupSkillProjectionError {
  kInvalidRecord,
  kCapacityExceeded,
};

// Decodes the active authored-skill and learned-procedure records from an
// already typed candidate. The result is the same bounded SkillRecord shape
// consumed by Core bootstrap, sorted by skill identity. Draft, disabled and
// retired status values remain records; this function does not judge whether
// a procedure may run. Tombstones, duplicate identities and descriptor/body
// disagreement fail closed.
base::expected<std::vector<core_service::mojom::SkillRecordPtr>,
               BackupSkillProjectionError>
DecodeSelectedBackupSkillRecords(
    const std::vector<BackupSnapshotRecord>& records);

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_SKILL_PROJECTION_H_
