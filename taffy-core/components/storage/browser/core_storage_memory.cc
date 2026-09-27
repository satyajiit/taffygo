// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_memory.h"

#include <optional>
#include <string_view>

#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_memory_validation.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

std::optional<uint64_t> CurrentMemoryRevision(sql::Database* database) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision FROM core_memory_state WHERE singleton=1"));
  if (!query.Step()) {
    return query.Succeeded() ? std::optional<uint64_t>(0u) : std::nullopt;
  }
  const int64_t revision = query.ColumnInt64(0);
  return revision > 0 ? std::optional<uint64_t>(revision) : std::nullopt;
}

std::optional<uint64_t> CurrentRecordRevision(sql::Database* database,
                                              std::string_view memory_id) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision FROM core_memory_record WHERE memory_id=?"));
  query.BindString(0, memory_id);
  if (!query.Step()) {
    return query.Succeeded() ? std::optional<uint64_t>(0u) : std::nullopt;
  }
  const int64_t revision = query.ColumnInt64(0);
  return revision > 0 ? std::optional<uint64_t>(revision) : std::nullopt;
}

bool OptionalColumnMatches(sql::Statement& query,
                           int column,
                           const std::optional<std::string>& value) {
  return value ? query.GetColumnType(column) != sql::ColumnType::kNull &&
                     query.ColumnString(column) == *value
               : query.GetColumnType(column) == sql::ColumnType::kNull;
}

bool OptionalWorkspaceMatches(
    sql::Statement& query,
    int id_column,
    int name_column,
    const mojom::MemoryWorkspaceRecordPtr& workspace) {
  return workspace
             ? query.GetColumnType(id_column) != sql::ColumnType::kNull &&
                   query.GetColumnType(name_column) != sql::ColumnType::kNull &&
                   query.ColumnString(id_column) == workspace->workspace_id &&
                   query.ColumnString(name_column) == workspace->display_name
             : query.GetColumnType(id_column) == sql::ColumnType::kNull &&
                   query.GetColumnType(name_column) == sql::ColumnType::kNull;
}

bool StoredRecordMatches(sql::Database* database,
                         const mojom::MemoryPersistEffect& persisted,
                         std::string_view effect_id) {
  const mojom::MemoryRecord& record = *persisted.record;
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,statement,source_kind,source_task_id,"
      "source_workspace_id,source_workspace_name,scope_kind,"
      "scope_workspace_id,scope_workspace_name,sensitivity,created_at_epoch_ms,"
      "updated_at_epoch_ms,reviewed_at_epoch_ms,expires_at_epoch_ms,"
      "expected_record_revision FROM core_memory_record "
      "WHERE memory_id=? AND effect_id=?"));
  query.BindString(0, record.memory_id);
  query.BindString(1, effect_id);
  return query.Step() &&
         query.ColumnInt64(0) == static_cast<int64_t>(record.revision) &&
         query.ColumnString(1) == record.statement &&
         query.ColumnInt(2) == static_cast<int>(record.source_kind) &&
         OptionalColumnMatches(query, 3, record.source_task_id) &&
         OptionalWorkspaceMatches(query, 4, 5, record.source_workspace) &&
         query.ColumnInt(6) == static_cast<int>(record.scope_kind) &&
         OptionalWorkspaceMatches(query, 7, 8, record.scope_workspace) &&
         query.ColumnInt(9) == static_cast<int>(record.sensitivity) &&
         query.ColumnInt64(10) ==
             static_cast<int64_t>(record.created_at_epoch_ms) &&
         query.ColumnInt64(11) ==
             static_cast<int64_t>(record.updated_at_epoch_ms) &&
         query.ColumnInt64(12) ==
             static_cast<int64_t>(record.reviewed_at_epoch_ms) &&
         query.ColumnInt64(13) ==
             static_cast<int64_t>(record.expires_at_epoch_ms) &&
         query.ColumnInt64(14) ==
             static_cast<int64_t>(persisted.expected_record_revision);
}

