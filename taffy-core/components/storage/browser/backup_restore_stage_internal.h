// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RESTORE_STAGE_INTERNAL_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RESTORE_STAGE_INTERNAL_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup::restore_internal {

// Creates the generated head and one fresh local identity in a new database.
// The caller owns both the empty database and the surrounding transaction.
bool CreateSchema(sql::Database* database, std::string_view profile_id);

// Inserts a decoded typed record into an empty staging or dormant target
// transaction. Archive revisions survive; caller-supplied local effect
// metadata is newly created and never authorizes replay of source-device work.
// The three-argument staging overload supplies stage-local metadata. No
// overwrite SQL is used.
base::expected<void, BackupRestoreStageError> InsertRecord(
    sql::Database* database,
    const core_service::mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext);
base::expected<void, BackupRestoreStageError> InsertRecord(
    sql::Database* database,
    const core_service::mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext,
    std::string_view local_effect_id);
bool InitializeCollectionRevisions(sql::Database* database);
bool InitializeCollectionRevisions(sql::Database* database,
                                   std::string_view library_effect_id,
                                   std::string_view memory_effect_id);

// New allowed families are kept out of the Library/Memory binder TU. This
// helper accepts only saved workspace, authored-skill and learned-procedure
// entries and never copies source SQL metadata or procedure run history.
base::expected<void, BackupRestoreStageError> InsertExtendedRecord(
    sql::Database* database,
    const core_service::mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext,
    std::string_view local_effect_id);

}  // namespace taffy::storage::backup::restore_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RESTORE_STAGE_INTERNAL_H_
