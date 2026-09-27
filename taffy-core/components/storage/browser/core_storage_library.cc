// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_library.h"

#include <optional>
#include <string>
#include <string_view>

#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_library_validation.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

std::optional<uint64_t> CurrentLibraryRevision(sql::Database* database) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision FROM core_library_state WHERE singleton=1"));
  if (!query.Step()) {
    return query.Succeeded() ? std::optional<uint64_t>(0u) : std::nullopt;
  }
  const int64_t revision = query.ColumnInt64(0);
  return revision > 0 ? std::optional<uint64_t>(revision) : std::nullopt;
}

std::optional<uint64_t> CurrentEntryRevision(sql::Database* database,
                                             std::string_view entry_id) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision FROM core_library_entry WHERE entry_id=?"));
  query.BindString(0, entry_id);
  if (!query.Step()) {
    return query.Succeeded() ? std::optional<uint64_t>(0u) : std::nullopt;
  }
  const int64_t revision = query.ColumnInt64(0);
  return revision > 0 ? std::optional<uint64_t>(revision) : std::nullopt;
}

bool StoredSourcesMatch(sql::Database* database,
                        const mojom::LibraryEntryRecord& entry) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT source_id,title,host,observed_at_epoch_ms "
      "FROM core_library_source WHERE entry_id=? ORDER BY source_id"));
  query.BindString(0, entry.entry_id);
  size_t index = 0u;
  while (query.Step()) {
    if (index >= entry.sources.size()) {
      return false;
    }
    const auto& source = entry.sources[index++];
    if (!source || query.ColumnString(0) != source->source_id ||
        query.ColumnString(1) != source->title ||
        query.ColumnString(2) != source->host ||
        query.ColumnInt64(3) !=
            static_cast<int64_t>(source->observed_at_epoch_ms)) {
      return false;
    }
  }
  return query.Succeeded() && index == entry.sources.size();
}

bool StoredEntryMatches(sql::Database* database,
                        const mojom::LibraryPersistEffect& persisted,
                        std::string_view effect_id) {
  const mojom::LibraryEntryRecord& entry = *persisted.entry;
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,collection_id,collection_name,source_workspace_id,"
      "source_workspace_revision,source_fact_id,field,original_value,"
      "correction,kind,captured_at_epoch_ms,last_checked_epoch_ms,has_conflict,"
      "expected_entry_revision FROM core_library_entry "
      "WHERE entry_id=? AND effect_id=?"));
  query.BindString(0, entry.entry_id);
  query.BindString(1, effect_id);
  if (!query.Step()) {
    return false;
  }
  const bool correction_matches =
      entry.correction ? query.GetColumnType(8) != sql::ColumnType::kNull &&
                             query.ColumnString(8) == *entry.correction
                       : query.GetColumnType(8) == sql::ColumnType::kNull;
  return query.ColumnInt64(0) == static_cast<int64_t>(entry.revision) &&
         query.ColumnString(1) == entry.collection_id &&
         query.ColumnString(2) == entry.collection_name &&
         query.ColumnString(3) == entry.source_workspace_id &&
         query.ColumnInt64(4) ==
             static_cast<int64_t>(entry.source_workspace_revision) &&
         query.ColumnString(5) == entry.source_fact_id &&
         query.ColumnString(6) == entry.field &&
         query.ColumnString(7) == entry.original_value && correction_matches &&
         query.ColumnInt(9) == static_cast<int>(entry.kind) &&
         query.ColumnInt64(10) ==
             static_cast<int64_t>(entry.captured_at_epoch_ms) &&
         query.ColumnInt64(11) ==
             static_cast<int64_t>(entry.last_checked_epoch_ms) &&
         query.ColumnInt(12) == static_cast<int>(entry.has_conflict) &&
         query.ColumnInt64(13) ==
             static_cast<int64_t>(persisted.expected_entry_revision) &&
         StoredSourcesMatch(database, entry);
}

bool GlobalReplayMatches(sql::Database* database,
                         std::string_view effect_id,
                         uint64_t expected_revision,
                         uint64_t resulting_revision) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,expected_revision FROM core_library_state "
      "WHERE singleton=1 AND effect_id=?"));
  query.BindString(0, effect_id);
  return query.Step() &&
         query.ColumnInt64(0) == static_cast<int64_t>(resulting_revision) &&
         query.ColumnInt64(1) == static_cast<int64_t>(expected_revision);
}

