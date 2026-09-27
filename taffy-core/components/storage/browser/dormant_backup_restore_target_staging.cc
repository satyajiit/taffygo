// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/backup_skill_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

#if BUILDFLAG(IS_POSIX)
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using Error = DormantBackupRestoreStageError;

constexpr char kStagingChild[] = "TaffyRestoreStaging";

bool BoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char byte) {
           return byte < 0x20u || byte == 0x7fu;
         });
}

bool NonzeroDigest(const std::vector<uint8_t>& digest) {
  return digest.size() == 32u &&
         std::ranges::any_of(digest, [](uint8_t byte) { return byte != 0u; });
}

bool StructurallyValidOperation(const mojom::OperationEnvelope* operation) {
  return operation && operation->service_generation > 0u &&
         operation->task_revision == 0u &&
         operation->deadline_monotonic_ms > 0u &&
         BoundedText(operation->operation_id, mojom::kMaxOperationIdBytes) &&
         BoundedText(operation->idempotency_key,
                     mojom::kMaxIdempotencyKeyBytes);
}

bool ExactOperation(const mojom::OperationEnvelope* left,
                    const mojom::OperationEnvelope* right) {
  return left && right && left->operation_id == right->operation_id &&
         left->service_generation == right->service_generation &&
         left->task_revision == right->task_revision &&
         left->deadline_monotonic_ms == right->deadline_monotonic_ms &&
         left->idempotency_key == right->idempotency_key;
}

bool ExactTarget(const mojom::BackupRestoreTarget* left,
                 const mojom::BackupRestoreTarget* right) {
  return left && right && left->kind == right->kind &&
         left->profile_id == right->profile_id;
}

bool ExactBinding(const mojom::BackupRestoreBinding* left,
                  const mojom::BackupRestoreBinding* right) {
  return left && right &&
         ExactOperation(left->planning_operation.get(),
                        right->planning_operation.get()) &&
         left->owner_profile_id == right->owner_profile_id &&
         ExactTarget(left->target.get(), right->target.get()) &&
         left->backup_id == right->backup_id &&
         left->snapshot_sha256 == right->snapshot_sha256 &&
         left->confirmation_sha256 == right->confirmation_sha256;
}

bool ValidAuthorizationAtEntry(
    const mojom::BackupRestorePlanResult* plan,
    const mojom::BackupRestoreStageAuthorization* authorization,
    std::string_view target_profile_id,
    uint64_t now_monotonic_ms) {
  if (!plan || !authorization || !plan->operation || !plan->target ||
      !plan->binding || !authorization->binding ||
      !authorization->decision_operation ||
      plan->status != mojom::BackupPlanningStatus::kSucceeded ||
      plan->has_conflicts ||
      plan->target->kind !=
          mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      plan->target->profile_id != target_profile_id ||
      !StructurallyValidOperation(plan->operation.get()) ||
      !StructurallyValidOperation(authorization->decision_operation.get()) ||
      authorization->decision_operation->deadline_monotonic_ms <=
          now_monotonic_ms ||
      plan->operation->service_generation !=
          authorization->decision_operation->service_generation ||
      !ExactBinding(plan->binding.get(), authorization->binding.get()) ||
      !ExactOperation(plan->operation.get(),
                      plan->binding->planning_operation.get()) ||
      !ExactTarget(plan->target.get(), plan->binding->target.get()) ||
      !BoundedText(plan->binding->owner_profile_id, mojom::kMaxBackupIdBytes) ||
      plan->binding->owner_profile_id == target_profile_id ||
      !BoundedText(plan->backup_id, mojom::kMaxBackupIdBytes) ||
      plan->backup_id != plan->binding->backup_id ||
      !NonzeroDigest(plan->snapshot_sha256) ||
      plan->snapshot_sha256 != plan->binding->snapshot_sha256 ||
      !NonzeroDigest(plan->confirmation_sha256) ||
      plan->confirmation_sha256 != plan->binding->confirmation_sha256) {
    return false;
  }
  return true;
}

