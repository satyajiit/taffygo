// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>

#include "sql/database.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {

base::expected<std::unique_ptr<DormantBackupRestoreTargetDeletionCustody>,
               DormantBackupRestoreResolutionError>
DormantBackupRestoreTargetDeletionCustody::OpenForDiscard(
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
  const base::FilePath database_path =
      reserved_profile_path.AppendASCII("TaffyCore")
          .AppendASCII("core.sqlite3");
  if (!resolution::IsRegularDirectoryWithoutLinks(database_path.DirName()) ||
      !resolution::HasNoSqliteSidecars(database_path)) {
    return base::unexpected(Error::kTargetChanged);
  }
  auto lease = DormantBackupRestoreTargetLease::TryAcquire(database_path);
  if (!lease) {
    return base::unexpected(Error::kTargetBusy);
  }
  auto custody = std::unique_ptr<DormantBackupRestoreTargetDeletionCustody>(
      new DormantBackupRestoreTargetDeletionCustody(
          reserved_profile_path, database_path,
          std::move(exact_target->profile_id),
          resolution::OpenPath(reserved_profile_path.DirName()),
          resolution::OpenPath(database_path), std::move(lease)));
  if (!custody->HasOwnedTargetPath()) {
    return base::unexpected(Error::kTargetChanged);
  }
  custody->database_ = std::make_unique<sql::Database>(
      sql::DatabaseOptions().set_read_only(true),
      sql::Database::Tag("TaffyCore"));
  if (!custody->database_->Open(database_path) ||
      !custody->database_->Execute("PRAGMA query_only=ON")) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  const auto schema = target::InspectCurrentSchema(custody->database_.get(),
                                                   custody->profile_id_);
  if (schema != target::SchemaMatch::kExact) {
    return base::unexpected(schema == target::SchemaMatch::kMismatch
                                ? Error::kSchemaMismatch
                                : Error::kStorageUnavailable);
  }
  return custody;
}

DormantBackupRestoreTargetDeletionCustody::
    DormantBackupRestoreTargetDeletionCustody(
        base::FilePath profile_path,
        base::FilePath database_path,
        std::string profile_id,
        base::File held_profile_parent,
        base::File held_database,
        std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease)
    : physical_lease_(std::move(physical_lease)),
      profile_path_(std::move(profile_path)),
      database_path_(std::move(database_path)),
      profile_id_(std::move(profile_id)),
      held_profile_parent_(std::move(held_profile_parent)),
      held_database_(std::move(held_database)) {}

DormantBackupRestoreTargetDeletionCustody::
    ~DormantBackupRestoreTargetDeletionCustody() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool DormantBackupRestoreTargetDeletionCustody::HasOwnedTargetPath() const {
  namespace resolution = restore_resolution_internal;
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return resolution::IsRegularDirectoryWithoutLinks(profile_path_) &&
         resolution::IsRegularDirectoryWithoutLinks(database_path_.DirName()) &&
         resolution::SameOpenedPath(held_profile_parent_,
                                    profile_path_.DirName(), true) &&
         resolution::SameOpenedPath(held_database_, database_path_, false) &&
         resolution::HasNoSqliteSidecars(database_path_);
}

bool DormantBackupRestoreTargetDeletionCustody::PrepareForProfileDeletion() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (prepared_for_deletion_ || !database_ || !HasOwnedTargetPath()) {
    return false;
  }
  database_.reset();
  prepared_for_deletion_ = true;
  return HasOwnedTargetPath();
}

bool DormantBackupRestoreTargetDeletionCustody::VerifyProfileDeleted() {
  namespace resolution = restore_resolution_internal;
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!prepared_for_deletion_ || !resolution::PathIsAbsent(profile_path_) ||
      !resolution::OpenedPathWasUnlinked(held_database_, database_path_,
                                         false)) {
    return false;
  }
  return resolution::SyncOpenedDirectory(held_profile_parent_);
}

}  // namespace taffy::storage::backup
