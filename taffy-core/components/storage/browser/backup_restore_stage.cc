// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_restore_stage.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreStageError;

bool NonzeroDigest(base::span<const uint8_t> digest) {
  return digest.size() == 32 &&
         std::ranges::any_of(digest, [](uint8_t byte) { return byte != 0; });
}

bool CanonicalId(std::string_view id) {
  return id.size() == 32 && std::ranges::all_of(id, [](char digit) {
           return (digit >= '0' && digit <= '9') ||
                  (digit >= 'a' && digit <= 'f');
         });
}

bool CanonicalSkillId(std::string_view id) {
  return !id.empty() && id.size() <= mojom::kMaxSkillIdBytes &&
         std::ranges::all_of(id, [](char byte) {
           return (byte >= 'a' && byte <= 'z') ||
                  (byte >= '0' && byte <= '9') || byte == '-' || byte == '.';
         });
}

bool IsSkillKind(mojom::BackupRecordKind kind) {
  return kind == mojom::BackupRecordKind::kUserAuthoredSkill ||
         kind == mojom::BackupRecordKind::kLearnedProcedure;
}

bool SupportedEntry(const mojom::BackupRestorePlanEntry& entry) {
  if (entry.archive_revision == 0 ||
      entry.archive_revision > static_cast<uint64_t>(INT64_MAX) ||
      entry.schema_version != 1 || entry.plaintext_sha256.size() != 32) {
    return false;
  }
  if (entry.kind == mojom::BackupRecordKind::kAssistantConfiguration) {
    if (entry.stable_id != kAssistantConfigurationBackupStableId ||
        entry.state != mojom::BackupRecordState::kActive) {
      return false;
    }
  } else if (IsSkillKind(entry.kind)) {
    if (!CanonicalSkillId(entry.stable_id) ||
        entry.state != mojom::BackupRecordState::kActive) {
      return false;
    }
  } else if ((entry.kind != mojom::BackupRecordKind::kSavedWorkspace &&
              entry.kind != mojom::BackupRecordKind::kLibraryEntry &&
              entry.kind != mojom::BackupRecordKind::kMemoryRecord) ||
             !CanonicalId(entry.stable_id)) {
    return false;
  }
  switch (entry.state) {
    case mojom::BackupRecordState::kActive:
      return entry.action == mojom::BackupRestoreAction::kStageCreate &&
             entry.plaintext_bytes > 0 &&
             entry.plaintext_bytes <= mojom::kMaxBackupRecordBytes &&
             NonzeroDigest(entry.plaintext_sha256);
    case mojom::BackupRecordState::kTombstone:
      // These local stores create a deletion only after revision 1 existed.
      return !IsSkillKind(entry.kind) &&
             entry.kind !=
                 mojom::BackupRecordKind::kAssistantConfiguration &&
             entry.action == mojom::BackupRestoreAction::kStageDeletion &&
             entry.archive_revision > 1 && entry.plaintext_bytes == 0 &&
             std::ranges::all_of(entry.plaintext_sha256,
                                 [](uint8_t byte) { return byte == 0; });
  }
  return false;
}

