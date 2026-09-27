// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sql/database.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {

base::expected<void, DormantBackupRestoreResolutionError>
VerifyPublishedDormantBackupRestoreTarget(
    const base::FilePath& reserved_profile_path,
    core_service::mojom::BackupRestoreTargetPtr exact_target) {
  namespace mojom = core_service::mojom;
  namespace resolution = restore_resolution_internal;
  namespace target = restore_target_internal;
  using Error = DormantBackupRestoreResolutionError;

  if (!exact_target ||
      exact_target->kind !=
          mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      !resolution::IsCanonicalUuidV4(exact_target->profile_id) ||
      !resolution::IsRegularDirectoryWithoutLinks(reserved_profile_path)) {
    return base::unexpected(Error::kInvalidTarget);
  }
  const base::FilePath core_path =
      reserved_profile_path.AppendASCII("TaffyCore");
  const base::FilePath database_path = core_path.AppendASCII("core.sqlite3");
  if (!resolution::IsRegularDirectoryWithoutLinks(core_path) ||
      !resolution::HasNoSqliteSidecars(database_path)) {
    return base::unexpected(Error::kTargetChanged);
  }
  auto lease = DormantBackupRestoreTargetLease::TryAcquire(database_path);
  if (!lease) {
    return base::unexpected(Error::kTargetBusy);
  }
  base::File held_core = resolution::OpenPath(core_path);
  base::File held_database = resolution::OpenPath(database_path);
  if (!resolution::SameOpenedPath(held_core, core_path, true) ||
      !resolution::SameOpenedPath(held_database, database_path, false)) {
    return base::unexpected(Error::kTargetChanged);
  }
  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         sql::Database::Tag("TaffyCore"));
  if (!database.Open(database_path) ||
      !database.Execute("PRAGMA query_only=ON") ||
      !resolution::SameOpenedPath(held_core, core_path, true) ||
      !resolution::SameOpenedPath(held_database, database_path, false)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  const auto schema =
      target::InspectCurrentSchema(&database, exact_target->profile_id);
  if (schema != target::SchemaMatch::kExact) {
    return base::unexpected(schema == target::SchemaMatch::kMismatch
                                ? Error::kSchemaMismatch
                                : Error::kStorageUnavailable);
  }
  return base::ok();
}

}  // namespace taffy::storage::backup
