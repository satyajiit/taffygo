// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "base/strings/cstring_view.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"

namespace taffy::storage::backup::restore_internal {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreStageError;

std::string StageSkillMetadataId(mojom::BackupRecordKind kind,
                                 std::string_view stable_id) {
  std::vector<uint8_t> material;
  const std::string_view domain =
      kind == mojom::BackupRecordKind::kUserAuthoredSkill
          ? "backup-stage-authored-skill-v1"
          : "backup-stage-learned-procedure-v1";
  material.insert(material.end(), domain.begin(), domain.end());
  material.push_back(0u);
  material.insert(material.end(), stable_id.begin(), stable_id.end());
  return "backup-stage-procedure-" +
         base::HexEncodeLower(crypto::hash::Sha256(material));
}

void BindOptional(sql::Statement& statement,
                  int index,
                  const std::optional<std::string>& value) {
  if (value) {
    statement.BindString(index, *value);
  } else {
    statement.BindNull(index);
  }
}

void BindWorkspace(sql::Statement& statement,
                   int index,
                   const mojom::MemoryWorkspaceRecordPtr& workspace) {
  if (workspace) {
    statement.BindString(index, workspace->workspace_id);
    statement.BindString(index + 1, workspace->display_name);
  } else {
    statement.BindNull(index);
    statement.BindNull(index + 1);
  }
}

bool InsertLibrary(sql::Database* database,
                   const mojom::LibraryEntryRecord& record,
                   std::string_view local_effect_id) {
  sql::Statement insert(database->GetUniqueStatement(
      "INSERT INTO core_library_entry(entry_id,revision,collection_id,"
      "collection_name,source_workspace_id,source_workspace_revision,"
      "source_fact_id,field,original_value,correction,kind,captured_at_epoch_"
      "ms,"
      "last_checked_epoch_ms,has_conflict,effect_id,expected_entry_revision) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)"));
  if (!insert.is_valid()) {
    return false;
  }
  insert.BindString(0, record.entry_id);
  insert.BindInt64(1, record.revision);
  insert.BindString(2, record.collection_id);
  insert.BindString(3, record.collection_name);
  insert.BindString(4, record.source_workspace_id);
  insert.BindInt64(5, record.source_workspace_revision);
  insert.BindString(6, record.source_fact_id);
  insert.BindString(7, record.field);
  insert.BindString(8, record.original_value);
  BindOptional(insert, 9, record.correction);
  insert.BindInt(10, static_cast<int>(record.kind));
  insert.BindInt64(11, record.captured_at_epoch_ms);
  insert.BindInt64(12, record.last_checked_epoch_ms);
  insert.BindBool(13, record.has_conflict);
  insert.BindString(14, local_effect_id);
  if (!insert.Run()) {
    return false;
  }
  for (const auto& source : record.sources) {
    sql::Statement citation(database->GetUniqueStatement(
        "INSERT INTO core_library_source(entry_id,source_id,title,host,"
        "observed_at_epoch_ms) VALUES(?,?,?,?,?)"));
    if (!citation.is_valid()) {
      return false;
    }
    citation.BindString(0, record.entry_id);
    citation.BindString(1, source->source_id);
    citation.BindString(2, source->title);
    citation.BindString(3, source->host);
    citation.BindInt64(4, source->observed_at_epoch_ms);
    if (!citation.Run()) {
      return false;
    }
  }
  return true;
}

bool InsertMemory(sql::Database* database,
                  const mojom::MemoryRecord& record,
                  std::string_view local_effect_id) {
  sql::Statement insert(database->GetUniqueStatement(
      "INSERT INTO core_memory_record(memory_id,revision,statement,"
      "source_kind,source_task_id,source_workspace_id,source_workspace_name,"
      "scope_kind,scope_workspace_id,scope_workspace_name,sensitivity,"
      "created_at_epoch_ms,updated_at_epoch_ms,reviewed_at_epoch_ms,"
      "expires_at_epoch_ms,effect_id,expected_record_revision) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)"));
  if (!insert.is_valid()) {
    return false;
  }
  insert.BindString(0, record.memory_id);
  insert.BindInt64(1, record.revision);
  insert.BindString(2, record.statement);
  insert.BindInt(3, static_cast<int>(record.source_kind));
  BindOptional(insert, 4, record.source_task_id);
  BindWorkspace(insert, 5, record.source_workspace);
  insert.BindInt(7, static_cast<int>(record.scope_kind));
  BindWorkspace(insert, 8, record.scope_workspace);
  insert.BindInt(10, static_cast<int>(record.sensitivity));
  insert.BindInt64(11, record.created_at_epoch_ms);
  insert.BindInt64(12, record.updated_at_epoch_ms);
  insert.BindInt64(13, record.reviewed_at_epoch_ms);
  insert.BindInt64(14, record.expires_at_epoch_ms);
  insert.BindString(15, local_effect_id);
  return insert.Run();
}

bool InsertConfiguration(sql::Database* database,
                         const mojom::AssistantConfiguration& record,
                         std::string_view local_effect_id) {
  uint32_t mask = 0;
  // The v1 decoder has already checked the closed, sorted 0..15 abilities.
  for (auto ability : record.disabled_abilities) {
    mask |= 1u << static_cast<uint32_t>(ability);
  }
  sql::Statement insert(database->GetUniqueStatement(
      "INSERT INTO core_assistant_configuration(singleton,revision,"
      "disabled_mask,personality_preset,pace,response_length,check_in,effect_"
      "id)"
      " VALUES(1,?,?,?,?,?,?,?)"));
  if (!insert.is_valid()) {
    return false;
  }
  insert.BindInt64(0, record.revision);
  insert.BindInt64(1, mask);
  insert.BindInt(2, static_cast<int>(record.preset));
  insert.BindInt64(3, record.pace);
  insert.BindInt64(4, record.length);
  insert.BindInt64(5, record.check_in);
  insert.BindString(6, local_effect_id);
  return insert.Run();
}

bool InsertTombstone(sql::Database* database,
                     const mojom::BackupRestorePlanEntry& entry,
                     std::string_view local_effect_id) {
  const bool library = entry.kind == mojom::BackupRecordKind::kLibraryEntry;
  sql::Statement insert(database->GetUniqueStatement(
      library ? base::cstring_view(
                    "INSERT INTO core_library_tombstone(entry_id,revision,"
                    "removed_at_epoch_ms,effect_id,expected_entry_revision) "
                    "VALUES(?,?,0,?,?)")
              : base::cstring_view(
                    "INSERT INTO core_memory_tombstone(memory_id,revision,"
                    "deleted_at_epoch_ms,effect_id,expected_record_revision) "
                    "VALUES(?,?,0,?,?)")));
  if (!insert.is_valid()) {
    return false;
  }
  insert.BindString(0, entry.stable_id);
  insert.BindInt64(1, entry.archive_revision);
  insert.BindString(2, local_effect_id);
  insert.BindInt64(3, entry.archive_revision - 1);
  return insert.Run();
}

base::expected<void, Error> StorageResult(bool stored) {
  if (!stored) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  return base::ok();
}

}  // namespace