bool ImmutableFieldsMatch(sql::Database* database,
                          const mojom::MemoryRecord& record) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT source_kind,source_task_id,source_workspace_id,"
      "source_workspace_name,created_at_epoch_ms FROM core_memory_record "
      "WHERE memory_id=?"));
  query.BindString(0, record.memory_id);
  return query.Step() &&
         query.ColumnInt(0) == static_cast<int>(record.source_kind) &&
         OptionalColumnMatches(query, 1, record.source_task_id) &&
         OptionalWorkspaceMatches(query, 2, 3, record.source_workspace) &&
         query.ColumnInt64(4) ==
             static_cast<int64_t>(record.created_at_epoch_ms);
}

bool GlobalReplayMatches(sql::Database* database,
                         std::string_view effect_id,
                         uint64_t expected_revision,
                         uint64_t resulting_revision) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,expected_revision FROM core_memory_state "
      "WHERE singleton=1 AND effect_id=?"));
  query.BindString(0, effect_id);
  return query.Step() &&
         query.ColumnInt64(0) == static_cast<int64_t>(resulting_revision) &&
         query.ColumnInt64(1) == static_cast<int64_t>(expected_revision);
}

bool AdvanceMemoryRevision(sql::Database* database,
                           std::string_view effect_id,
                           uint64_t expected_revision,
                           uint64_t resulting_revision) {
  if (expected_revision == 0u) {
    sql::Statement insert(database->GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_memory_state(singleton,revision,effect_id,"
        "expected_revision) VALUES(1,?,?,?)"));
    insert.BindInt64(0, static_cast<int64_t>(resulting_revision));
    insert.BindString(1, effect_id);
    insert.BindInt64(2, static_cast<int64_t>(expected_revision));
    return insert.Run() && database->GetLastChangeCount() == 1;
  }
  sql::Statement update(database->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_memory_state SET revision=?,effect_id=?,"
      "expected_revision=? WHERE singleton=1 AND revision=?"));
  update.BindInt64(0, static_cast<int64_t>(resulting_revision));
  update.BindString(1, effect_id);
  update.BindInt64(2, static_cast<int64_t>(expected_revision));
  update.BindInt64(3, static_cast<int64_t>(expected_revision));
  return update.Run() && database->GetLastChangeCount() == 1;
}

void BindOptionalString(sql::Statement& statement,
                        int index,
                        const std::optional<std::string>& value) {
  if (value) {
    statement.BindString(index, *value);
  } else {
    statement.BindNull(index);
  }
}

void BindWorkspace(sql::Statement& statement,
                   int id_index,
                   int name_index,
                   const mojom::MemoryWorkspaceRecordPtr& workspace) {
  if (workspace) {
    statement.BindString(id_index, workspace->workspace_id);
    statement.BindString(name_index, workspace->display_name);
  } else {
    statement.BindNull(id_index);
    statement.BindNull(name_index);
  }
}

bool WriteRecord(sql::Database* database,
                 const mojom::MemoryPersistEffect& persisted,
                 std::string_view effect_id) {
  const mojom::MemoryRecord& record = *persisted.record;
  if (persisted.expected_record_revision != 0u &&
      !ImmutableFieldsMatch(database, record)) {
    return false;
  }
  sql::Statement write(
      persisted.expected_record_revision == 0u
          ? database->GetCachedStatement(
                SQL_FROM_HERE,
                "INSERT INTO core_memory_record(memory_id,revision,statement,"
                "source_kind,source_task_id,source_workspace_id,"
                "source_workspace_name,scope_kind,scope_workspace_id,"
                "scope_workspace_name,sensitivity,created_at_epoch_ms,"
                "updated_at_epoch_ms,reviewed_at_epoch_ms,expires_at_epoch_ms,"
                "effect_id,expected_record_revision) VALUES(?1,?2,?3,?4,?5,"
                "?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)")
          : database->GetCachedStatement(
                SQL_FROM_HERE,
                "UPDATE core_memory_record SET revision=?2,statement=?3,"
                "source_kind=?4,source_task_id=?5,source_workspace_id=?6,"
                "source_workspace_name=?7,scope_kind=?8,scope_workspace_id=?9,"
                "scope_workspace_name=?10,sensitivity=?11,created_at_epoch_ms="
                "?12,updated_at_epoch_ms=?13,reviewed_at_epoch_ms=?14,"
                "expires_at_epoch_ms=?15,effect_id=?16,"
                "expected_record_revision=?17 WHERE memory_id=?1 AND "
                "revision=?17"));
  write.BindString(0, record.memory_id);
  write.BindInt64(1, static_cast<int64_t>(record.revision));
  write.BindString(2, record.statement);
  write.BindInt(3, static_cast<int>(record.source_kind));
  BindOptionalString(write, 4, record.source_task_id);
  BindWorkspace(write, 5, 6, record.source_workspace);
  write.BindInt(7, static_cast<int>(record.scope_kind));
  BindWorkspace(write, 8, 9, record.scope_workspace);
  write.BindInt(10, static_cast<int>(record.sensitivity));
  write.BindInt64(11, static_cast<int64_t>(record.created_at_epoch_ms));
  write.BindInt64(12, static_cast<int64_t>(record.updated_at_epoch_ms));
  write.BindInt64(13, static_cast<int64_t>(record.reviewed_at_epoch_ms));
  write.BindInt64(14, static_cast<int64_t>(record.expires_at_epoch_ms));
  write.BindString(15, effect_id);
  write.BindInt64(16, static_cast<int64_t>(persisted.expected_record_revision));
  if (!write.Run() || database->GetLastChangeCount() != 1) {
    return false;
  }
  sql::Statement clear(database->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_memory_tombstone WHERE memory_id=?"));
  clear.BindString(0, record.memory_id);
  return clear.Run();
}

}  // namespace

