// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_bookmark_forest_validation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "components/bookmarks/browser/bookmark_uuids.h"
#include "crypto/secure_util.h"

namespace taffy {
namespace {

constexpr size_t kMaxNodes = 10'000u;
constexpr size_t kMaxDepth = 128u;
constexpr size_t kMaxBytes = 32u * 1024u * 1024u;

std::optional<size_t> RootIndex(std::string_view parent_id) {
  using namespace storage::backup;
  if (parent_id == kBookmarkBarBackupRoot) {
    return 0u;
  }
  if (parent_id == kOtherBookmarksBackupRoot) {
    return 1u;
  }
  if (parent_id == kMobileBookmarksBackupRoot) {
    return 2u;
  }
  return std::nullopt;
}

bool IsReservedOrBannedUuid(std::string_view stable_id) {
  // This mirrors BookmarkCodec's initial UUID set plus the one value it
  // rewrites explicitly. kShoppingCollectionUuid is deliberately absent: the
  // pinned Chromium model treats it as an ordinary child identity.
  const std::array reserved = {bookmarks::kRootNodeUuid,
                               bookmarks::kBookmarkBarNodeUuid,
                               bookmarks::kOtherBookmarksNodeUuid,
                               bookmarks::kMobileBookmarksNodeUuid,
                               bookmarks::kManagedNodeUuid,
                               bookmarks::kBannedUuidDueToPastSyncBug};
  for (const char* value : reserved) {
    if (stable_id == value) {
      return true;
    }
  }
  return false;
}

bool PositionsAreDense(
    base::span<const size_t> siblings,
    base::span<const storage::backup::BackupBookmarkRecord> records) {
  std::vector<bool> claimed(siblings.size());
  for (size_t record_index : siblings) {
    const uint32_t position = records[record_index].position;
    if (position >= claimed.size() || claimed[position]) {
      return false;
    }
    claimed[position] = true;
  }
  return true;
}

struct PendingNode {
  size_t record_index;
  size_t depth;
};

}  // namespace

bool ValidateBackupBookmarkForest(
    base::span<const storage::backup::BackupBookmarkRecord> records) {
  using storage::backup::EncodeBookmarkRecordV1;
  if (records.size() > kMaxNodes) {
    return false;
  }

  std::map<std::string_view, size_t> record_by_id;
  size_t encoded_bytes = 0u;
  for (size_t index = 0u; index < records.size(); ++index) {
    const auto& record = records[index];
    auto encoded = EncodeBookmarkRecordV1(record);
    if (!encoded) {
      return false;
    }
    const size_t record_bytes = encoded->size();
    crypto::SecureZeroBuffer(*encoded);
    if (IsReservedOrBannedUuid(record.stable_id) ||
        !record_by_id.emplace(record.stable_id, index).second ||
        record_bytes > kMaxBytes - encoded_bytes) {
      return false;
    }
    encoded_bytes += record_bytes;
  }

  std::array<std::vector<size_t>, 3u> root_children;
  std::vector<std::vector<size_t>> children(records.size());
  for (size_t index = 0u; index < records.size(); ++index) {
    const auto& record = records[index];
    if (const auto root = RootIndex(record.parent_id)) {
      root_children[*root].push_back(index);
      continue;
    }
    const auto parent = record_by_id.find(record.parent_id);
    if (parent == record_by_id.end() || records[parent->second].url) {
      return false;
    }
    children[parent->second].push_back(index);
  }

  for (const auto& siblings : root_children) {
    if (!PositionsAreDense(siblings, records)) {
      return false;
    }
  }
  for (const auto& siblings : children) {
    if (!PositionsAreDense(siblings, records)) {
      return false;
    }
  }

  std::vector<PendingNode> pending;
  for (const auto& siblings : root_children) {
    for (size_t record_index : siblings) {
      pending.push_back({record_index, 1u});
    }
  }
  std::vector<bool> visited(records.size());
  size_t visited_count = 0u;
  while (!pending.empty()) {
    const PendingNode current = pending.back();
    pending.pop_back();
    if (current.depth > kMaxDepth || visited[current.record_index]) {
      return false;
    }
    visited[current.record_index] = true;
    ++visited_count;
    for (size_t child : children[current.record_index]) {
      pending.push_back({child, current.depth + 1u});
    }
  }
  return visited_count == records.size();
}

}  // namespace taffy
