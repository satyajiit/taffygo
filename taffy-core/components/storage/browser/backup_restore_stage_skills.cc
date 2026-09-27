// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/backup_skill_projection.h"

namespace taffy::storage::backup {

base::expected<std::vector<core_service::mojom::SkillRecordPtr>,
               BackupRestoreStageError>
BackupRestoreStage::ReadVerifiedSkillRecords() const {
  auto records = ReadVerifiedRecords();
  if (!records) {
    return base::unexpected(records.error());
  }
  auto skills = DecodeSelectedBackupSkillRecords(*records);
  if (!skills) {
    return base::unexpected(
        skills.error() == BackupSkillProjectionError::kCapacityExceeded
            ? BackupRestoreStageError::kUnsupportedRecord
            : BackupRestoreStageError::kPayloadMismatch);
  }
  return std::move(*skills);
}

}  // namespace taffy::storage::backup
