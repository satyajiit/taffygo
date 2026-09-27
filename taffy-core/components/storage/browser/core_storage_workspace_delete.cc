// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <limits>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "base/strings/string_view_util.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"

namespace taffy {
namespace {

constexpr std::string_view kDeletionMagic = "TAFFYWSD";
constexpr uint32_t kDeletionSchemaVersion = 1u;
constexpr size_t kConfirmationTokenBytes = 64u;

bool IsLowerHex(std::string_view value, size_t size) {
  return value.size() == size &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

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

std::vector<uint8_t> EncodeTombstone(const WorkspaceDeletionRequest& request) {
  std::vector<uint8_t> bytes(kDeletionMagic.begin(), kDeletionMagic.end());
  AppendU32(&bytes, kDeletionSchemaVersion);
  AppendString(&bytes, request.workspace_id);
  AppendU64(&bytes, request.expected_revision);
  AppendU64(&bytes, request.resulting_revision);
  AppendU64(&bytes, request.counts.sources);
  AppendU64(&bytes, request.counts.facts);
  AppendU64(&bytes, request.counts.artifact_metadata);
  AppendU64(&bytes, request.counts.derived_indexes);
  AppendString(&bytes, request.confirmation_token);
  return bytes;
}

class TombstoneReader {
 public:
  explicit TombstoneReader(base::span<const uint8_t> bytes) : bytes_(bytes) {}

  bool ReadMagic() {
    if (Remaining() < kDeletionMagic.size()) {
      return false;
    }
    const auto value = bytes_.subspan(offset_, kDeletionMagic.size());
    offset_ += kDeletionMagic.size();
    return std::equal(value.begin(), value.end(), kDeletionMagic.begin());
  }

  bool ReadU32(uint32_t* value) {
    if (Remaining() < sizeof(*value)) {
      return false;
    }
    *value = 0u;
    for (size_t index = 0u; index < sizeof(*value); ++index) {
      *value |= static_cast<uint32_t>(bytes_[offset_ + index]) << (index * 8u);
    }
    offset_ += sizeof(*value);
    return true;
  }

  bool ReadU64(uint64_t* value) {
    if (Remaining() < sizeof(*value)) {
      return false;
    }
    *value = 0u;
    for (size_t index = 0u; index < sizeof(*value); ++index) {
      *value |= static_cast<uint64_t>(bytes_[offset_ + index]) << (index * 8u);
    }
    offset_ += sizeof(*value);
    return true;
  }

  bool ReadString(size_t maximum, std::string_view* value) {
    uint32_t length = 0u;
    if (!ReadU32(&length) || length == 0u || length > maximum ||
        Remaining() < length) {
      return false;
    }
    *value = base::as_string_view(bytes_.subspan(offset_, length));
    offset_ += length;
    return true;
  }

  bool AtEnd() const { return offset_ == bytes_.size(); }

 private:
  size_t Remaining() const { return bytes_.size() - offset_; }

  const base::raw_span<const uint8_t> bytes_;
  size_t offset_ = 0u;
};

bool ValidateRequest(const WorkspaceDeletionRequest& request) {
  return IsLowerHex(request.workspace_id, 32u) && !request.effect_id.empty() &&
         request.effect_id.size() <= core_service::mojom::kMaxIdentifierBytes &&
         request.expected_revision > 0u &&
         request.expected_revision <
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) &&
         request.resulting_revision == request.expected_revision + 1u &&
         request.counts.artifact_metadata <= 2u &&
         request.counts.derived_indexes == 0u &&
         IsLowerHex(request.confirmation_token, kConfirmationTokenBytes);
}

}  // namespace

bool IsWorkspaceDeletionTombstone(base::span<const uint8_t> snapshot,
                                  std::string_view expected_workspace_id,
                                  uint64_t stored_revision) {
  TombstoneReader reader(snapshot);
  uint32_t schema_version = 0u;
  std::string_view workspace_id;
  std::string_view confirmation_token;
  uint64_t expected_revision = 0u;
  uint64_t resulting_revision = 0u;
  WorkspaceDeletionCounts counts;
  return reader.ReadMagic() && reader.ReadU32(&schema_version) &&
         schema_version == kDeletionSchemaVersion &&
         reader.ReadString(32u, &workspace_id) &&
         workspace_id == expected_workspace_id &&
         reader.ReadU64(&expected_revision) && expected_revision > 0u &&
         reader.ReadU64(&resulting_revision) &&
         resulting_revision == stored_revision &&
         resulting_revision == expected_revision + 1u &&
         reader.ReadU64(&counts.sources) && reader.ReadU64(&counts.facts) &&
         reader.ReadU64(&counts.artifact_metadata) &&
         reader.ReadU64(&counts.derived_indexes) &&
         counts.artifact_metadata <= 2u && counts.derived_indexes == 0u &&
         reader.ReadString(kConfirmationTokenBytes, &confirmation_token) &&
         IsLowerHex(confirmation_token, kConfirmationTokenBytes) &&
         reader.AtEnd();
}

bool CommitWorkspaceDeletion(sql::Database* database,
                             const WorkspaceDeletionRequest& request) {
  if (!database || !ValidateRequest(request)) {
    return false;
  }
  const std::vector<uint8_t> tombstone = EncodeTombstone(request);
  sql::Transaction transaction(database);
  if (!transaction.Begin()) {
    return false;
  }
  {
    sql::Statement duplicate(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT workspace_id,revision,snapshot,expected_revision FROM "
        "core_workspace WHERE effect_id=?"));
    duplicate.BindString(0, request.effect_id);
    if (duplicate.Step()) {
      const bool identical =
          duplicate.ColumnString(0) == request.workspace_id &&
          duplicate.ColumnInt64(1) ==
              static_cast<int64_t>(request.resulting_revision) &&
          duplicate.ColumnBlobAsVector(2) == tombstone &&
          duplicate.ColumnInt64(3) ==
              static_cast<int64_t>(request.expected_revision);
      return identical && transaction.Commit();
    }
    if (!duplicate.Succeeded()) {
      return false;
    }
  }