bool AdvanceLibraryRevision(sql::Database* database,
                            std::string_view effect_id,
                            uint64_t expected_revision,
                            uint64_t resulting_revision) {
  if (expected_revision == 0u) {
    sql::Statement insert(database->GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_library_state(singleton,revision,effect_id,"
        "expected_revision) VALUES(1,?,?,?)"));
    insert.BindInt64(0, static_cast<int64_t>(resulting_revision));
    insert.BindString(1, effect_id);
    insert.BindInt64(2, static_cast<int64_t>(expected_revision));
    return insert.Run() && database->GetLastChangeCount() == 1;
  }
  sql::Statement update(database->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_library_state SET revision=?,effect_id=?,"
      "expected_revision=? WHERE singleton=1 AND revision=?"));
  update.BindInt64(0, static_cast<int64_t>(resulting_revision));
  update.BindString(1, effect_id);
  update.BindInt64(2, static_cast<int64_t>(expected_revision));
  update.BindInt64(3, static_cast<int64_t>(expected_revision));
  return update.Run() && database->GetLastChangeCount() == 1;
}

bool WriteEntry(sql::Database* database,
                const mojom::LibraryPersistEffect& persisted,
                std::string_view effect_id) {
  const mojom::LibraryEntryRecord& entry = *persisted.entry;
  sql::Statement write(
      persisted.expected_entry_revision == 0u
          ? database->GetCachedStatement(
                SQL_FROM_HERE,
                "INSERT INTO core_library_entry(entry_id,revision,"
                "collection_id,collection_name,source_workspace_id,"
                "source_workspace_revision,source_fact_id,field,"
                "original_value,correction,kind,captured_at_epoch_ms,"
                "last_checked_epoch_ms,has_conflict,effect_id,"
                "expected_entry_revision) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,"
                "?9,?10,?11,?12,?13,?14,?15,?16)")
          : database->GetCachedStatement(
                SQL_FROM_HERE,
                "UPDATE core_library_entry SET revision=?2,collection_id=?3,"
                "collection_name=?4,source_workspace_id=?5,"
                "source_workspace_revision=?6,source_fact_id=?7,field=?8,"
                "original_value=?9,correction=?10,kind=?11,"
                "captured_at_epoch_ms=?12,last_checked_epoch_ms=?13,"
                "has_conflict=?14,effect_id=?15,expected_entry_revision=?16 "
                "WHERE entry_id=?1 AND revision=?16"));
  write.BindString(0, entry.entry_id);
  write.BindInt64(1, static_cast<int64_t>(entry.revision));
  write.BindString(2, entry.collection_id);
  write.BindString(3, entry.collection_name);
  write.BindString(4, entry.source_workspace_id);
  write.BindInt64(5, static_cast<int64_t>(entry.source_workspace_revision));
  write.BindString(6, entry.source_fact_id);
  write.BindString(7, entry.field);
  write.BindString(8, entry.original_value);
  if (entry.correction) {
    write.BindString(9, *entry.correction);
  } else {
    write.BindNull(9);
  }
  write.BindInt(10, static_cast<int>(entry.kind));
  write.BindInt64(11, static_cast<int64_t>(entry.captured_at_epoch_ms));
  write.BindInt64(12, static_cast<int64_t>(entry.last_checked_epoch_ms));
  write.BindInt(13, static_cast<int>(entry.has_conflict));
  write.BindString(14, effect_id);
  write.BindInt64(15, static_cast<int64_t>(persisted.expected_entry_revision));
  if (!write.Run() || database->GetLastChangeCount() != 1) {
    return false;
  }
  sql::Statement clear_tombstone(database->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_library_tombstone WHERE entry_id=?"));
  clear_tombstone.BindString(0, entry.entry_id);
  if (!clear_tombstone.Run()) {
    return false;
  }
  sql::Statement clear_sources(database->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_library_source WHERE entry_id=?"));
  clear_sources.BindString(0, entry.entry_id);
  if (!clear_sources.Run()) {
    return false;
  }
  for (const auto& source : entry.sources) {
    sql::Statement insert(database->GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_library_source(entry_id,source_id,title,host,"
        "observed_at_epoch_ms) VALUES(?,?,?,?,?)"));
    insert.BindString(0, entry.entry_id);
    insert.BindString(1, source->source_id);
    insert.BindString(2, source->title);
    insert.BindString(3, source->host);
    insert.BindInt64(4, static_cast<int64_t>(source->observed_at_epoch_ms));
    if (!insert.Run() || database->GetLastChangeCount() != 1) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool LoadLibraryRecords(sql::Database* database,
                        std::vector<mojom::LibraryEntryRecordPtr>* records,
                        uint64_t* global_revision) {
  const std::optional<uint64_t> revision = CurrentLibraryRevision(database);
  if (!revision) {
    return false;
  }
  *global_revision = *revision;
  sql::Statement entries(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT entry_id,revision,collection_id,collection_name,"
      "source_workspace_id,source_workspace_revision,source_fact_id,field,"
      "original_value,correction,kind,captured_at_epoch_ms,"
      "last_checked_epoch_ms,has_conflict FROM core_library_entry "
      "ORDER BY entry_id"));
  while (entries.Step()) {
    if (records->size() >= mojom::kMaxLibraryEntries) {
      return false;
    }
    auto entry = mojom::LibraryEntryRecord::New();
    entry->entry_id = entries.ColumnString(0);
    const int64_t entry_revision = entries.ColumnInt64(1);
    entry->collection_id = entries.ColumnString(2);
    entry->collection_name = entries.ColumnString(3);
    entry->source_workspace_id = entries.ColumnString(4);
    const int64_t source_workspace_revision = entries.ColumnInt64(5);
    entry->source_fact_id = entries.ColumnString(6);
    entry->field = entries.ColumnString(7);
    entry->original_value = entries.ColumnString(8);
    if (entries.GetColumnType(9) != sql::ColumnType::kNull) {
      entry->correction = entries.ColumnString(9);
    }
    const int kind = entries.ColumnInt(10);
    const int64_t captured = entries.ColumnInt64(11);
    const int64_t checked = entries.ColumnInt64(12);
    const int conflict = entries.ColumnInt(13);
    if (entry_revision <= 0 || source_workspace_revision <= 0 || kind < 0 ||
        kind > 3 || captured < 0 || checked < 0 || conflict < 0 ||
        conflict > 1) {
      return false;
    }
    entry->revision = static_cast<uint64_t>(entry_revision);
    entry->source_workspace_revision =
        static_cast<uint64_t>(source_workspace_revision);
    entry->kind = static_cast<mojom::LibraryFactKind>(kind);
    entry->captured_at_epoch_ms = static_cast<uint64_t>(captured);
    entry->last_checked_epoch_ms = static_cast<uint64_t>(checked);
    entry->has_conflict = conflict == 1;

    sql::Statement sources(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT source_id,title,host,observed_at_epoch_ms "
        "FROM core_library_source WHERE entry_id=? ORDER BY source_id"));
    sources.BindString(0, entry->entry_id);
    while (sources.Step()) {
      if (entry->sources.size() >= mojom::kMaxLibrarySources ||
          sources.ColumnInt64(3) < 0) {
        return false;
      }
      auto source = mojom::LibrarySourceRecord::New();
      source->source_id = sources.ColumnString(0);
      source->title = sources.ColumnString(1);
      source->host = sources.ColumnString(2);
      source->observed_at_epoch_ms =
          static_cast<uint64_t>(sources.ColumnInt64(3));
      entry->sources.push_back(std::move(source));
    }
    if (!sources.Succeeded() ||
        !IsValidLibraryEntry(*entry, entry->revision - 1u)) {
      return false;
    }
    records->push_back(std::move(entry));
  }
  if (!entries.Succeeded() || (*revision == 0u && !records->empty())) {
    return false;
  }
  sql::Statement orphans(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT 1 FROM core_library_source s LEFT JOIN core_library_entry e "
      "ON e.entry_id=s.entry_id WHERE e.entry_id IS NULL LIMIT 1"));
  return !orphans.Step() && orphans.Succeeded();
}

bool LoadLibrary(sql::Database* database, mojom::CoreBootstrap* bootstrap) {
  return LoadLibraryRecords(database, &bootstrap->library_entries,
                            &bootstrap->library_revision);
}

bool CommitLibraryEntry(sql::Database* database,
                        const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (!IsValidLibraryStorageCommitBody(body) || effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  if (GlobalReplayMatches(database, effect.effect_id, body.expected_revision,
                          body.resulting_revision)) {
    return StoredEntryMatches(database, *body.library_entry, effect.effect_id);
  }
  const std::optional<uint64_t> global = CurrentLibraryRevision(database);
  const std::optional<uint64_t> entry =
      CurrentEntryRevision(database, body.library_entry->entry->entry_id);
  if (!global || !entry || *global != body.expected_revision ||
      *entry != body.library_entry->expected_entry_revision) {
    return false;
  }
  sql::Transaction transaction(database);
  return transaction.Begin() &&
         AdvanceLibraryRevision(database, effect.effect_id,
                                body.expected_revision,
                                body.resulting_revision) &&
         WriteEntry(database, *body.library_entry, effect.effect_id) &&
         transaction.Commit();
}

bool CommitLibraryDeletion(sql::Database* database,
                           const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (!IsValidLibraryStorageCommitBody(body) || effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  if (GlobalReplayMatches(database, effect.effect_id, body.expected_revision,
                          body.resulting_revision)) {
    sql::Statement replay(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT revision,removed_at_epoch_ms,expected_entry_revision FROM "
        "core_library_tombstone WHERE entry_id=? AND effect_id=?"));
    replay.BindString(0, body.library_deletion->entry_id);
    replay.BindString(1, effect.effect_id);
    return replay.Step() &&
           replay.ColumnInt64(0) ==
               static_cast<int64_t>(
                   body.library_deletion->resulting_entry_revision) &&
           replay.ColumnInt64(1) ==
               static_cast<int64_t>(
                   body.library_deletion->removed_at_epoch_ms) &&
           replay.ColumnInt64(2) ==
               static_cast<int64_t>(
                   body.library_deletion->expected_entry_revision);
  }
  const std::optional<uint64_t> global = CurrentLibraryRevision(database);
  const std::optional<uint64_t> entry =
      CurrentEntryRevision(database, body.library_deletion->entry_id);
  if (!global || !entry || *global != body.expected_revision ||
      *entry != body.library_deletion->expected_entry_revision) {
    return false;
  }
  sql::Transaction transaction(database);
  if (!transaction.Begin() ||
      !AdvanceLibraryRevision(database, effect.effect_id,
                              body.expected_revision,
                              body.resulting_revision)) {
    return false;
  }
  sql::Statement sources(database->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_library_source WHERE entry_id=?"));
  sources.BindString(0, body.library_deletion->entry_id);
  sql::Statement entry_delete(database->GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM core_library_entry WHERE entry_id=? AND revision=?"));
  entry_delete.BindString(0, body.library_deletion->entry_id);
  entry_delete.BindInt64(
      1, static_cast<int64_t>(body.library_deletion->expected_entry_revision));
  if (!sources.Run() || !entry_delete.Run() ||
      database->GetLastChangeCount() != 1) {
    return false;
  }
  sql::Statement tombstone(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO "
      "core_library_tombstone(entry_id,revision,removed_at_epoch_ms,"
      "effect_id,expected_entry_revision) VALUES(?,?,?,?,?)"));
  tombstone.BindString(0, body.library_deletion->entry_id);
  tombstone.BindInt64(
      1, static_cast<int64_t>(body.library_deletion->resulting_entry_revision));
  tombstone.BindInt64(
      2, static_cast<int64_t>(body.library_deletion->removed_at_epoch_ms));
  tombstone.BindString(3, effect.effect_id);
  tombstone.BindInt64(
      4, static_cast<int64_t>(body.library_deletion->expected_entry_revision));
  return tombstone.Run() && database->GetLastChangeCount() == 1 &&
         transaction.Commit();
}

}  // namespace taffy