base::expected<uint64_t, Error> ValidatePlan(
    const mojom::BackupRestorePlanResult& plan,
    base::span<const uint8_t> confirmed_digest) {
  if (plan.status != mojom::BackupPlanningStatus::kSucceeded ||
      !plan.operation || plan.operation->operation_id.empty() || !plan.target ||
      plan.target->kind != mojom::BackupRestoreTargetKind::kNewRegularProfile ||
      plan.target->profile_id.empty() ||
      plan.target->profile_id.size() > mojom::kMaxBackupIdBytes ||
      plan.has_conflicts || plan.backup_id.empty() ||
      plan.backup_id.size() > mojom::kMaxBackupIdBytes ||
      plan.entries.size() > mojom::kMaxBackupRecords ||
      !NonzeroDigest(plan.snapshot_sha256) ||
      !NonzeroDigest(plan.confirmation_sha256) ||
      !std::ranges::equal(confirmed_digest, plan.confirmation_sha256)) {
    return base::unexpected(Error::kPlanRefused);
  }
  uint64_t total = 0;
  std::set<std::pair<mojom::BackupRecordKind, std::string>> identities;
  std::set<std::string> skill_ids;
  std::map<mojom::BackupRecordKind, size_t> active_counts;
  for (const auto& entry : plan.entries) {
    if (!entry || !SupportedEntry(*entry)) {
      return base::unexpected(Error::kUnsupportedRecord);
    }
    if (!identities.emplace(entry->kind, entry->stable_id).second ||
        (IsSkillKind(entry->kind) &&
         !skill_ids.insert(entry->stable_id).second) ||
        entry->plaintext_bytes > mojom::kMaxBackupPlaintextBytes - total) {
      return base::unexpected(Error::kPlanRefused);
    }
    total += entry->plaintext_bytes;
    if (entry->state == mojom::BackupRecordState::kActive) {
      ++active_counts[entry->kind];
    }
  }
  if (active_counts[mojom::BackupRecordKind::kLibraryEntry] >
          mojom::kMaxLibraryEntries ||
      active_counts[mojom::BackupRecordKind::kMemoryRecord] >
          mojom::kMaxMemoryRecords ||
      active_counts[mojom::BackupRecordKind::kSavedWorkspace] >
          mojom::kMaxWorkspacesPerProfile ||
      active_counts[mojom::BackupRecordKind::kUserAuthoredSkill] +
              active_counts[mojom::BackupRecordKind::kLearnedProcedure] >
          mojom::kMaxSkillsPerProfile ||
      active_counts[mojom::BackupRecordKind::kAssistantConfiguration] > 1) {
    return base::unexpected(Error::kPlanRefused);
  }
  return total;
}

bool Matches(const mojom::BackupRecordDescriptor& actual,
             const mojom::BackupRestorePlanEntry& expected) {
  return actual.kind == expected.kind &&
         actual.stable_id == expected.stable_id &&
         actual.revision == expected.archive_revision &&
         actual.schema_version == expected.schema_version &&
         actual.state == expected.state &&
         actual.plaintext_bytes == expected.plaintext_bytes &&
         actual.plaintext_sha256 == expected.plaintext_sha256;
}

}  // namespace

base::expected<std::unique_ptr<BackupRestoreStage>, Error>
BackupRestoreStage::Create(const base::FilePath& staging_parent,
                           const mojom::BackupRestorePlanResult& plan,
                           base::span<const uint8_t> confirmed_digest,
                           base::File plaintext_payload) {
  auto validated = ValidatePlan(plan, confirmed_digest);
  if (!validated.has_value()) {
    return base::unexpected(validated.error());
  }
  if (!plaintext_payload.IsValid() ||
      plaintext_payload.GetLength() != static_cast<int64_t>(*validated)) {
    return base::unexpected(Error::kPayloadMismatch);
  }
  auto stage =
      std::unique_ptr<BackupRestoreStage>(new BackupRestoreStage(plan.Clone()));
  auto result = stage->Build(staging_parent, std::move(plaintext_payload));
  if (!result) {
    return base::unexpected(result.error());
  }
  if (!stage->Verify()) {
    return base::unexpected(Error::kPayloadMismatch);
  }
  return stage;
}

BackupRestoreStage::BackupRestoreStage(mojom::BackupRestorePlanResultPtr plan)
    : plan_(std::move(plan)) {}
BackupRestoreStage::~BackupRestoreStage() {
  if (retain_for_recovery_on_destruction_) {
    // ScopedTempDir normally performs unverified best-effort deletion from its
    // destructor. Once commit preparation has succeeded, durable browser
    // custody—not object lifetime—must decide when this stage is removed.
    (void)directory_.Take();
  }
}

void BackupRestoreStage::RetainForRecoveryOnDestruction() {
  retain_for_recovery_on_destruction_ = true;
}