bool CreateSchema(sql::Database* database, std::string_view profile_id) {
  if (!database->Execute(storage_schema::kLedgerStatement)) {
    return false;
  }
  for (auto statement : storage_schema::kStatements) {
    if (!database->Execute(statement)) {
      return false;
    }
  }
  sql::Statement ledger(database->GetUniqueStatement(
      "INSERT INTO taffy_storage_schema(component,version,checksum) "
      "VALUES(?,?,?)"));
  sql::Statement identity(database->GetUniqueStatement(
      "INSERT INTO core_profile_identity(singleton,browser_profile_id) "
      "VALUES(1,?)"));
  if (!ledger.is_valid() || !identity.is_valid()) {
    return false;
  }
  ledger.BindString(0, storage_schema::kComponent);
  ledger.BindInt(1, static_cast<int>(storage_schema::kVersion));
  ledger.BindString(2, storage_schema::kChecksum);
  identity.BindString(0, profile_id);
  return ledger.Run() && identity.Run();
}

base::expected<void, Error> InsertRecord(
    sql::Database* database,
    const mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext) {
  std::string local_effect_id;
  if (entry.state == mojom::BackupRecordState::kTombstone) {
    local_effect_id = "backup-stage-deletion-" + entry.stable_id;
  } else if (entry.kind == mojom::BackupRecordKind::kSavedWorkspace) {
    local_effect_id = "backup-stage-workspace-" + entry.stable_id;
  } else if (entry.kind == mojom::BackupRecordKind::kLibraryEntry) {
    local_effect_id = "backup-stage-library-" + entry.stable_id;
  } else if (entry.kind == mojom::BackupRecordKind::kMemoryRecord) {
    local_effect_id = "backup-stage-memory-" + entry.stable_id;
  } else if (entry.kind == mojom::BackupRecordKind::kUserAuthoredSkill) {
    local_effect_id = StageSkillMetadataId(entry.kind, entry.stable_id);
  } else if (entry.kind == mojom::BackupRecordKind::kLearnedProcedure) {
    local_effect_id = StageSkillMetadataId(entry.kind, entry.stable_id);
  } else {
    local_effect_id = "backup-stage-configuration";
  }
  return InsertRecord(database, entry, plaintext, local_effect_id);
}

