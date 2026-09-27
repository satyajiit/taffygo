// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_storage_snapshot.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "base/strings/cstring_view.h"
#include "crypto/hash.h"
#include "crypto/secure_util.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_storage_snapshot_internal.h"
#include "taffy/components/storage/browser/core_storage_assistant_configuration.h"
#include "taffy/components/storage/browser/core_storage_library.h"
#include "taffy/components/storage/browser/core_storage_memory.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;

bool CanonicalId(std::string_view id) {
  return id.size() == 32 && std::ranges::all_of(id, [](char digit) {
           return (digit >= '0' && digit <= '9') ||
                  (digit >= 'a' && digit <= 'f');
         });
}

bool AppendActive(std::vector<BackupSnapshotRecord>* output,
                  mojom::BackupRecordKind kind,
                  std::string stable_id,
                  uint64_t revision,
                  uint32_t schema_version,
                  EncodedBackupRecord encoded) {
  if (!encoded) {
    return false;
  }
  BackupSnapshotRecord record;
  record.plaintext = std::move(*encoded);
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = revision;
  record.descriptor->schema_version = schema_version;
  record.descriptor->state = mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  output->push_back(std::move(record));
  return true;
}

bool ReadTombstones(sql::Database* database,
                    mojom::BackupRecordKind kind,
                    std::vector<BackupSnapshotRecord>* output) {
  const bool library = kind == mojom::BackupRecordKind::kLibraryEntry;
  sql::Statement rows(database->GetUniqueStatement(
      library
          ? base::cstring_view("SELECT entry_id,revision FROM "
                               "core_library_tombstone ORDER BY entry_id")
          : base::cstring_view("SELECT memory_id,revision FROM "
                               "core_memory_tombstone ORDER BY memory_id")));
  while (rows.Step()) {
    if (output->size() >= mojom::kMaxBackupRecords ||
        rows.GetColumnType(0) != sql::ColumnType::kText ||
        rows.GetColumnType(1) != sql::ColumnType::kInteger ||
        !CanonicalId(rows.ColumnString(0)) || rows.ColumnInt64(1) <= 0) {
      return false;
    }
    BackupSnapshotRecord record;
    record.descriptor = mojom::BackupRecordDescriptor::New();
    record.descriptor->kind = kind;
    record.descriptor->stable_id = rows.ColumnString(0);
    record.descriptor->revision = static_cast<uint64_t>(rows.ColumnInt64(1));
    record.descriptor->schema_version =
        library ? kLibraryBackupSchemaVersion : kMemoryBackupSchemaVersion;
    record.descriptor->state = mojom::BackupRecordState::kTombstone;
    record.descriptor->plaintext_sha256.assign(32, 0);
    output->push_back(std::move(record));
  }
  return rows.Succeeded();
}

bool ReadKind(sql::Database* database,
              mojom::BackupRecordKind kind,
              std::vector<BackupSnapshotRecord>* output) {
  uint64_t ignored_global_revision = 0;
  switch (kind) {
    case mojom::BackupRecordKind::kLibraryEntry: {
      std::vector<mojom::LibraryEntryRecordPtr> entries;
      if (!LoadLibraryRecords(database, &entries, &ignored_global_revision)) {
        return false;
      }
      for (const auto& entry : entries) {
        if (!AppendActive(output, kind, entry->entry_id, entry->revision,
                          kLibraryBackupSchemaVersion,
                          EncodeLibraryRecordV1(*entry))) {
          return false;
        }
      }
      return ReadTombstones(database, kind, output);
    }
    case mojom::BackupRecordKind::kMemoryRecord: {
      std::vector<mojom::MemoryRecordPtr> records;
      if (!LoadMemoryRecords(database, &records, &ignored_global_revision)) {
        return false;
      }
      for (const auto& record : records) {
        if (!AppendActive(output, kind, record->memory_id, record->revision,
                          kMemoryBackupSchemaVersion,
                          EncodeMemoryRecordV1(*record))) {
          return false;
        }
      }
      return ReadTombstones(database, kind, output);
    }
    case mojom::BackupRecordKind::kAssistantConfiguration: {
      mojom::AssistantConfigurationPtr record;
      if (!LoadAssistantConfigurationRecord(database, &record)) {
        return false;
      }
      return !record ||
             AppendActive(output, kind, kAssistantConfigurationBackupStableId,
                          record->revision,
                          kAssistantConfigurationBackupSchemaVersion,
                          EncodeAssistantConfigurationV1(*record));
    }
    case mojom::BackupRecordKind::kSavedWorkspace:
    case mojom::BackupRecordKind::kUserAuthoredSkill:
    case mojom::BackupRecordKind::kLearnedProcedure:
      return snapshot_internal::ReadSavedWorkspaceOrProcedureKind(database,
                                                                  kind, output);
    default:
      return false;
  }
}

}  // namespace