base::expected<void, Error> BackupRestoreStage::Build(
    const base::FilePath& staging_parent,
    base::File plaintext_payload) {
  if (staging_parent.empty() ||
      !directory_.CreateUniqueTempDirUnderPath(
          staging_parent, kBackupRestoreStageDirectoryPrefix)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  database_path_ = directory_.GetPath().AppendASCII("restore.sqlite3");
  sql::Database database(sql::DatabaseOptions().set_wal_mode(false),
                         sql::Database::Tag("TaffyCore"));
  if (!database.Open(database_path_)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  sql::Transaction transaction(&database);
  if (!transaction.Begin() ||
      !restore_internal::CreateSchema(&database, plan_->target->profile_id)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  uint64_t offset = 0;
  for (const auto& entry : plan_->entries) {
    // The local holder wipes each plaintext buffer even on decode/SQL failure.
    BackupSnapshotRecord record;
    record.plaintext.resize(static_cast<size_t>(entry->plaintext_bytes));
    if (!record.plaintext.empty() &&
        (!plaintext_payload.ReadAndCheck(static_cast<int64_t>(offset),
                                         record.plaintext) ||
         !std::ranges::equal(crypto::hash::Sha256(record.plaintext),
                             entry->plaintext_sha256))) {
      return base::unexpected(Error::kPayloadMismatch);
    }
    auto inserted =
        restore_internal::InsertRecord(&database, *entry, record.plaintext);
    if (!inserted.has_value()) {
      return base::unexpected(inserted.error());
    }
    offset += entry->plaintext_bytes;
  }
  // A concurrent append/truncate cannot turn a previously measured range into
  // a different admitted file. Every byte used above has its own digest check.
  if (plaintext_payload.GetLength() != static_cast<int64_t>(offset)) {
    return base::unexpected(Error::kPayloadMismatch);
  }
  if (!restore_internal::InitializeCollectionRevisions(&database) ||
      !transaction.Commit()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  return base::ok();
}

bool BackupRestoreStage::Verify() const {
  return ReadVerifiedRecords().has_value();
}

base::expected<std::vector<BackupSnapshotRecord>, BackupRestoreStageError>
BackupRestoreStage::ReadVerifiedRecords() const {
  if (database_path_.empty() || !base::PathExists(database_path_) ||
      base::IsLink(database_path_)) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  // Verification is observation: it must never recreate a removed candidate.
  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         sql::Database::Tag("TaffyCore"));
  if (!database.Open(database_path_) ||
      !database.Execute("PRAGMA query_only=ON")) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  sql::Statement identity(
      database.GetUniqueStatement("SELECT browser_profile_id FROM "
                                  "core_profile_identity WHERE singleton=1"));
  if (!identity.Step() ||
      identity.ColumnString(0) != plan_->target->profile_id) {
    return base::unexpected(Error::kPayloadMismatch);
  }
  auto records = ReadSelectedBackupRecords(
      &database, std::array{mojom::BackupRecordKind::kAssistantConfiguration,
                            mojom::BackupRecordKind::kSavedWorkspace,
                            mojom::BackupRecordKind::kLibraryEntry,
                            mojom::BackupRecordKind::kMemoryRecord,
                            mojom::BackupRecordKind::kUserAuthoredSkill,
                            mojom::BackupRecordKind::kLearnedProcedure});
  if (!records) {
    return base::unexpected(records.error() == BackupSnapshotError::kUnavailable
                                ? Error::kStorageUnavailable
                                : Error::kPayloadMismatch);
  }
  if (records->size() != plan_->entries.size()) {
    return base::unexpected(Error::kPayloadMismatch);
  }
  std::map<std::pair<mojom::BackupRecordKind, std::string>,
           const mojom::BackupRestorePlanEntry*>
      expected;
  for (const auto& entry : plan_->entries) {
    expected.emplace(std::make_pair(entry->kind, entry->stable_id),
                     entry.get());
  }
  for (const auto& record : *records) {
    const auto found =
        expected.find({record.descriptor->kind, record.descriptor->stable_id});
    if (found == expected.end() ||
        !Matches(*record.descriptor, *found->second)) {
      return base::unexpected(Error::kPayloadMismatch);
    }
  }
  return std::move(*records);
}

bool BackupRestoreStage::Delete() {
  if (database_path_.empty()) {
    return true;
  }
  if ((deletion_gate_for_testing_ && !deletion_gate_for_testing_.Run()) ||
      !directory_.Delete()) {
    return false;
  }
  retain_for_recovery_on_destruction_ = false;
  database_path_.clear();
  return true;
}

}  // namespace taffy::storage::backup