  std::vector<uint8_t> snapshot;
  {
    sql::Statement current(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT revision,snapshot FROM core_workspace WHERE workspace_id=?"));
    current.BindString(0, request.workspace_id);
    if (!current.Step() ||
        current.ColumnInt64(0) !=
            static_cast<int64_t>(request.expected_revision)) {
      return false;
    }
    snapshot = current.ColumnBlobAsVector(1);
  }
  WorkspaceSnapshotShape shape;
  if (!DecodeWorkspaceSnapshotShape(snapshot, request.workspace_id,
                                    request.expected_revision, &shape) ||
      shape.source_count != request.counts.sources ||
      shape.fact_count != request.counts.facts) {
    return false;
  }
  sql::Statement update(database->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_workspace SET revision=?,snapshot=?,effect_id=?,"
      "expected_revision=? WHERE workspace_id=? AND revision=?"));
  update.BindInt64(0, static_cast<int64_t>(request.resulting_revision));
  update.BindBlob(1, tombstone);
  update.BindString(2, request.effect_id);
  update.BindInt64(3, static_cast<int64_t>(request.expected_revision));
  update.BindString(4, request.workspace_id);
  update.BindInt64(5, static_cast<int64_t>(request.expected_revision));
  if (!update.Run() || database->GetLastChangeCount() != 1) {
    return false;
  }
  sql::Statement verify(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,snapshot FROM core_workspace WHERE workspace_id=? AND "
      "effect_id=?"));
  verify.BindString(0, request.workspace_id);
  verify.BindString(1, request.effect_id);
  return verify.Step() &&
         verify.ColumnInt64(0) ==
             static_cast<int64_t>(request.resulting_revision) &&
         verify.ColumnBlobAsVector(1) == tombstone && transaction.Commit();
}

}  // namespace taffy
