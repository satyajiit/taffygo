// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/files/file_util.h"
#include "sql/database.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
namespace resolution_internal = restore_resolution_internal;
using Error = DormantBackupRestoreResolutionError;
using resolution_internal::DirectChildren;
using resolution_internal::HasNoSqliteSidecars;
using resolution_internal::IsCanonicalStageChild;
using resolution_internal::IsCanonicalUuidV4;
using resolution_internal::IsRegularDirectoryWithoutLinks;
using resolution_internal::OpenedPathWasUnlinked;
using resolution_internal::OpenPath;
using resolution_internal::PathIsAbsent;
using resolution_internal::RemoveOpenedChildAndSync;
using resolution_internal::SameOpenedPath;
using resolution_internal::SyncOpenedDirectory;

constexpr char kCoreDirectory[] = "TaffyCore";
constexpr char kTargetDatabase[] = "core.sqlite3";
constexpr char kStagingParent[] = "TaffyRestoreStaging";
constexpr char kStageDatabase[] = "restore.sqlite3";
constexpr std::array kSelection{
    mojom::BackupRecordKind::kAssistantConfiguration,
    mojom::BackupRecordKind::kSavedWorkspace,
    mojom::BackupRecordKind::kLibraryEntry,
    mojom::BackupRecordKind::kMemoryRecord,
    mojom::BackupRecordKind::kUserAuthoredSkill,
    mojom::BackupRecordKind::kLearnedProcedure,
};

}  // namespace

base::expected<std::unique_ptr<DormantBackupRestoreTargetFinalizer>, Error>
DormantBackupRestoreTargetFinalizer::OpenForAccept(
    const base::FilePath& reserved_profile_path,
    mojom::BackupRestoreTargetPtr exact_target,
    mojom::BackupRestoreCandidateWitnessPtr exact_witness) {
  if (!exact_target ||
      exact_target->kind !=
          mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      !IsCanonicalUuidV4(exact_target->profile_id) ||
      !IsRegularDirectoryWithoutLinks(reserved_profile_path)) {
    return base::unexpected(Error::kInvalidTarget);
  }
  if (!exact_witness ||
      !target_internal::IsValidCandidateWitness(*exact_witness)) {
    return base::unexpected(Error::kInvalidWitness);
  }
  const base::FilePath core_path =
      reserved_profile_path.AppendASCII(kCoreDirectory);
  const base::FilePath database_path = core_path.AppendASCII(kTargetDatabase);
  if (!IsRegularDirectoryWithoutLinks(core_path) ||
      !HasNoSqliteSidecars(database_path)) {
    return base::unexpected(Error::kTargetChanged);
  }
  auto lease = DormantBackupRestoreTargetLease::TryAcquire(database_path);
  if (!lease) {
    return base::unexpected(Error::kTargetBusy);
  }
  auto finalizer = std::unique_ptr<DormantBackupRestoreTargetFinalizer>(
      new DormantBackupRestoreTargetFinalizer(
          reserved_profile_path, database_path,
          std::move(exact_target->profile_id), std::move(exact_witness),
          OpenPath(core_path), OpenPath(database_path), std::move(lease)));
  auto target = finalizer->OpenAndValidateTarget();
  if (!target.has_value()) {
    return base::unexpected(target.error());
  }
  auto stage = finalizer->LocateAndValidateRetainedStage();
  if (!stage.has_value()) {
    return base::unexpected(stage.error());
  }
  return finalizer;
}

DormantBackupRestoreTargetFinalizer::DormantBackupRestoreTargetFinalizer(
    base::FilePath profile_path,
    base::FilePath database_path,
    std::string profile_id,
    mojom::BackupRestoreCandidateWitnessPtr witness,
    base::File held_core_directory,
    base::File held_database,
    std::unique_ptr<DormantBackupRestoreTargetLease> physical_lease)
    : physical_lease_(std::move(physical_lease)),
      profile_path_(std::move(profile_path)),
      core_path_(database_path.DirName()),
      database_path_(std::move(database_path)),
      profile_id_(std::move(profile_id)),
      witness_(std::move(witness)),
      held_core_directory_(std::move(held_core_directory)),
      held_database_(std::move(held_database)) {}

DormantBackupRestoreTargetFinalizer::~DormantBackupRestoreTargetFinalizer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool DormantBackupRestoreTargetFinalizer::HasOwnedTargetPaths() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsRegularDirectoryWithoutLinks(profile_path_) &&
         SameOpenedPath(held_core_directory_, core_path_, true) &&
         SameOpenedPath(held_database_, database_path_, false) &&
         HasNoSqliteSidecars(database_path_);
}