bool LoadMemoryRecords(sql::Database* database,
                       std::vector<mojom::MemoryRecordPtr>* records,
                       uint64_t* global_revision) {
  const std::optional<uint64_t> revision = CurrentMemoryRevision(database);
  if (!revision) {
    return false;
  }
  *global_revision = *revision;
  sql::Statement rows(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT memory_id,revision,statement,source_kind,source_task_id,"
      "source_workspace_id,source_workspace_name,scope_kind,"
      "scope_workspace_id,scope_workspace_name,sensitivity,created_at_epoch_ms,"
      "updated_at_epoch_ms,reviewed_at_epoch_ms,expires_at_epoch_ms "
      "FROM core_memory_record ORDER BY memory_id"));
  while (rows.Step()) {
    if (records->size() >= mojom::kMaxMemoryRecords) {
      return false;
    }
    auto record = mojom::MemoryRecord::New();
    record->memory_id = rows.ColumnString(0);
    const int64_t record_revision = rows.ColumnInt64(1);
    record->statement = rows.ColumnString(2);
    const int source_kind = rows.ColumnInt(3);
    if (rows.GetColumnType(4) != sql::ColumnType::kNull) {
      record->source_task_id = rows.ColumnString(4);
    }
    if (rows.GetColumnType(5) != sql::ColumnType::kNull &&
        rows.GetColumnType(6) != sql::ColumnType::kNull) {
      record->source_workspace = mojom::MemoryWorkspaceRecord::New(
          rows.ColumnString(5), rows.ColumnString(6));
    }
    const int scope_kind = rows.ColumnInt(7);
    if (rows.GetColumnType(8) != sql::ColumnType::kNull &&
        rows.GetColumnType(9) != sql::ColumnType::kNull) {
      record->scope_workspace = mojom::MemoryWorkspaceRecord::New(
          rows.ColumnString(8), rows.ColumnString(9));
    }
    const int sensitivity = rows.ColumnInt(10);
    const int64_t created = rows.ColumnInt64(11);
    const int64_t updated = rows.ColumnInt64(12);
    const int64_t reviewed = rows.ColumnInt64(13);
    const int64_t expires = rows.ColumnInt64(14);
    if (record_revision <= 0 || source_kind < 0 || source_kind > 1 ||
        scope_kind < 0 || scope_kind > 1 || sensitivity < 0 ||
        sensitivity > 1 || created < 0 || updated < 0 || reviewed < 0 ||
        expires < 0) {
      return false;
    }
    record->revision = static_cast<uint64_t>(record_revision);
    record->source_kind = static_cast<mojom::MemorySourceKind>(source_kind);
    record->scope_kind = static_cast<mojom::MemoryScopeKind>(scope_kind);
    record->sensitivity = static_cast<mojom::MemorySensitivity>(sensitivity);
    record->created_at_epoch_ms = static_cast<uint64_t>(created);
    record->updated_at_epoch_ms = static_cast<uint64_t>(updated);
    record->reviewed_at_epoch_ms = static_cast<uint64_t>(reviewed);
    record->expires_at_epoch_ms = static_cast<uint64_t>(expires);
    if (!IsValidMemoryRecord(*record, record->revision - 1u)) {
      return false;
    }
    records->push_back(std::move(record));
  }
  return rows.Succeeded() && (*revision != 0u || records->empty());
}