Error MapStageError(BackupRestoreStageError error) {
  if (error == BackupRestoreStageError::kPlanRefused) {
    return Error::kPlanRefused;
  }
  if (error == BackupRestoreStageError::kUnsupportedRecord) {
    return Error::kUnsupportedRecord;
  }
  if (error == BackupRestoreStageError::kPayloadMismatch) {
    return Error::kPayloadMismatch;
  }
  return Error::kStorageUnavailable;
}

bool SyncDirectory(const base::FilePath& path) {
#if BUILDFLAG(IS_POSIX)
  base::File directory(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                 base::File::FLAG_NO_FOLLOW);
  base::File::Info info;
  return directory.IsValid() && directory.GetInfo(&info) && info.is_directory &&
         HANDLE_EINTR(fsync(directory.GetPlatformFile())) == 0;
#else
  return false;
#endif
}

}  // namespace

bool DormantBackupRestoreTarget::HasOwnedStagingPath() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
#if BUILDFLAG(IS_POSIX)
  if (staging_parent_unlinked_ || staging_parent_path_.empty() ||
      staging_parent_path_.DirName() != database_path_.DirName()) {
    return false;
  }
  base::stat_wrapper_t held = {};
  base::stat_wrapper_t current = {};
  return staging_parent_directory_.IsValid() &&
         base::File::Fstat(staging_parent_directory_.GetPlatformFile(),
                           &held) == 0 &&
         base::File::Lstat(staging_parent_path_, &current) == 0 &&
         S_ISDIR(held.st_mode) && S_ISDIR(current.st_mode) &&
         held.st_dev == current.st_dev && held.st_ino == current.st_ino;
#else
  return false;
#endif
}

bool DormantBackupRestoreTarget::HasPristineTargetContents() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement identity(
      database_->GetUniqueStatement("SELECT browser_profile_id FROM "
                                    "core_profile_identity WHERE singleton=1"));
  if (!identity.Step() || identity.ColumnString(0) != profile_id_ ||
      identity.Step() || !identity.Succeeded()) {
    return false;
  }
  sql::Statement tables(database_->GetUniqueStatement(
      "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT IN "
      "('taffy_storage_schema','core_profile_identity')"));
  while (tables.Step()) {
    const std::string name = tables.ColumnString(0);
    if ((name != "sqlite_sequence" && !name.starts_with("core_")) ||
        !std::ranges::all_of(name, [](char byte) {
          return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                 byte == '_';
        })) {
      return false;
    }
    sql::Statement row(
        database_->GetUniqueStatement("SELECT 1 FROM " + name + " LIMIT 1"));
    if (row.Step() || !row.Succeeded()) {
      return false;
    }
  }
  return tables.Succeeded();
}

bool DormantBackupRestoreTarget::IsPristineTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Transaction snapshot(database_.get());
  return snapshot.Begin() && HasPristineTargetContents() && snapshot.Commit();
}

