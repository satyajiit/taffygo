// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "sql/database.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
using Error = DormantBackupRestoreReconcileError;
using State = DormantBackupRestoreReconcileState;

bool IsCanonicalUuidV4(std::string_view value) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(value);
  return parsed.is_valid() && parsed.AsLowercaseString() == value &&
         value.size() == 36u && value[14] == '4' &&
         (value[19] == '8' || value[19] == '9' || value[19] == 'a' ||
          value[19] == 'b');
}

// One definition of this rule, in
// dormant_backup_restore_target_resolution_internal.cc, because the platform
// link it has to accept is a fact about the device rather than about any one
// caller, and three copies of it were three places to get that wrong.
using restore_resolution_internal::IsRegularDirectoryWithoutLinks;

// One definition of the sidecar rule, in
// dormant_backup_restore_target_resolution_internal.cc: what SQLite leaves
// beside a database is a fact about the engine, not about the caller.
using restore_resolution_internal::HasNoSqliteSidecars;

}  // namespace

base::expected<std::unique_ptr<DormantBackupRestoreTargetReconciler>, Error>
DormantBackupRestoreTargetReconciler::OpenForRecovery(
    const base::FilePath& reserved_profile_path,
    mojom::BackupRestoreTargetPtr exact_target) {
  if (!exact_target ||
      exact_target->kind !=
          mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      !IsCanonicalUuidV4(exact_target->profile_id) ||
      !IsRegularDirectoryWithoutLinks(reserved_profile_path)) {
    return base::unexpected(Error::kInvalidTarget);
  }
  const base::FilePath core_path =
      reserved_profile_path.AppendASCII("TaffyCore");
  if (!IsRegularDirectoryWithoutLinks(core_path)) {
    return base::unexpected(Error::kTargetChanged);
  }
  const base::FilePath database_path = core_path.AppendASCII("core.sqlite3");
  auto physical_lease =
      DormantBackupRestoreTargetLease::TryAcquire(database_path);
  if (!physical_lease) {
    return base::unexpected(Error::kTargetBusy);
  }
  if (!HasNoSqliteSidecars(database_path)) {
    return base::unexpected(Error::kTargetChanged);
  }
  base::File held_database(database_path, base::File::FLAG_OPEN |
                                              base::File::FLAG_READ |
                                              base::File::FLAG_NO_FOLLOW);
  auto reconciler = std::unique_ptr<DormantBackupRestoreTargetReconciler>(
      new DormantBackupRestoreTargetReconciler(
          database_path, std::move(exact_target->profile_id),
          std::move(held_database), std::move(physical_lease)));
  if (!reconciler->HasOwnedDatabasePath()) {
    return base::unexpected(Error::kTargetChanged);
  }
  if (!reconciler->database_->Open(database_path) ||
      !reconciler->database_->Execute("PRAGMA query_only=ON") ||
      !reconciler->HasOwnedDatabasePath() ||
      !HasNoSqliteSidecars(database_path)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  const target_internal::SchemaMatch schema =
      target_internal::InspectCurrentSchema(reconciler->database_.get(),
                                            reconciler->profile_id_);
  if (schema != target_internal::SchemaMatch::kExact) {
    return base::unexpected(schema == target_internal::SchemaMatch::kMismatch
                                ? Error::kSchemaMismatch
                                : Error::kStorageUnavailable);
  }
  if (!reconciler->HasOwnedDatabasePath()) {
    return base::unexpected(Error::kTargetChanged);
  }
  return reconciler;
}

DormantBackupRestoreTargetReconciler::DormantBackupRestoreTargetReconciler(
    base::FilePath database_path,
    std::string profile_id,
    base::File held_database,
    std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease)
    : physical_lease_(std::move(physical_lease)),
      database_path_(std::move(database_path)),
      profile_id_(std::move(profile_id)),
      held_database_(std::move(held_database)),
      database_(std::make_unique<sql::Database>(
          sql::DatabaseOptions().set_read_only(true),
          sql::Database::Tag("TaffyCore"))) {}

DormantBackupRestoreTargetReconciler::~DormantBackupRestoreTargetReconciler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool DormantBackupRestoreTargetReconciler::HasOwnedDatabasePath() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
#if BUILDFLAG(IS_POSIX)
  base::stat_wrapper_t held = {};
  base::stat_wrapper_t current = {};
  return held_database_.IsValid() &&
         IsRegularDirectoryWithoutLinks(database_path_.DirName()) &&
         base::File::Fstat(held_database_.GetPlatformFile(), &held) == 0 &&
         base::File::Lstat(database_path_, &current) == 0 &&
         S_ISREG(held.st_mode) && S_ISREG(current.st_mode) &&
         held.st_nlink == 1 && current.st_nlink == 1 &&
         held.st_dev == current.st_dev && held.st_ino == current.st_ino;
#else
  return false;
#endif
}

base::expected<State, Error> DormantBackupRestoreTargetReconciler::Reconcile(
    mojom::BackupRestoreCandidateWitnessPtr expected_witness) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!expected_witness ||
      !target_internal::IsValidCandidateWitness(*expected_witness)) {
    return base::unexpected(Error::kInvalidWitness);
  }
  if (!HasOwnedDatabasePath() || !HasNoSqliteSidecars(database_path_)) {
    return base::unexpected(Error::kTargetChanged);
  }
  sql::Transaction snapshot(database_.get());
  if (!snapshot.Begin()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  const target_internal::SchemaMatch schema =
      target_internal::InspectCurrentSchema(database_.get(), profile_id_);
  if (schema != target_internal::SchemaMatch::kExact) {
    return base::unexpected(schema == target_internal::SchemaMatch::kMismatch
                                ? Error::kSchemaMismatch
                                : Error::kStorageUnavailable);
  }
  const target_internal::ProjectionMatch committed =
      target_internal::InspectCommittedProjection(database_.get(), profile_id_,
                                                  *expected_witness);
  if (committed == target_internal::ProjectionMatch::kUnavailable) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  State result = State::kOutcomeUnknown;
  if (committed == target_internal::ProjectionMatch::kExact) {
    result = State::kCommitted;
  } else {
    const target_internal::ProjectionMatch pristine =
        target_internal::InspectPristineProjection(database_.get(),
                                                   profile_id_);
    if (pristine == target_internal::ProjectionMatch::kUnavailable) {
      return base::unexpected(Error::kStorageUnavailable);
    }
    if (pristine == target_internal::ProjectionMatch::kExact) {
      result = State::kPristine;
    }
  }
  if (!snapshot.Commit()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  if (!HasOwnedDatabasePath() || !HasNoSqliteSidecars(database_path_)) {
    return base::unexpected(Error::kTargetChanged);
  }
  return result;
}

}  // namespace taffy::storage::backup