bool LoadMemory(sql::Database* database, mojom::CoreBootstrap* bootstrap) {
  return LoadMemoryRecords(database, &bootstrap->memory_records,
                           &bootstrap->memory_revision);
}

bool CommitMemoryRecord(sql::Database* database,
                        const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (!IsValidMemoryStorageCommitBody(body) || effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  if (GlobalReplayMatches(database, effect.effect_id, body.expected_revision,
                          body.resulting_revision)) {
    return StoredRecordMatches(database, *body.memory_record, effect.effect_id);
  }
  const std::optional<uint64_t> global = CurrentMemoryRevision(database);
  const std::optional<uint64_t> record =
      CurrentRecordRevision(database, body.memory_record->record->memory_id);
  if (!global || !record || *global != body.expected_revision ||
      *record != body.memory_record->expected_record_revision) {
    return false;
  }
  sql::Transaction transaction(database);
  return transaction.Begin() &&
         AdvanceMemoryRevision(database, effect.effect_id,
                               body.expected_revision,
                               body.resulting_revision) &&
         WriteRecord(database, *body.memory_record, effect.effect_id) &&
         transaction.Commit();
}

bool CommitMemoryDeletion(sql::Database* database,
                          const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (!IsValidMemoryStorageCommitBody(body) || effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  if (GlobalReplayMatches(database, effect.effect_id, body.expected_revision,
                          body.resulting_revision)) {
    sql::Statement replay(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT revision,deleted_at_epoch_ms,expected_record_revision FROM "
        "core_memory_tombstone WHERE memory_id=? AND effect_id=?"));
    replay.BindString(0, body.memory_deletion->memory_id);
    replay.BindString(1, effect.effect_id);
    return replay.Step() &&
           replay.ColumnInt64(0) ==
               static_cast<int64_t>(
                   body.memory_deletion->resulting_record_revision) &&
           replay.ColumnInt64(1) ==
               static_cast<int64_t>(
                   body.memory_deletion->deleted_at_epoch_ms) &&
           replay.ColumnInt64(2) ==
               static_cast<int64_t>(
                   body.memory_deletion->expected_record_revision);
  }
  const std::optional<uint64_t> global = CurrentMemoryRevision(database);
  const std::optional<uint64_t> record =
      CurrentRecordRevision(database, body.memory_deletion->memory_id);
  if (!global || !record || *global != body.expected_revision ||
      *record != body.memory_deletion->expected_record_revision) {
    return false;
  }
  sql::Transaction transaction(database);
  if (!transaction.Begin() ||
      !AdvanceMemoryRevision(database, effect.effect_id, body.expected_revision,
                             body.resulting_revision)) {
    return false;
  }
  sql::Statement remove(database->GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM core_memory_record WHERE memory_id=? AND revision=?"));
  remove.BindString(0, body.memory_deletion->memory_id);
  remove.BindInt64(
      1, static_cast<int64_t>(body.memory_deletion->expected_record_revision));
  if (!remove.Run() || database->GetLastChangeCount() != 1) {
    return false;
  }
  sql::Statement tombstone(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO "
      "core_memory_tombstone(memory_id,revision,deleted_at_epoch_ms,"
      "effect_id,expected_record_revision) VALUES(?,?,?,?,?)"));
  tombstone.BindString(0, body.memory_deletion->memory_id);
  tombstone.BindInt64(
      1, static_cast<int64_t>(body.memory_deletion->resulting_record_revision));
  tombstone.BindInt64(
      2, static_cast<int64_t>(body.memory_deletion->deleted_at_epoch_ms));
  tombstone.BindString(3, effect.effect_id);
  tombstone.BindInt64(
      4, static_cast<int64_t>(body.memory_deletion->expected_record_revision));
  return tombstone.Run() && database->GetLastChangeCount() == 1 &&
         transaction.Commit();
}

}  // namespace taffy