base::expected<void, Error>
DormantBackupRestoreTargetFinalizer::OpenAndValidateTarget() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!HasOwnedTargetPaths()) {
    return base::unexpected(Error::kTargetChanged);
  }
  database_ = std::make_unique<sql::Database>(
      sql::DatabaseOptions().set_read_only(true),
      sql::Database::Tag("TaffyCore"));
  if (!database_->Open(database_path_) ||
      !database_->Execute("PRAGMA query_only=ON") || !HasOwnedTargetPaths()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  const auto schema =
      target_internal::InspectCurrentSchema(database_.get(), profile_id_);
  if (schema != target_internal::SchemaMatch::kExact) {
    return base::unexpected(schema == target_internal::SchemaMatch::kMismatch
                                ? Error::kSchemaMismatch
                                : Error::kStorageUnavailable);
  }
  if (!RevalidateCandidate()) {
    return base::unexpected(Error::kCandidateMismatch);
  }
  return base::ok();
}

base::expected<void, Error>
DormantBackupRestoreTargetFinalizer::LocateAndValidateRetainedStage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  staging_parent_path_ = core_path_.AppendASCII(kStagingParent);
  if (PathIsAbsent(staging_parent_path_)) {
    staging_parent_path_.clear();
    return base::ok();
  }
  if (!IsRegularDirectoryWithoutLinks(staging_parent_path_)) {
    return base::unexpected(Error::kStageMismatch);
  }
  held_staging_parent_ = OpenPath(staging_parent_path_);
  if (!SameOpenedPath(held_staging_parent_, staging_parent_path_, true)) {
    return base::unexpected(Error::kStageMismatch);
  }
  stage_present_ = true;
  const auto children = DirectChildren(staging_parent_path_);
  if (!children) {
    return base::unexpected(Error::kStageMismatch);
  }
  if (children->empty()) {
    stage_database_unlinked_ = true;
    stage_child_unlinked_ = true;
    return base::ok();
  }
  if (children->size() != 1u ||
      children->front().DirName() != staging_parent_path_ ||
      !IsCanonicalStageChild(children->front()) ||
      !IsRegularDirectoryWithoutLinks(children->front())) {
    return base::unexpected(Error::kStageMismatch);
  }
  stage_child_path_ = children->front();
  held_stage_child_ = OpenPath(stage_child_path_);
  if (!SameOpenedPath(held_stage_child_, stage_child_path_, true)) {
    return base::unexpected(Error::kStageMismatch);
  }
  const auto stage_entries = DirectChildren(stage_child_path_);
  if (!stage_entries) {
    return base::unexpected(Error::kStageMismatch);
  }
  if (stage_entries->empty()) {
    stage_database_unlinked_ = true;
    return base::ok();
  }
  stage_database_path_ = stage_child_path_.AppendASCII(kStageDatabase);
  // Two names, not one. `journal_mode=TRUNCATE` — which sql::Database sets for
  // every non-WAL database — commits by truncating the rollback journal to
  // zero bytes rather than unlinking it, so a staged database that has ever
  // been written to sits beside its own empty journal. Requiring a single
  // entry here refused every retained stage this class was written to accept.
  // HasNoSqliteSidecars still decides whether that journal is the empty one a
  // clean commit leaves, and any third name is still a mismatch.
  const base::FilePath stage_journal(stage_database_path_.value() +
                                     FILE_PATH_LITERAL("-journal"));
  if (stage_entries->size() > 2u ||
      !std::ranges::all_of(*stage_entries, [&](const base::FilePath& entry) {
        return entry == stage_database_path_ || entry == stage_journal;
      })) {
    return base::unexpected(Error::kStageMismatch);
  }
  if (!std::ranges::contains(*stage_entries, stage_database_path_)) {
    // A previous cleanup removed the database and stopped before its journal.
    // That is a resumable interruption, not a foreign stage: the journal is
    // named by the database that is gone, and FinalizeForPublication removes
    // it below on the way to emptying these directories.
    stage_database_unlinked_ = true;
    return base::ok();
  }
  if (!HasNoSqliteSidecars(stage_database_path_)) {
    return base::unexpected(Error::kStageMismatch);
  }
  held_stage_database_ = OpenPath(stage_database_path_);
  if (!SameOpenedPath(held_stage_database_, stage_database_path_, false)) {
    return base::unexpected(Error::kStageMismatch);
  }
  stage_database_ = std::make_unique<sql::Database>(
      sql::DatabaseOptions().set_read_only(true),
      sql::Database::Tag("TaffyCore"));
  if (!stage_database_->Open(stage_database_path_) ||
      !stage_database_->Execute("PRAGMA query_only=ON") ||
      !HasOwnedStagePaths() ||
      target_internal::InspectCurrentSchema(stage_database_.get(),
                                            profile_id_) !=
          target_internal::SchemaMatch::kExact) {
    return base::unexpected(Error::kStageMismatch);
  }
  auto records = ReadSelectedBackupRecords(stage_database_.get(), kSelection);
  auto observed =
      records ? target_internal::BuildCandidateWitness(*records) : nullptr;
  if (!observed ||
      !target_internal::IsExactCandidateWitness(*observed, *witness_)) {
    return base::unexpected(Error::kStageMismatch);
  }
  return base::ok();
}

