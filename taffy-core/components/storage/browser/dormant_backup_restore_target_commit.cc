// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

#if BUILDFLAG(IS_POSIX)
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
using Error = DormantBackupRestoreCommitError;
using Outcome = DormantBackupRestoreCommitOutcome;

bool BoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char byte) {
           return byte < 0x20u || byte == 0x7fu;
         });
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

bool ExactEntry(const mojom::BackupRestorePlanEntry* left,
                const mojom::BackupRestorePlanEntry* right) {
  return left && right && left->kind == right->kind &&
         left->stable_id == right->stable_id &&
         left->archive_revision == right->archive_revision &&
         left->action == right->action &&
         left->schema_version == right->schema_version &&
         left->state == right->state &&
         left->plaintext_bytes == right->plaintext_bytes &&
         left->plaintext_sha256 == right->plaintext_sha256;
}

bool ExactPlan(const mojom::BackupRestorePlanResult* left,
               const mojom::BackupRestorePlanResult* right) {
  if (!left || !right ||
      !ExactOperation(left->operation.get(), right->operation.get()) ||
      left->status != right->status || left->backup_id != right->backup_id ||
      left->snapshot_sha256 != right->snapshot_sha256 ||
      !ExactTarget(left->target.get(), right->target.get()) ||
      left->has_conflicts != right->has_conflicts ||
      left->confirmation_sha256 != right->confirmation_sha256 ||
      !ExactBinding(left->binding.get(), right->binding.get()) ||
      left->entries.size() != right->entries.size()) {
    return false;
  }
  for (size_t index = 0u; index < left->entries.size(); ++index) {
    if (!ExactEntry(left->entries[index].get(), right->entries[index].get())) {
      return false;
    }
  }
  return true;
}

bool LiveExactCommitAuthorization(
    const mojom::BackupRestorePlanResult* plan,
    const mojom::BackupRestoreCommitAuthorization* authorization,
    uint64_t now_monotonic_ms) {
  return plan && plan->operation && plan->binding && authorization &&
         authorization->binding &&
         StructurallyValidOperation(authorization->decision_operation.get()) &&
         authorization->decision_operation->deadline_monotonic_ms >
             now_monotonic_ms &&
         authorization->decision_operation->service_generation ==
             plan->operation->service_generation &&
         ExactBinding(plan->binding.get(), authorization->binding.get());
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

// One definition of the sidecar rule, in
// dormant_backup_restore_target_resolution_internal.cc: what SQLite leaves
// beside a database is a fact about the engine, not about the caller.
using restore_resolution_internal::HasNoSqliteSidecars;

const BackupSnapshotRecord* FindRecord(
    const std::vector<BackupSnapshotRecord>& records,
    const mojom::BackupRestorePlanEntry& entry) {
  const auto found = std::ranges::find_if(records, [&](const auto& record) {
    return record.descriptor && record.descriptor->kind == entry.kind &&
           record.descriptor->stable_id == entry.stable_id;
  });
  return found == records.end() ? nullptr : &*found;
}

}  // namespace

base::expected<mojom::BackupRestoreCandidateWitnessPtr, Error>
DormantBackupRestoreTarget::PrepareCommitWitness() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!restore_stage_ || !HasOwnedStagingPath()) {
    return base::unexpected(Error::kStageUnavailable);
  }
  if (!HasOwnedDatabasePath() || !HasNoSqliteSidecars(database_path_) ||
      target_internal::InspectCurrentSchema(database_.get(), profile_id_) !=
          target_internal::SchemaMatch::kExact) {
    return base::unexpected(Error::kTargetChanged);
  }
  if (!IsPristineTarget()) {
    return base::unexpected(Error::kTargetNotPristine);
  }
  auto records = restore_stage_->ReadVerifiedRecords();
  if (!records.has_value()) {
    return base::unexpected(records.error() ==
                                    BackupRestoreStageError::kStorageUnavailable
                                ? Error::kStageUnavailable
                                : Error::kWitnessMismatch);
  }
  auto observed = target_internal::BuildCandidateWitness(*records);
  if (!observed) {
    return base::unexpected(Error::kWitnessMismatch);
  }
  if (!HasOwnedDatabasePath()) {
    return base::unexpected(Error::kTargetChanged);
  }
  if (!HasOwnedStagingPath()) {
    return base::unexpected(Error::kStageUnavailable);
  }
  if (prepared_commit_witness_) {
    if (!target_internal::IsExactCandidateWitness(*prepared_commit_witness_,
                                                  *observed)) {
      return base::unexpected(Error::kWitnessMismatch);
    }
  } else {
    prepared_commit_witness_ = observed.Clone();
  }
  // From the first successful preparation onward, losing this in-memory
  // owner must not silently delete the staged bytes. The browser may append a
  // durable commit intent immediately after this return, and restart recovery
  // must retain exact cleanup custody even if the owner is then destroyed.
  restore_stage_->RetainForRecoveryOnDestruction();
  return observed;
}