BackupSnapshotRecord::BackupSnapshotRecord() = default;
BackupSnapshotRecord::BackupSnapshotRecord(BackupSnapshotRecord&&) = default;
BackupSnapshotRecord& BackupSnapshotRecord::operator=(
    BackupSnapshotRecord&& other) {
  if (this != &other) {
    crypto::SecureZeroBuffer(plaintext);
    descriptor = std::move(other.descriptor);
    plaintext = std::move(other.plaintext);
  }
  return *this;
}
BackupSnapshotRecord::~BackupSnapshotRecord() {
  crypto::SecureZeroBuffer(plaintext);
}

bool IsSupportedBackupStorageSelection(
    base::span<const mojom::BackupRecordKind> selection) {
  if (selection.empty() || selection.size() > 6) {
    return false;
  }
  std::set<mojom::BackupRecordKind> distinct;
  for (auto kind : selection) {
    if ((kind != mojom::BackupRecordKind::kAssistantConfiguration &&
         kind != mojom::BackupRecordKind::kSavedWorkspace &&
         kind != mojom::BackupRecordKind::kLibraryEntry &&
         kind != mojom::BackupRecordKind::kMemoryRecord &&
         kind != mojom::BackupRecordKind::kUserAuthoredSkill &&
         kind != mojom::BackupRecordKind::kLearnedProcedure) ||
        !distinct.insert(kind).second) {
      return false;
    }
  }
  return true;
}

BackupSnapshotResult ReadSelectedBackupRecords(
    sql::Database* database,
    base::span<const mojom::BackupRecordKind> selection) {
  if (!IsSupportedBackupStorageSelection(selection)) {
    return base::unexpected(BackupSnapshotError::kUnsupportedSelection);
  }
  if (!database || !database->is_open()) {
    return base::unexpected(BackupSnapshotError::kUnavailable);
  }
  sql::Transaction snapshot(database);
  if (!snapshot.Begin()) {
    return base::unexpected(BackupSnapshotError::kUnavailable);
  }
  std::vector<BackupSnapshotRecord> records;
  for (auto kind : selection) {
    if (!ReadKind(database, kind, &records)) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
  }
  if (records.size() > mojom::kMaxBackupRecords) {
    return base::unexpected(BackupSnapshotError::kCapacityExceeded);
  }
  std::set<std::pair<mojom::BackupRecordKind, std::string>> identities;
  uint64_t total_bytes = 0;
  for (const auto& record : records) {
    if (!identities
             .emplace(record.descriptor->kind, record.descriptor->stable_id)
             .second) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
    if (record.plaintext.size() >
        mojom::kMaxBackupPlaintextBytes - total_bytes) {
      return base::unexpected(BackupSnapshotError::kCapacityExceeded);
    }
    total_bytes += record.plaintext.size();
  }
  if (!snapshot.Commit()) {
    return base::unexpected(BackupSnapshotError::kUnavailable);
  }
  return records;
}

}  // namespace taffy::storage::backup
