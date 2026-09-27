// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace taffy::storage::backup {
namespace {

using Error = DormantBackupRestoreTargetError;

bool IsCanonicalUuidV4(std::string_view value) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(value);
  return parsed.is_valid() && parsed.AsLowercaseString() == value &&
         value[14] == '4' &&
         (value[19] == '8' || value[19] == '9' || value[19] == 'a' ||
          value[19] == 'b');
}

// One definition of this rule, in
// dormant_backup_restore_target_resolution_internal.cc, because the platform
// link it has to accept is a fact about the device rather than about any one
// caller, and three copies of it were three places to get that wrong.
using restore_resolution_internal::IsRegularDirectoryWithoutLinks;

bool SyncDirectory(const base::FilePath& path) {
#if BUILDFLAG(IS_POSIX)
  base::File directory(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                 base::File::FLAG_NO_FOLLOW);
  base::File::Info info;
  // File::Flush uses fdatasync on Android/Linux. Directory-entry durability
  // requires fsync, so do not substitute the ordinary file flush here.
  return directory.IsValid() && directory.GetInfo(&info) && info.is_directory &&
         HANDLE_EINTR(fsync(directory.GetPlatformFile())) == 0;
#else
  return false;
#endif
}

}  // namespace

base::expected<std::unique_ptr<DormantBackupRestoreTarget>, Error>
DormantBackupRestoreTarget::Create(const base::FilePath& reserved_profile_path,
                                   std::string reserved_target_profile_id,
                                   bool private_profile) {
  if (private_profile || !IsCanonicalUuidV4(reserved_target_profile_id) ||
      !IsRegularDirectoryWithoutLinks(reserved_profile_path)) {
    return base::unexpected(Error::kInvalidTarget);
  }
  const auto directory = reserved_profile_path.AppendASCII("TaffyCore");
  const auto database_path = directory.AppendASCII("core.sqlite3");
  auto physical_lease =
      DormantBackupRestoreTargetLease::TryAcquire(database_path);
  if (!physical_lease) {
    return base::unexpected(Error::kTargetOccupied);
  }
  if (base::PathExists(directory) || base::IsLink(directory)) {
    return base::unexpected(Error::kTargetOccupied);
  }
  if (!base::CreateDirectory(directory) || base::IsLink(directory)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  // FLAG_CREATE is O_CREAT|O_EXCL: a preexisting file, including a dangling
  // symbolic link, cannot be opened or truncated by this initial write.
  base::File exclusive(database_path,
                       base::File::FLAG_CREATE | base::File::FLAG_READ |
                           base::File::FLAG_WRITE | base::File::FLAG_NO_FOLLOW);
  if (!exclusive.IsValid() || !exclusive.created()) {
    return base::unexpected(exclusive.error_details() ==
                                    base::File::FILE_ERROR_EXISTS
                                ? Error::kTargetOccupied
                                : Error::kStorageUnavailable);
  }
  auto target = std::unique_ptr<DormantBackupRestoreTarget>(
      new DormantBackupRestoreTarget(
          database_path, std::move(reserved_target_profile_id),
          std::move(exclusive), std::move(physical_lease)));
  if (!target->Initialize() || !SyncDirectory(directory) ||
      !SyncDirectory(reserved_profile_path)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  return target;
}

DormantBackupRestoreTarget::DormantBackupRestoreTarget(
    base::FilePath database_path,
    std::string profile_id,
    base::File exclusive_file,
    std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease)
    : physical_lease_(std::move(physical_lease)),
      database_path_(std::move(database_path)),
      profile_id_(std::move(profile_id)),
      exclusive_file_(std::move(exclusive_file)),
      database_(
          std::make_unique<sql::Database>(sql::DatabaseOptions()
                                              .set_exclusive_locking(true)
                                              .set_wal_mode(false)
                                              .set_flush_to_media(true),
                                          sql::Database::Tag("TaffyCore"))) {}

DormantBackupRestoreTarget::~DormantBackupRestoreTarget() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool DormantBackupRestoreTarget::HasOwnedDatabasePath() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
#if BUILDFLAG(IS_POSIX)
  base::stat_wrapper_t owned = {};
  base::stat_wrapper_t current = {};
  return exclusive_file_.IsValid() &&
         IsRegularDirectoryWithoutLinks(database_path_.DirName()) &&
         base::File::Fstat(exclusive_file_.GetPlatformFile(), &owned) == 0 &&
         base::File::Lstat(database_path_, &current) == 0 &&
         S_ISREG(owned.st_mode) && S_ISREG(current.st_mode) &&
         owned.st_nlink == 1 && current.st_nlink == 1 &&
         owned.st_dev == current.st_dev && owned.st_ino == current.st_ino;
#else
  return false;
#endif
}

bool DormantBackupRestoreTarget::Initialize() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Check the held regular-file identity on both sides of SQLite's pathname
  // open. This detects replacement; sole lifecycle-owner access to the private
  // directory is still required to exclude swaps during that call itself.
  if (!HasOwnedDatabasePath() || !database_->Open(database_path_) ||
      !HasOwnedDatabasePath() ||
      !database_->Execute("PRAGMA synchronous=FULL")) {
    return false;
  }
  {
    sql::Transaction transaction(database_.get());
    if (!transaction.Begin() ||
        !restore_internal::CreateSchema(database_.get(), profile_id_) ||
        !transaction.Commit()) {
      return false;
    }
  }
  sql::Statement identity(
      database_->GetUniqueStatement("SELECT browser_profile_id FROM "
                                    "core_profile_identity WHERE singleton=1"));
  if (!identity.is_valid() || !identity.Step() ||
      identity.ColumnString(0) != profile_id_ || identity.Step() ||
      !identity.Succeeded()) {
    return false;
  }
  return HasOwnedDatabasePath() && exclusive_file_.Flush();
}

}  // namespace taffy::storage::backup
