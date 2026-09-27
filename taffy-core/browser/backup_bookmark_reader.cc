// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "taffy/browser/backup_browser_record_adapters.h"
#include "taffy/browser/backup_browser_record_adapters_internal.h"

namespace taffy {
namespace {

constexpr size_t kMaxNodes = 10000;
constexpr size_t kMaxDepth = 128;
constexpr size_t kMaxBytes = 32u * 1024u * 1024u;

// Local stack entries never escape this synchronous model-sequence read.
struct PendingBookmark {
  raw_ptr<const bookmarks::BookmarkNode> node;
  std::string parent_id;
  uint32_t position;
  size_t depth;
};

bool AppendChildren(const bookmarks::BookmarkNode& parent,
                    const std::string& parent_id,
                    size_t depth,
                    size_t visited,
                    std::vector<PendingBookmark>* pending) {
  if (parent.children().empty()) {
    return true;
  }
  if (depth > kMaxDepth || visited + pending->size() > kMaxNodes ||
      parent.children().size() > kMaxNodes - visited - pending->size()) {
    return false;
  }
  for (size_t index = parent.children().size(); index > 0; --index) {
    pending->push_back({parent.children()[index - 1].get(), parent_id,
                        static_cast<uint32_t>(index - 1), depth});
  }
  return true;
}

}  // namespace

storage::backup::BackupSnapshotResult ReadBookmarkBackupRecords(
    const bookmarks::BookmarkModel* model) {
  using namespace storage::backup;
  if (!model || !model->loaded()) {
    return base::unexpected(BackupSnapshotError::kUnavailable);
  }
  for (const auto* account_root :
       {model->account_bookmark_bar_node(), model->account_other_node(),
        model->account_mobile_node()}) {
    if (account_root && !account_root->children().empty()) {
      return base::unexpected(BackupSnapshotError::kUnsupportedSelection);
    }
  }
  std::vector<PendingBookmark> pending;
  const std::pair<const bookmarks::BookmarkNode*, const char*> roots[] = {
      {model->bookmark_bar_node(), kBookmarkBarBackupRoot},
      {model->other_node(), kOtherBookmarksBackupRoot},
      {model->mobile_node(), kMobileBookmarksBackupRoot}};
  for (const auto& [root, id] : roots) {
    if (!root) {
      return base::unexpected(BackupSnapshotError::kUnavailable);
    }
    if (!AppendChildren(*root, id, 1, 0, &pending)) {
      return base::unexpected(BackupSnapshotError::kCapacityExceeded);
    }
  }
  std::vector<BackupSnapshotRecord> result;
  std::set<std::string> identities;
  size_t bytes = 0;
  while (!pending.empty()) {
    auto entry = std::move(pending.back());
    pending.pop_back();
    const auto& node = *entry.node;
    if (!model->IsLocalOnlyNode(node)) {
      return base::unexpected(BackupSnapshotError::kUnsupportedSelection);
    }
    if (node.is_permanent_node() ||
        !identities.insert(node.uuid().AsLowercaseString()).second ||
        (node.is_url() && !node.children().empty())) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
    const int64_t added =
        node.date_added().ToDeltaSinceWindowsEpoch().InMicroseconds();
    const int64_t modified = node.is_url() ? 0
                                           : node.date_folder_modified()
                                                 .ToDeltaSinceWindowsEpoch()
                                                 .InMicroseconds();
    if (added < 0 || modified < 0) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
    BackupBookmarkRecord values;
    values.stable_id = node.uuid().AsLowercaseString();
    values.parent_id = std::move(entry.parent_id);
    values.position = entry.position;
    if (node.GetTitle().size() > 4096 ||
        !base::UTF16ToUTF8(node.GetTitle().data(), node.GetTitle().size(),
                           &values.title)) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
    if (node.is_url()) {
      values.url = node.url().spec();
    }
    values.date_added_windows_us = static_cast<uint64_t>(added);
    values.date_folder_modified_windows_us = static_cast<uint64_t>(modified);
    auto record = backup_browser_internal::MakeSnapshotRecord(
        core_service::mojom::BackupRecordKind::kBookmark, values.stable_id,
        EncodeBookmarkRecordV1(values));
    if (!record) {
      return base::unexpected(record.error());
    }
    if (record->plaintext.size() > kMaxBytes - bytes) {
      return base::unexpected(BackupSnapshotError::kCapacityExceeded);
    }
    bytes += record->plaintext.size();
    result.push_back(std::move(*record));
    if (!AppendChildren(node, values.stable_id, entry.depth + 1, result.size(),
                        &pending)) {
      return base::unexpected(BackupSnapshotError::kCapacityExceeded);
    }
  }
  std::ranges::sort(result, {}, [](const BackupSnapshotRecord& record) {
    return record.descriptor->stable_id;
  });
  return result;
}

}  // namespace taffy