base::expected<DormantBackupRestoreStageReceipt, Error>
DormantBackupRestoreTarget::StageAuthorized(
    mojom::BackupRestorePlanResultPtr plan,
    mojom::BackupRestoreStageAuthorizationPtr authorization,
    base::File plaintext_payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const int64_t now = base::TimeTicks::Now().since_origin().InMilliseconds();
  if (now < 0 ||
      !ValidAuthorizationAtEntry(plan.get(), authorization.get(), profile_id_,
                                 static_cast<uint64_t>(now))) {
    return base::unexpected(Error::kInvalidAuthorization);
  }
  if (stage_authorization_consumed_) {
    return base::unexpected(Error::kAuthorizationConsumed);
  }

  // A structurally valid exact authorization is consumptive. Mark it before
  // any filesystem or SQL observation so a transient failure cannot turn one
  // source-Core decision into multiple physical attempts.
  stage_authorization_consumed_ = true;
  if (!HasOwnedDatabasePath() || !IsPristineTarget()) {
    return base::unexpected(Error::kTargetChanged);
  }

  staging_parent_path_ = database_path_.DirName().AppendASCII(kStagingChild);
  if (base::PathExists(staging_parent_path_) ||
      base::IsLink(staging_parent_path_)) {
    staging_parent_path_.clear();
    return base::unexpected(Error::kStagingPathOccupied);
  }
#if BUILDFLAG(IS_POSIX)
  if (HANDLE_EINTR(mkdir(staging_parent_path_.value().c_str(), 0700)) != 0) {
    staging_parent_path_.clear();
    return base::unexpected(errno == EEXIST ? Error::kStagingPathOccupied
                                            : Error::kStorageUnavailable);
  }
#else
  staging_parent_path_.clear();
  return base::unexpected(Error::kStorageUnavailable);
#endif

  staging_parent_directory_ = base::File(
      staging_parent_path_, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                base::File::FLAG_NO_FOLLOW);
  if (!HasOwnedStagingPath() || !SyncDirectory(database_path_.DirName()) ||
      !HasOwnedDatabasePath()) {
    return base::unexpected(Error::kStorageUnavailable);
  }

  auto stage = BackupRestoreStage::Create(
      staging_parent_path_, *plan, authorization->binding->confirmation_sha256,
      std::move(plaintext_payload));
  if (!stage.has_value()) {
    return base::unexpected(MapStageError(stage.error()));
  }
  restore_stage_ = std::move(*stage);
  auto verified = restore_stage_->ReadVerifiedRecords();
  if (!verified.has_value()) {
    return base::unexpected(MapStageError(verified.error()));
  }
  auto procedures = DecodeSelectedBackupSkillRecords(*verified);
  if (!procedures.has_value()) {
    return base::unexpected(
        procedures.error() == BackupSkillProjectionError::kCapacityExceeded
            ? Error::kUnsupportedRecord
            : Error::kPayloadMismatch);
  }
  if (!HasOwnedDatabasePath() || !HasOwnedStagingPath() ||
      !IsPristineTarget()) {
    return base::unexpected(Error::kTargetChanged);
  }

  DormantBackupRestoreStageReceipt receipt;
  std::ranges::copy(plan->snapshot_sha256, receipt.snapshot_sha256.begin());
  receipt.record_count = static_cast<uint64_t>(verified->size());
  receipt.procedures = std::move(*procedures);
  return receipt;
}

base::expected<void, Error> DormantBackupRestoreTarget::AbandonStage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (staging_parent_path_.empty()) {
    return base::ok();
  }
  if (staging_parent_unlinked_) {
    if (!SyncDirectory(database_path_.DirName())) {
      return base::unexpected(Error::kCleanupFailed);
    }
    staging_parent_directory_.Close();
    staging_parent_path_.clear();
    staging_parent_unlinked_ = false;
    return base::ok();
  }
  if (!HasOwnedDatabasePath() || !HasOwnedStagingPath()) {
    return base::unexpected(Error::kCleanupFailed);
  }
  if (restore_stage_ && !restore_stage_->Delete()) {
    return base::unexpected(Error::kCleanupFailed);
  }
  restore_stage_.reset();

  // The parent was exclusively created and its held inode still matches the
  // pathname. Recursive cleanup is confined to that exact owned child; it can
  // contain a partially built inner stage after a failed Create.
  base::DeletePathRecursively(staging_parent_path_);
  if (base::PathExists(staging_parent_path_) ||
      base::IsLink(staging_parent_path_)) {
    return base::unexpected(Error::kCleanupFailed);
  }
  staging_parent_unlinked_ = true;
  if (!SyncDirectory(database_path_.DirName())) {
    return base::unexpected(Error::kCleanupFailed);
  }
  staging_parent_directory_.Close();
  staging_parent_path_.clear();
  staging_parent_unlinked_ = false;
  return base::ok();
}

}  // namespace taffy::storage::backup
