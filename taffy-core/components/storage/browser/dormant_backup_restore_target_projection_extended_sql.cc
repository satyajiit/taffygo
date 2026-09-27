// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection_internal.h"

namespace taffy::storage::backup::restore_target_internal {
namespace {

namespace mojom = core_service::mojom;

ProjectionMatch OneWorkspaceMatches(
    sql::Database* database,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness,
    const BackupSnapshotRecord& record) {
  const mojom::BackupRecordDescriptor& descriptor = *record.descriptor;
  const std::string metadata = ImportedMetadataId(
      target_profile_id, witness, ImportedMetadataRole::kRecord,
      descriptor.kind, descriptor.stable_id);
  sql::Statement row(database->GetUniqueStatement(
      "SELECT effect_id,expected_revision FROM core_workspace "
      "WHERE workspace_id=?"));
  row.BindString(0, descriptor.stable_id);
  if (!row.Step()) {
    return row.Succeeded() ? ProjectionMatch::kMismatch
                           : ProjectionMatch::kUnavailable;
  }
  const int64_t expected_revision =
      descriptor.state == mojom::BackupRecordState::kTombstone
          ? static_cast<int64_t>(descriptor.revision - 1u)
          : 0;
  if (metadata.empty() || row.ColumnString(0) != metadata ||
      row.ColumnInt64(1) != expected_revision || row.Step()) {
    return ProjectionMatch::kMismatch;
  }
  return row.Succeeded() ? ProjectionMatch::kExact
                         : ProjectionMatch::kUnavailable;
}

ProjectionMatch OneSkillMatches(
    sql::Database* database,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness,
    const BackupSnapshotRecord& record) {
  const mojom::BackupRecordDescriptor& descriptor = *record.descriptor;
  const std::string metadata = ImportedMetadataId(
      target_profile_id, witness, ImportedMetadataRole::kRecord,
      descriptor.kind, descriptor.stable_id);
  sql::Statement row(database->GetUniqueStatement(
      "SELECT i.effect_id,v.effect_id FROM core_skill_installation i "
      "JOIN core_skill_version v ON v.skill_id=i.skill_id AND "
      "v.version=i.active_version WHERE i.skill_id=?"));
  row.BindString(0, descriptor.stable_id);
  if (!row.Step()) {
    return row.Succeeded() ? ProjectionMatch::kMismatch
                           : ProjectionMatch::kUnavailable;
  }
  if (metadata.empty() || row.ColumnString(0) != metadata ||
      row.ColumnString(1) != metadata || row.Step()) {
    return ProjectionMatch::kMismatch;
  }
  return row.Succeeded() ? ProjectionMatch::kExact
                         : ProjectionMatch::kUnavailable;
}

ProjectionMatch CountMatches(sql::Database* database,
                             std::string_view table,
                             uint64_t expected) {
  sql::Statement row(database->GetUniqueStatement("SELECT COUNT(*) FROM " +
                                                  std::string(table)));
  if (!row.Step()) {
    return ProjectionMatch::kUnavailable;
  }
  const int64_t actual = row.ColumnInt64(0);
  if (actual < 0 || static_cast<uint64_t>(actual) != expected || row.Step()) {
    return ProjectionMatch::kMismatch;
  }
  return row.Succeeded() ? ProjectionMatch::kExact
                         : ProjectionMatch::kUnavailable;
}

}  // namespace

ProjectionMatch ExtendedImportedMetadataMatches(
    sql::Database* database,
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness,
    const std::vector<BackupSnapshotRecord>& records) {
  uint64_t workspaces = 0u;
  uint64_t skills = 0u;
  for (const auto& record : records) {
    const mojom::BackupRecordKind kind = record.descriptor->kind;
    ProjectionMatch match = ProjectionMatch::kExact;
    if (kind == mojom::BackupRecordKind::kSavedWorkspace) {
      ++workspaces;
      match = OneWorkspaceMatches(database, target_profile_id, witness, record);
    } else if (kind == mojom::BackupRecordKind::kUserAuthoredSkill ||
               kind == mojom::BackupRecordKind::kLearnedProcedure) {
      ++skills;
      match = OneSkillMatches(database, target_profile_id, witness, record);
    }
    if (match != ProjectionMatch::kExact) {
      return match;
    }
  }
  const std::array expected_counts = {
      std::pair<std::string_view, uint64_t>("core_workspace", workspaces),
      std::pair<std::string_view, uint64_t>("core_skill_installation", skills),
      std::pair<std::string_view, uint64_t>("core_skill_version", skills),
  };
  for (const auto& [table, expected] : expected_counts) {
    const ProjectionMatch match = CountMatches(database, table, expected);
    if (match != ProjectionMatch::kExact) {
      return match;
    }
  }
  return ProjectionMatch::kExact;
}

}  // namespace taffy::storage::backup::restore_target_internal