base::expected<Outcome, Error> DormantBackupRestoreTarget::CommitAuthorized(
    mojom::BackupRestorePlanResultPtr plan,
    mojom::BackupRestoreCommitAuthorizationPtr authorization,
    mojom::BackupRestoreCandidateWitnessPtr expected_witness) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (commit_authorization_consumed_) {
    return base::unexpected(Error::kAuthorizationConsumed);
  }
  if (!restore_stage_ || !prepared_commit_witness_) {
    return base::unexpected(Error::kCommitNotPrepared);
  }
  const int64_t now = base::TimeTicks::Now().since_origin().InMilliseconds();
  if (now < 0 || !ExactPlan(plan.get(), &restore_stage_->plan()) ||
      !LiveExactCommitAuthorization(plan.get(), authorization.get(),
                                    static_cast<uint64_t>(now))) {
    return base::unexpected(Error::kInvalidAuthorization);
  }
  if (!expected_witness || !target_internal::IsExactCandidateWitness(
                               *prepared_commit_witness_, *expected_witness)) {
    return base::unexpected(Error::kWitnessMismatch);
  }

  auto records = restore_stage_->ReadVerifiedRecords();
  if (!records.has_value()) {
    return base::unexpected(records.error() ==
                                    BackupRestoreStageError::kStorageUnavailable
                                ? Error::kStageUnavailable
                                : Error::kWitnessMismatch);
  }
  auto observed = target_internal::BuildCandidateWitness(*records);
  if (!observed ||
      !target_internal::IsExactCandidateWitness(*observed, *expected_witness)) {
    return base::unexpected(Error::kWitnessMismatch);
  }
  if (!HasOwnedDatabasePath() || !HasNoSqliteSidecars(database_path_) ||
      !HasOwnedStagingPath() ||
      target_internal::InspectCurrentSchema(database_.get(), profile_id_) !=
          target_internal::SchemaMatch::kExact) {
    return base::unexpected(Error::kTargetChanged);
  }
  if (!IsPristineTarget()) {
    return base::unexpected(Error::kTargetNotPristine);
  }

  // Everything above is a safe refusal. From this point the exact authority
  // is spent and no failure is allowed to imply the transaction did not land.
  commit_authorization_consumed_ = true;
  sql::Transaction transaction(database_.get());
  if (!transaction.Begin() || !HasPristineTargetContents()) {
    return Outcome::kOutcomeUnknown;
  }
  for (const auto& entry : plan->entries) {
    const BackupSnapshotRecord* record = FindRecord(*records, *entry);
    const std::string effect_id = target_internal::ImportedMetadataId(
        profile_id_, *expected_witness,
        target_internal::ImportedMetadataRole::kRecord, entry->kind,
        entry->stable_id);
    if (!record || effect_id.empty() ||
        !restore_internal::InsertRecord(database_.get(), *entry,
                                        record->plaintext, effect_id)) {
      return Outcome::kOutcomeUnknown;
    }
  }
  const std::string library_clock = target_internal::ImportedMetadataId(
      profile_id_, *expected_witness,
      target_internal::ImportedMetadataRole::kLibraryCollection,
      mojom::BackupRecordKind::kLibraryEntry, {});
  const std::string memory_clock = target_internal::ImportedMetadataId(
      profile_id_, *expected_witness,
      target_internal::ImportedMetadataRole::kMemoryCollection,
      mojom::BackupRecordKind::kMemoryRecord, {});
  if (library_clock.empty() || memory_clock.empty() ||
      !restore_internal::InitializeCollectionRevisions(
          database_.get(), library_clock, memory_clock) ||
      !transaction.Commit()) {
    return Outcome::kOutcomeUnknown;
  }
  if (after_commit_for_testing_) {
    std::move(after_commit_for_testing_).Run();
  }
  if (!HasOwnedDatabasePath() || !HasNoSqliteSidecars(database_path_) ||
      !exclusive_file_.Flush() || !SyncDirectory(database_path_.DirName()) ||
      target_internal::InspectCommittedProjection(database_.get(), profile_id_,
                                                  *expected_witness) !=
          target_internal::ProjectionMatch::kExact ||
      !HasOwnedDatabasePath()) {
    return Outcome::kOutcomeUnknown;
  }
  return Outcome::kCommitted;
}

}  // namespace taffy::storage::backup
