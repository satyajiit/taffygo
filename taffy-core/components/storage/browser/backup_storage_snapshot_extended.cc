// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_storage_snapshot_internal.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"

namespace taffy::storage::backup::snapshot_internal {
namespace {

namespace mojom = core_service::mojom;

bool CanonicalWorkspaceId(std::string_view value) {
  return value.size() == 32u && std::ranges::all_of(value, [](char digit) {
           return (digit >= '0' && digit <= '9') ||
                  (digit >= 'a' && digit <= 'f');
         });
}

bool AppendActive(std::vector<BackupSnapshotRecord>* output,
                  mojom::BackupRecordKind kind,
                  std::string stable_id,
                  uint64_t revision,
                  EncodedBackupRecord encoded) {
  if (!encoded || output->size() >= mojom::kMaxBackupRecords) {
    return false;
  }
  BackupSnapshotRecord record;
  record.plaintext = std::move(*encoded);
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = revision;
  record.descriptor->schema_version =
      kind == mojom::BackupRecordKind::kSavedWorkspace
          ? kSavedWorkspaceBackupSchemaVersion
          : kSkillBackupSchemaVersion;
  record.descriptor->state = mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  output->push_back(std::move(record));
  return true;
}

bool AppendWorkspaceTombstone(std::vector<BackupSnapshotRecord>* output,
                              std::string stable_id,
                              uint64_t revision) {
  if (output->size() >= mojom::kMaxBackupRecords ||
      !CanonicalWorkspaceId(stable_id) || revision <= 1u) {
    return false;
  }
  BackupSnapshotRecord record;
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = mojom::BackupRecordKind::kSavedWorkspace;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = revision;
  record.descriptor->schema_version = kSavedWorkspaceBackupSchemaVersion;
  record.descriptor->state = mojom::BackupRecordState::kTombstone;
  record.descriptor->plaintext_sha256.assign(crypto::hash::kSha256Size, 0u);
  output->push_back(std::move(record));
  return true;
}

bool ReadWorkspaces(sql::Database* database,
                    std::vector<BackupSnapshotRecord>* output) {
  sql::Statement rows(database->GetUniqueStatement(
      "SELECT workspace_id,revision,snapshot FROM core_workspace "
      "ORDER BY workspace_id"));
  while (rows.Step()) {
    if (rows.GetColumnType(0) != sql::ColumnType::kText ||
        rows.GetColumnType(1) != sql::ColumnType::kInteger ||
        rows.GetColumnType(2) != sql::ColumnType::kBlob) {
      return false;
    }
    const std::string id = rows.ColumnString(0);
    const int64_t revision = rows.ColumnInt64(1);
    std::vector<uint8_t> snapshot = rows.ColumnBlobAsVector(2);
    if (!CanonicalWorkspaceId(id) || revision <= 0) {
      return false;
    }
    if (IsWorkspaceDeletionTombstone(snapshot, id,
                                     static_cast<uint64_t>(revision))) {
      if (!AppendWorkspaceTombstone(output, id,
                                    static_cast<uint64_t>(revision))) {
        return false;
      }
      continue;
    }
    WorkspaceSnapshotShape shape;
    if (!DecodeWorkspaceSnapshotShape(
            snapshot, id, static_cast<uint64_t>(revision), &shape)) {
      return false;
    }
    if (!shape.saved) {
      continue;
    }
    if (output->size() >= mojom::kMaxBackupRecords) {
      return false;
    }
    auto record = mojom::WorkspaceRestoreRecord::New(
        id, static_cast<uint64_t>(revision), std::move(snapshot));
    if (!AppendActive(output, mojom::BackupRecordKind::kSavedWorkspace, id,
                      static_cast<uint64_t>(revision),
                      EncodeSavedWorkspaceRecordV1(*record))) {
      return false;
    }
  }
  return rows.Succeeded();
}

std::optional<mojom::SkillProvenance> ProvenanceForKind(
    mojom::BackupRecordKind kind) {
  if (kind == mojom::BackupRecordKind::kUserAuthoredSkill) {
    return mojom::SkillProvenance::kAuthored;
  }
  if (kind == mojom::BackupRecordKind::kLearnedProcedure) {
    return mojom::SkillProvenance::kRecordedFromTask;
  }
  return std::nullopt;
}

bool ReadSkills(sql::Database* database,
                mojom::BackupRecordKind kind,
                std::vector<BackupSnapshotRecord>* output) {
  const auto expected_provenance = ProvenanceForKind(kind);
  if (!expected_provenance) {
    return false;
  }
  sql::Statement rows(database->GetUniqueStatement(
      "SELECT i.skill_id,i.origin,i.provenance,v.status,i.active_version,"
      "v.definition,v.step_count,i.installed_at_utc_ms,i.updated_at_utc_ms "
      "FROM core_skill_installation i LEFT JOIN core_skill_version v "
      "ON v.skill_id=i.skill_id AND v.version=i.active_version "
      "WHERE i.provenance=? ORDER BY i.skill_id"));
  rows.BindInt(0, static_cast<int>(*expected_provenance));
  while (rows.Step()) {
    if (output->size() >= mojom::kMaxBackupRecords ||
        rows.GetColumnType(0) != sql::ColumnType::kText ||
        rows.GetColumnType(1) != sql::ColumnType::kText ||
        rows.GetColumnType(2) != sql::ColumnType::kInteger ||
        rows.GetColumnType(3) != sql::ColumnType::kInteger ||
        rows.GetColumnType(4) != sql::ColumnType::kInteger ||
        rows.GetColumnType(5) != sql::ColumnType::kBlob ||
        rows.GetColumnType(6) != sql::ColumnType::kInteger ||
        rows.GetColumnType(7) != sql::ColumnType::kInteger ||
        rows.GetColumnType(8) != sql::ColumnType::kInteger) {
      return false;
    }
    const int64_t version = rows.ColumnInt64(4);
    const int64_t installed_at = rows.ColumnInt64(7);
    const int64_t updated_at = rows.ColumnInt64(8);
    if (version <= 0 ||
        version > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
        installed_at < 0 || updated_at < 0) {
      return false;
    }
    auto record = mojom::SkillRecord::New();
    record->skill_id = rows.ColumnString(0);
    record->origin = rows.ColumnString(1);
    record->provenance = static_cast<mojom::SkillProvenance>(rows.ColumnInt(2));
    record->status = static_cast<mojom::SkillStatus>(rows.ColumnInt(3));
    record->active_version = static_cast<uint32_t>(version);
    record->definition = rows.ColumnBlobAsVector(5);
    const int64_t step_count = rows.ColumnInt64(6);
    if (step_count <= 0 ||
        step_count >
            static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    record->step_count = static_cast<uint32_t>(step_count);
    record->installed_at_utc_ms = static_cast<uint64_t>(installed_at);
    record->updated_at_utc_ms = static_cast<uint64_t>(updated_at);
    if (!AppendActive(output, kind, record->skill_id, record->active_version,
                      EncodeSkillRecordV1(*record, kind))) {
      return false;
    }
  }
  return rows.Succeeded();
}

}  // namespace

bool ReadSavedWorkspaceOrProcedureKind(
    sql::Database* database,
    mojom::BackupRecordKind kind,
    std::vector<BackupSnapshotRecord>* output) {
  if (kind == mojom::BackupRecordKind::kSavedWorkspace) {
    return ReadWorkspaces(database, output);
  }
  return ReadSkills(database, kind, output);
}

}  // namespace taffy::storage::backup::snapshot_internal
