// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/strings/cstring_view.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection_internal.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"

namespace taffy::storage::backup::restore_target_internal {
namespace {

namespace mojom = core_service::mojom;

using SchemaObject = std::tuple<std::string, std::string, std::string>;

std::optional<std::vector<SchemaObject>> ReadSchema(sql::Database* database) {
  sql::Statement rows(database->GetUniqueStatement(
      "SELECT type,name,IFNULL(sql,'') FROM sqlite_master"));
  std::vector<SchemaObject> objects;
  while (rows.Step()) {
    objects.emplace_back(rows.ColumnString(0), rows.ColumnString(1),
                         rows.ColumnString(2));
  }
  if (!rows.Succeeded()) {
    return std::nullopt;
  }
  std::ranges::sort(objects);
  return objects;
}

bool IsAllowedProjectionTable(std::string_view name) {
  static constexpr std::array<std::string_view, 11> kAllowed = {
      "core_assistant_configuration",
      "core_library_state",
      "core_library_entry",
      "core_library_source",
      "core_library_tombstone",
      "core_memory_state",
      "core_memory_record",
      "core_memory_tombstone",
      "core_skill_installation",
      "core_skill_version",
      "core_workspace",
  };
  return std::ranges::find(kAllowed, name) != kAllowed.end();
}

bool SafeSchemaIdentifier(std::string_view name) {
  return !name.empty() && std::ranges::all_of(name, [](char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
           byte == '_';
  });
}

ProjectionMatch ForbiddenTablesAreEmpty(sql::Database* database) {
  sql::Statement tables(database->GetUniqueStatement(
      "SELECT name FROM sqlite_schema WHERE type='table' ORDER BY name"));
  while (tables.Step()) {
    const std::string name = tables.ColumnString(0);
    if (name == "taffy_storage_schema" || name == "core_profile_identity" ||
        IsAllowedProjectionTable(name)) {
      continue;
    }
    if (!SafeSchemaIdentifier(name)) {
      return ProjectionMatch::kMismatch;
    }
    sql::Statement row(
        database->GetUniqueStatement("SELECT 1 FROM " + name + " LIMIT 1"));
    if (row.Step()) {
      return ProjectionMatch::kMismatch;
    }
    if (!row.Succeeded()) {
      return ProjectionMatch::kUnavailable;
    }
  }
  return tables.Succeeded() ? ProjectionMatch::kExact
                            : ProjectionMatch::kUnavailable;
}

ProjectionMatch AllContentTablesAreEmpty(sql::Database* database) {
  sql::Statement tables(database->GetUniqueStatement(
      "SELECT name FROM sqlite_schema WHERE type='table' ORDER BY name"));
  while (tables.Step()) {
    const std::string name = tables.ColumnString(0);
    if (name == "taffy_storage_schema" || name == "core_profile_identity") {
      continue;
    }
    if (!SafeSchemaIdentifier(name)) {
      return ProjectionMatch::kMismatch;
    }
    sql::Statement row(
        database->GetUniqueStatement("SELECT 1 FROM " + name + " LIMIT 1"));
    if (row.Step()) {
      return ProjectionMatch::kMismatch;
    }
    if (!row.Succeeded()) {
      return ProjectionMatch::kUnavailable;
    }
  }
  return tables.Succeeded() ? ProjectionMatch::kExact
                            : ProjectionMatch::kUnavailable;
}

ProjectionMatch StateMatches(
    sql::Database* database,
    mojom::BackupRecordKind kind,
    bool expected,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness) {
  const bool library = kind == mojom::BackupRecordKind::kLibraryEntry;
  // The two literals differ in length, so a conditional between them decays to
  // `const char*`, which base::cstring_view does not accept. Building the view
  // inside each branch keeps the array type, and both statements are unchanged.
  sql::Statement row(database->GetUniqueStatement(
      library ? base::cstring_view(
                    "SELECT revision,effect_id,expected_revision FROM "
                    "core_library_state WHERE singleton=1")
              : base::cstring_view(
                    "SELECT revision,effect_id,expected_revision FROM "
                    "core_memory_state WHERE singleton=1")));
  if (!row.Step()) {
    return row.Succeeded() && !expected ? ProjectionMatch::kExact
           : row.Succeeded()            ? ProjectionMatch::kMismatch
                                        : ProjectionMatch::kUnavailable;
  }
  const std::string effect_id =
      ImportedMetadataId(target_profile_id, witness,
                         library ? ImportedMetadataRole::kLibraryCollection
                                 : ImportedMetadataRole::kMemoryCollection,
                         kind, {});
  if (!expected || effect_id.empty() || row.ColumnInt64(0) != 1 ||
      row.ColumnString(1) != effect_id || row.ColumnInt64(2) != 0 ||
      row.Step()) {
    return ProjectionMatch::kMismatch;
  }
  return row.Succeeded() ? ProjectionMatch::kExact
                         : ProjectionMatch::kUnavailable;
}

ProjectionMatch RecordMetadataMatches(
    sql::Database* database,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness,
    const BackupSnapshotRecord& record) {
  const mojom::BackupRecordDescriptor& descriptor = *record.descriptor;
  const std::string expected = ImportedMetadataId(
      target_profile_id, witness, ImportedMetadataRole::kRecord,
      descriptor.kind, descriptor.stable_id);
  if (expected.empty()) {
    return ProjectionMatch::kMismatch;
  }
  if (descriptor.kind == mojom::BackupRecordKind::kAssistantConfiguration) {
    sql::Statement row(database->GetUniqueStatement(
        "SELECT effect_id FROM core_assistant_configuration WHERE "
        "singleton=1"));
    if (!row.Step()) {
      return row.Succeeded() ? ProjectionMatch::kMismatch
                             : ProjectionMatch::kUnavailable;
    }
    if (row.ColumnString(0) != expected || row.Step()) {
      return ProjectionMatch::kMismatch;
    }
    return row.Succeeded() ? ProjectionMatch::kExact
                           : ProjectionMatch::kUnavailable;
  }

  if (descriptor.kind == mojom::BackupRecordKind::kSavedWorkspace ||
      descriptor.kind == mojom::BackupRecordKind::kUserAuthoredSkill ||
      descriptor.kind == mojom::BackupRecordKind::kLearnedProcedure) {
    return ProjectionMatch::kExact;
  }

  const bool library =
      descriptor.kind == mojom::BackupRecordKind::kLibraryEntry;
  const bool tombstone =
      descriptor.state == mojom::BackupRecordState::kTombstone;
  const std::string sql =
      library ? tombstone
                    ? "SELECT "
                      "effect_id,expected_entry_revision,removed_at_epoch_ms "
                      "FROM core_library_tombstone WHERE entry_id=?"
                    : "SELECT effect_id,expected_entry_revision,0 FROM "
                      "core_library_entry WHERE entry_id=?"
      : tombstone
          ? "SELECT effect_id,expected_record_revision,deleted_at_epoch_ms "
            "FROM core_memory_tombstone WHERE memory_id=?"
          : "SELECT effect_id,expected_record_revision,0 FROM "
            "core_memory_record WHERE memory_id=?";
  sql::Statement row(database->GetUniqueStatement(sql));
  row.BindString(0, descriptor.stable_id);
  if (!row.Step()) {
    return row.Succeeded() ? ProjectionMatch::kMismatch
                           : ProjectionMatch::kUnavailable;
  }
  const int64_t expected_revision =
      tombstone ? static_cast<int64_t>(descriptor.revision - 1u) : 0;
  if (row.ColumnString(0) != expected ||
      row.ColumnInt64(1) != expected_revision || row.ColumnInt64(2) != 0 ||
      row.Step()) {
    return ProjectionMatch::kMismatch;
  }
  return row.Succeeded() ? ProjectionMatch::kExact
                         : ProjectionMatch::kUnavailable;
}

ProjectionMatch MetadataMatches(
    sql::Database* database,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness,
    const std::vector<BackupSnapshotRecord>& records) {
  for (const auto& record : records) {
    const ProjectionMatch match =
        RecordMetadataMatches(database, target_profile_id, witness, record);
    if (match != ProjectionMatch::kExact) {
      return match;
    }
  }
  for (const auto kind : {mojom::BackupRecordKind::kLibraryEntry,
                          mojom::BackupRecordKind::kMemoryRecord}) {
    const bool selected =
        std::ranges::find(witness.selection, kind) != witness.selection.end();
    const ProjectionMatch match =
        StateMatches(database, kind, selected, target_profile_id, witness);
    if (match != ProjectionMatch::kExact) {
      return match;
    }
  }
  const ProjectionMatch extended = ExtendedImportedMetadataMatches(
      database, target_profile_id, witness, records);
  if (extended != ProjectionMatch::kExact) {
    return extended;
  }
  sql::Statement orphan(database->GetUniqueStatement(
      "SELECT 1 FROM core_library_source s LEFT JOIN core_library_entry e "
      "ON e.entry_id=s.entry_id WHERE e.entry_id IS NULL LIMIT 1"));
  if (orphan.Step()) {
    return ProjectionMatch::kMismatch;
  }
  return orphan.Succeeded() ? ProjectionMatch::kExact
                            : ProjectionMatch::kUnavailable;
}

}  // namespace

SchemaMatch InspectCurrentSchema(sql::Database* database,
                                 std::string_view target_profile_id) {
  if (!database || !database->is_open()) {
    return SchemaMatch::kUnavailable;
  }
  sql::Statement ledger(database->GetUniqueStatement(
      "SELECT component,version,checksum FROM taffy_storage_schema"));
  if (!ledger.Step()) {
    return ledger.Succeeded() ? SchemaMatch::kMismatch
                              : SchemaMatch::kUnavailable;
  }
  if (ledger.ColumnString(0) != storage_schema::kComponent ||
      ledger.ColumnInt(1) != static_cast<int>(storage_schema::kVersion) ||
      ledger.ColumnString(2) != storage_schema::kChecksum || ledger.Step()) {
    return SchemaMatch::kMismatch;
  }
  if (!ledger.Succeeded()) {
    return SchemaMatch::kUnavailable;
  }
  sql::Statement identity(database->GetUniqueStatement(
      "SELECT browser_profile_id FROM core_profile_identity"));
  if (!identity.Step()) {
    return identity.Succeeded() ? SchemaMatch::kMismatch
                                : SchemaMatch::kUnavailable;
  }
  if (identity.ColumnString(0) != target_profile_id || identity.Step()) {
    return SchemaMatch::kMismatch;
  }
  if (!identity.Succeeded()) {
    return SchemaMatch::kUnavailable;
  }

  sql::Database expected(sql::DatabaseOptions(),
                         sql::Database::Tag("TaffyCore"));
  if (!expected.OpenInMemory() ||
      !restore_internal::CreateSchema(&expected, target_profile_id)) {
    return SchemaMatch::kUnavailable;
  }
  const auto actual_schema = ReadSchema(database);
  const auto expected_schema = ReadSchema(&expected);
  if (!actual_schema || !expected_schema) {
    return SchemaMatch::kUnavailable;
  }
  return *actual_schema == *expected_schema ? SchemaMatch::kExact
                                            : SchemaMatch::kMismatch;
}

ProjectionMatch InspectPristineProjection(sql::Database* database,
                                          std::string_view target_profile_id) {
  const SchemaMatch schema = InspectCurrentSchema(database, target_profile_id);
  if (schema != SchemaMatch::kExact) {
    return schema == SchemaMatch::kUnavailable ? ProjectionMatch::kUnavailable
                                               : ProjectionMatch::kMismatch;
  }
  return AllContentTablesAreEmpty(database);
}

ProjectionMatch InspectCommittedProjection(
    sql::Database* database,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& expected) {
  if (!IsValidCandidateWitness(expected)) {
    return ProjectionMatch::kMismatch;
  }
  const SchemaMatch schema = InspectCurrentSchema(database, target_profile_id);
  if (schema != SchemaMatch::kExact) {
    return schema == SchemaMatch::kUnavailable ? ProjectionMatch::kUnavailable
                                               : ProjectionMatch::kMismatch;
  }
  const ProjectionMatch forbidden = ForbiddenTablesAreEmpty(database);
  if (forbidden != ProjectionMatch::kExact) {
    return forbidden;
  }
  constexpr std::array kSelection = {
      mojom::BackupRecordKind::kAssistantConfiguration,
      mojom::BackupRecordKind::kSavedWorkspace,
      mojom::BackupRecordKind::kLibraryEntry,
      mojom::BackupRecordKind::kMemoryRecord,
      mojom::BackupRecordKind::kUserAuthoredSkill,
      mojom::BackupRecordKind::kLearnedProcedure,
  };
  auto records = ReadSelectedBackupRecords(database, kSelection);
  if (!records.has_value()) {
    return records.error() == BackupSnapshotError::kUnavailable
               ? ProjectionMatch::kUnavailable
               : ProjectionMatch::kMismatch;
  }
  auto observed = BuildCandidateWitness(*records);
  if (!observed || !IsExactCandidateWitness(*observed, expected)) {
    return ProjectionMatch::kMismatch;
  }
  return MetadataMatches(database, target_profile_id, expected, *records);
}

}  // namespace taffy::storage::backup::restore_target_internal