base::expected<void, Error> InsertRecord(
    sql::Database* database,
    const mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext,
    std::string_view local_effect_id) {
  if (local_effect_id.empty() ||
      local_effect_id.size() > mojom::kMaxIdentifierBytes) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  if (entry.state == mojom::BackupRecordState::kTombstone) {
    if (entry.kind == mojom::BackupRecordKind::kSavedWorkspace) {
      return InsertExtendedRecord(database, entry, plaintext, local_effect_id);
    }
    if (entry.kind != mojom::BackupRecordKind::kLibraryEntry &&
        entry.kind != mojom::BackupRecordKind::kMemoryRecord) {
      return base::unexpected(Error::kUnsupportedRecord);
    }
    if (!plaintext.empty()) {
      return base::unexpected(Error::kPayloadMismatch);
    }
    return StorageResult(InsertTombstone(database, entry, local_effect_id));
  }
  switch (entry.kind) {
    case mojom::BackupRecordKind::kLibraryEntry: {
      auto decoded = DecodeLibraryRecordV1(plaintext, entry.stable_id,
                                           entry.archive_revision);
      if (!decoded.has_value()) {
        return base::unexpected(Error::kPayloadMismatch);
      }
      return StorageResult(InsertLibrary(database, **decoded, local_effect_id));
    }
    case mojom::BackupRecordKind::kMemoryRecord: {
      auto decoded = DecodeMemoryRecordV1(plaintext, entry.stable_id,
                                          entry.archive_revision);
      if (!decoded.has_value()) {
        return base::unexpected(Error::kPayloadMismatch);
      }
      return StorageResult(InsertMemory(database, **decoded, local_effect_id));
    }
    case mojom::BackupRecordKind::kAssistantConfiguration: {
      auto decoded =
          DecodeAssistantConfigurationV1(plaintext, entry.archive_revision);
      if (!decoded.has_value()) {
        return base::unexpected(Error::kPayloadMismatch);
      }
      return StorageResult(
          InsertConfiguration(database, **decoded, local_effect_id));
    }
    case mojom::BackupRecordKind::kSavedWorkspace:
    case mojom::BackupRecordKind::kUserAuthoredSkill:
    case mojom::BackupRecordKind::kLearnedProcedure:
      return InsertExtendedRecord(database, entry, plaintext, local_effect_id);
    default:
      return base::unexpected(Error::kUnsupportedRecord);
  }
}

bool InitializeCollectionRevisions(sql::Database* database) {
  return InitializeCollectionRevisions(database, "backup-stage-library-state",
                                       "backup-stage-memory-state");
}

bool InitializeCollectionRevisions(sql::Database* database,
                                   std::string_view library_effect_id,
                                   std::string_view memory_effect_id) {
  if (library_effect_id.empty() || memory_effect_id.empty() ||
      library_effect_id.size() > mojom::kMaxIdentifierBytes ||
      memory_effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  // These are new local collection clocks, not clocks imported from a device
  // or sync peer. Per-record archive revisions are retained independently.
  sql::Statement library(database->GetUniqueStatement(
      "INSERT INTO core_library_state SELECT 1,1,?,0 WHERE EXISTS("
      "SELECT 1 FROM core_library_entry UNION ALL "
      "SELECT 1 FROM core_library_tombstone)"));
  sql::Statement memory(database->GetUniqueStatement(
      "INSERT INTO core_memory_state SELECT 1,1,?,0 WHERE EXISTS("
      "SELECT 1 FROM core_memory_record UNION ALL "
      "SELECT 1 FROM core_memory_tombstone)"));
  if (!library.is_valid() || !memory.is_valid()) {
    return false;
  }
  library.BindString(0, library_effect_id);
  memory.BindString(0, memory_effect_id);
  return library.Run() && memory.Run();
}

}  // namespace taffy::storage::backup::restore_internal
