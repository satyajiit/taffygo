// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"

namespace taffy::storage::backup::restore_internal {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreStageError;

constexpr std::string_view kDeletionMagic = "TAFFYWSD";
constexpr uint32_t kDeletionSchemaVersion = 1u;
constexpr std::string_view kDeletionMarkerDomain =
    "TaffyBackupSanitizedWorkspaceDeletionV1";

void AppendU32(std::vector<uint8_t>* bytes, uint32_t value) {
  for (size_t index = 0u; index < sizeof(value); ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendU64(std::vector<uint8_t>* bytes, uint64_t value) {
  for (size_t index = 0u; index < sizeof(value); ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendString(std::vector<uint8_t>* bytes, std::string_view value) {
  AppendU32(bytes, static_cast<uint32_t>(value.size()));
  bytes->insert(bytes->end(), value.begin(), value.end());
}

std::vector<uint8_t> SanitizedWorkspaceTombstone(
    const mojom::BackupRestorePlanEntry& entry,
    std::string_view local_effect_id) {
  std::vector<uint8_t> marker_material;
  AppendString(&marker_material, kDeletionMarkerDomain);
  AppendString(&marker_material, local_effect_id);
  AppendString(&marker_material, entry.stable_id);
  AppendU64(&marker_material, entry.archive_revision);
  const std::string marker =
      base::HexEncodeLower(crypto::hash::Sha256(marker_material));

  std::vector<uint8_t> bytes(kDeletionMagic.begin(), kDeletionMagic.end());
  AppendU32(&bytes, kDeletionSchemaVersion);
  AppendString(&bytes, entry.stable_id);
  AppendU64(&bytes, entry.archive_revision - 1u);
  AppendU64(&bytes, entry.archive_revision);
  // Original source/fact/audit counts and the original confirmation receipt
  // are intentionally not archive material. Zero is the exact sanitized
  // deletion projection; `marker` is deterministic local metadata, not a
  // carried decision or authority.
  AppendU64(&bytes, 0u);
  AppendU64(&bytes, 0u);
  AppendU64(&bytes, 0u);
  AppendU64(&bytes, 0u);
  AppendString(&bytes, marker);
  return bytes;
}

base::expected<void, Error> InsertWorkspace(
    sql::Database* database,
    const mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext,
    std::string_view local_effect_id) {
  std::vector<uint8_t> snapshot;
  uint64_t expected_revision = 0u;
  if (entry.state == mojom::BackupRecordState::kTombstone) {
    snapshot = SanitizedWorkspaceTombstone(entry, local_effect_id);
    expected_revision = entry.archive_revision - 1u;
    if (!IsWorkspaceDeletionTombstone(snapshot, entry.stable_id,
                                      entry.archive_revision)) {
      return base::unexpected(Error::kPayloadMismatch);
    }
  } else {
    auto decoded = DecodeSavedWorkspaceRecordV1(plaintext, entry.stable_id,
                                                entry.archive_revision);
    if (!decoded.has_value()) {
      return base::unexpected(Error::kPayloadMismatch);
    }
    snapshot = std::move((*decoded)->snapshot);
  }
  sql::Statement insert(database->GetUniqueStatement(
      "INSERT INTO core_workspace(workspace_id,revision,snapshot,effect_id,"
      "expected_revision) VALUES(?,?,?,?,?)"));
  if (!insert.is_valid()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  insert.BindString(0, entry.stable_id);
  insert.BindInt64(1, static_cast<int64_t>(entry.archive_revision));
  insert.BindBlob(2, snapshot);
  insert.BindString(3, local_effect_id);
  insert.BindInt64(4, static_cast<int64_t>(expected_revision));
  if (!insert.Run()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  return base::ok();
}

base::expected<void, Error> InsertSkill(
    sql::Database* database,
    const mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext,
    std::string_view local_effect_id) {
  auto decoded = DecodeSkillRecordV1(plaintext, entry.kind, entry.stable_id,
                                     entry.archive_revision);
  if (!decoded.has_value()) {
    return base::unexpected(Error::kPayloadMismatch);
  }
  const mojom::SkillRecord& record = **decoded;
  sql::Statement installation(database->GetUniqueStatement(
      "INSERT INTO core_skill_installation(skill_id,origin,provenance,"
      "active_version,effect_id,installed_at_utc_ms,updated_at_utc_ms) "
      "VALUES(?,?,?,?,?,?,?)"));
  sql::Statement version(database->GetUniqueStatement(
      "INSERT INTO core_skill_version(skill_id,version,status,definition,"
      "step_count,effect_id,created_at_utc_ms) VALUES(?,?,?,?,?,?,?)"));
  if (!installation.is_valid() || !version.is_valid()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  installation.BindString(0, record.skill_id);
  installation.BindString(1, record.origin);
  installation.BindInt(2, static_cast<int>(record.provenance));
  installation.BindInt64(3, static_cast<int64_t>(record.active_version));
  installation.BindString(4, local_effect_id);
  installation.BindInt64(5, static_cast<int64_t>(record.installed_at_utc_ms));
  installation.BindInt64(6, static_cast<int64_t>(record.updated_at_utc_ms));
  version.BindString(0, record.skill_id);
  version.BindInt64(1, static_cast<int64_t>(record.active_version));
  version.BindInt(2, static_cast<int>(record.status));
  version.BindBlob(3, record.definition);
  version.BindInt64(4, static_cast<int64_t>(record.step_count));
  version.BindString(5, local_effect_id);
  version.BindInt64(6, static_cast<int64_t>(record.updated_at_utc_ms));
  if (!installation.Run() || !version.Run()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  return base::ok();
}

}  // namespace

base::expected<void, Error> InsertExtendedRecord(
    sql::Database* database,
    const mojom::BackupRestorePlanEntry& entry,
    base::span<const uint8_t> plaintext,
    std::string_view local_effect_id) {
  if (entry.kind == mojom::BackupRecordKind::kSavedWorkspace) {
    if (entry.state == mojom::BackupRecordState::kTombstone &&
        !plaintext.empty()) {
      return base::unexpected(Error::kPayloadMismatch);
    }
    return InsertWorkspace(database, entry, plaintext, local_effect_id);
  }
  if (entry.kind != mojom::BackupRecordKind::kUserAuthoredSkill &&
      entry.kind != mojom::BackupRecordKind::kLearnedProcedure) {
    return base::unexpected(Error::kUnsupportedRecord);
  }
  if (entry.state != mojom::BackupRecordState::kActive) {
    return base::unexpected(Error::kUnsupportedRecord);
  }
  return InsertSkill(database, entry, plaintext, local_effect_id);
}

}  // namespace taffy::storage::backup::restore_internal