bool DormantBackupRestoreTargetFinalizer::HasOwnedStagePaths() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return stage_present_ && !stage_database_unlinked_ &&
         SameOpenedPath(held_staging_parent_, staging_parent_path_, true) &&
         SameOpenedPath(held_stage_child_, stage_child_path_, true) &&
         SameOpenedPath(held_stage_database_, stage_database_path_, false) &&
         HasNoSqliteSidecars(stage_database_path_);
}

bool DormantBackupRestoreTargetFinalizer::RevalidateCandidate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!database_ || !HasOwnedTargetPaths()) {
    return false;
  }
  sql::Transaction snapshot(database_.get());
  return snapshot.Begin() &&
         target_internal::InspectCommittedProjection(database_.get(),
                                                     profile_id_, *witness_) ==
             target_internal::ProjectionMatch::kExact &&
         snapshot.Commit() && HasOwnedTargetPaths();
}

base::expected<void, Error>
DormantBackupRestoreTargetFinalizer::FinalizeForPublication() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!RevalidateCandidate()) {
    return base::unexpected(Error::kCandidateMismatch);
  }
  if (!stage_present_) {
    // Not a ternary: base::ok() and base::unexpected() are distinct types with
    // no common type for the conditional operator to convert to.
    if (!SyncOpenedDirectory(held_core_directory_)) {
      return base::unexpected(Error::kCleanupFailed);
    }
    return base::ok();
  }
  if (!stage_database_unlinked_) {
    if ((!OpenedPathWasUnlinked(held_stage_database_, stage_database_path_,
                                false) &&
         !HasOwnedStagePaths()) ||
        !HasNoSqliteSidecars(stage_database_path_)) {
      return base::unexpected(Error::kStageMismatch);
    }
    stage_database_.reset();
    if (!RemoveOpenedChildAndSync(held_stage_child_, stage_child_path_,
                                  held_stage_database_, stage_database_path_,
                                  false)) {
      return base::unexpected(Error::kCleanupFailed);
    }
    stage_database_unlinked_ = true;
  }
  // The journal the database left behind is removed with it: nothing else may
  // name it, HasNoSqliteSidecars proved it empty before the database went, and
  // the directory below cannot be removed while it is still there.
  if (!stage_database_path_.empty()) {
    const base::FilePath stage_journal(stage_database_path_.value() +
                                       FILE_PATH_LITERAL("-journal"));
    if (base::IsLink(stage_journal) ||
        (base::PathExists(stage_journal) &&
         (!base::DeleteFile(stage_journal) ||
          !SyncOpenedDirectory(held_stage_child_)))) {
      return base::unexpected(Error::kCleanupFailed);
    }
  }
  if (!stage_child_unlinked_) {
    if (!OpenedPathWasUnlinked(held_stage_child_, stage_child_path_, true)) {
      const auto children = DirectChildren(stage_child_path_);
      if (!children || !children->empty()) {
        return base::unexpected(Error::kCleanupFailed);
      }
    }
    if (!RemoveOpenedChildAndSync(held_staging_parent_, staging_parent_path_,
                                  held_stage_child_, stage_child_path_, true)) {
      return base::unexpected(Error::kCleanupFailed);
    }
    stage_child_unlinked_ = true;
  }
  if (!staging_parent_unlinked_) {
    if (!OpenedPathWasUnlinked(held_staging_parent_, staging_parent_path_,
                               true)) {
      const auto children = DirectChildren(staging_parent_path_);
      if (!children || !children->empty()) {
        return base::unexpected(Error::kCleanupFailed);
      }
    }
    if (!RemoveOpenedChildAndSync(held_core_directory_, core_path_,
                                  held_staging_parent_, staging_parent_path_,
                                  true)) {
      return base::unexpected(Error::kCleanupFailed);
    }
    staging_parent_unlinked_ = true;
  }
  if (!SyncOpenedDirectory(held_core_directory_)) {
    return base::unexpected(Error::kCleanupFailed);
  }
  stage_database_.reset();
  held_stage_database_.Close();
  held_stage_child_.Close();
  held_staging_parent_.Close();
  stage_database_path_.clear();
  stage_child_path_.clear();
  staging_parent_path_.clear();
  stage_present_ = false;
  return base::ok();
}

bool DormantBackupRestoreTargetFinalizer::VerifyPublishedIdentity() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return database_ && HasOwnedTargetPaths() &&
         target_internal::InspectCurrentSchema(database_.get(), profile_id_) ==
             target_internal::SchemaMatch::kExact;
}

}  // namespace taffy::storage::backup
