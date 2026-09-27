// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_browser_target_writers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/bookmarks/browser/bookmark_client.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "taffy/browser/backup_bookmark_forest_validation.h"
#include "taffy/browser/backup_browser_record_adapters.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace taffy {
namespace {

using storage::backup::BackupBookmarkRecord;
using storage::backup::BackupSnapshotError;

std::optional<BackupSnapshotError> ModelBoundaryError(
    const bookmarks::BookmarkModel* model,
    bool require_empty) {
  if (!model || !model->loaded() || !model->root_node() ||
      !model->bookmark_bar_node() || !model->other_node() ||
      !model->mobile_node() || !model->client()) {
    return BackupSnapshotError::kUnavailable;
  }
  if (model->client()->IsSyncFeatureEnabledIncludingBookmarks() ||
      model->account_bookmark_bar_node() || model->account_other_node() ||
      model->account_mobile_node()) {
    return BackupSnapshotError::kUnsupportedSelection;
  }

  const std::array<const bookmarks::BookmarkNode*, 3u> local_roots = {
      model->bookmark_bar_node(), model->other_node(), model->mobile_node()};
  if (model->root_node()->children().size() != local_roots.size()) {
    return BackupSnapshotError::kUnsupportedSelection;
  }
  for (const auto& child : model->root_node()->children()) {
    if (std::ranges::find(local_roots, child.get()) == local_roots.end()) {
      return BackupSnapshotError::kUnsupportedSelection;
    }
  }
  if (require_empty && std::ranges::any_of(local_roots, [](const auto* root) {
        return !root->children().empty();
      })) {
    return BackupSnapshotError::kUnsupportedSelection;
  }
  return std::nullopt;
}

BackupBookmarkTargetReadback DecodeBookmarkSnapshot(
    storage::backup::BackupSnapshotResult snapshot) {
  using core_service::mojom::BackupRecordKind;
  using storage::backup::DecodeBookmarkRecordV1;
  if (!snapshot) {
    return base::unexpected(snapshot.error());
  }
  std::vector<BackupBookmarkRecord> records;
  records.reserve(snapshot->size());
  for (const auto& item : *snapshot) {
    if (!item.descriptor ||
        item.descriptor->kind != BackupRecordKind::kBookmark) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
    auto decoded = DecodeBookmarkRecordV1(
        item.plaintext, item.descriptor->stable_id, item.descriptor->revision);
    if (!decoded) {
      return base::unexpected(BackupSnapshotError::kInvalidRecord);
    }
    records.push_back(std::move(*decoded));
  }
  return records;
}

const bookmarks::BookmarkNode* AddRecord(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* parent,
    const BackupBookmarkRecord& record) {
  const base::Time added = base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(static_cast<int64_t>(record.date_added_windows_us)));
  const base::Uuid uuid = base::Uuid::ParseLowercase(record.stable_id);
  const std::u16string title = base::UTF8ToUTF16(record.title);
  if (record.url) {
    return model->AddURL(parent, record.position, title, GURL(*record.url),
                         nullptr, added, uuid, false);
  }
  return model->AddFolder(parent, record.position, title, nullptr, added, uuid);
}

}  // namespace

ChromiumBackupBookmarkTargetWriter::ChromiumBackupBookmarkTargetWriter(
    bookmarks::BookmarkModel* model)
    : model_(model) {}

ChromiumBackupBookmarkTargetWriter::~ChromiumBackupBookmarkTargetWriter() =
    default;

BackupBookmarkTargetReadback ChromiumBackupBookmarkTargetWriter::ReadBack()
    const {
  if (auto error = ModelBoundaryError(model_, false)) {
    return base::unexpected(*error);
  }
  return DecodeBookmarkSnapshot(ReadBookmarkBackupRecords(model_));
}

BackupBrowserTargetWriteResult
ChromiumBackupBookmarkTargetWriter::WriteAndReadBack(
    base::span<const BackupBookmarkRecord> records) {
  if (ModelBoundaryError(model_, true) ||
      !ValidateBackupBookmarkForest(records)) {
    return BackupBrowserTargetWriteResult::kRefusedBeforeMutation;
  }

  std::vector<BackupBookmarkRecord> expected(records.begin(), records.end());
  std::ranges::sort(expected, {}, &BackupBookmarkRecord::stable_id);

  std::map<std::string, std::vector<const BackupBookmarkRecord*>> children;
  for (const auto& record : records) {
    children[record.parent_id].push_back(&record);
  }
  for (auto& entry : children) {
    std::ranges::sort(entry.second, {}, [](const BackupBookmarkRecord* record) {
      return record->position;
    });
  }

  using storage::backup::kBookmarkBarBackupRoot;
  using storage::backup::kMobileBookmarksBackupRoot;
  using storage::backup::kOtherBookmarksBackupRoot;
  std::map<std::string, const bookmarks::BookmarkNode*> nodes = {
      {kBookmarkBarBackupRoot, model_->bookmark_bar_node()},
      {kOtherBookmarksBackupRoot, model_->other_node()},
      {kMobileBookmarksBackupRoot, model_->mobile_node()}};
  std::vector<std::string> pending = {
      kBookmarkBarBackupRoot, kOtherBookmarksBackupRoot,
      kMobileBookmarksBackupRoot};
  std::vector<std::pair<const bookmarks::BookmarkNode*, uint64_t>> folders;

  bool mutation_attempted = false;
  bool projection_complete = true;
  model_->BeginExtensiveChanges();
  for (size_t next = 0u; next < pending.size() && projection_complete; ++next) {
    const auto parent = nodes.find(pending[next]);
    const auto child_group = children.find(pending[next]);
    if (parent == nodes.end()) {
      projection_complete = false;
      break;
    }
    if (child_group == children.end()) {
      continue;
    }
    for (const BackupBookmarkRecord* record : child_group->second) {
      mutation_attempted = true;
      const bookmarks::BookmarkNode* node =
          AddRecord(model_, parent->second, *record);
      if (!node || node->uuid().AsLowercaseString() != record->stable_id) {
        projection_complete = false;
        break;
      }
      if (!record->url) {
        if (!nodes.emplace(record->stable_id, node).second) {
          projection_complete = false;
          break;
        }
        pending.push_back(record->stable_id);
        folders.emplace_back(node, record->date_folder_modified_windows_us);
      }
    }
  }
  if (projection_complete) {
    for (const auto& [folder, modified_windows_us] : folders) {
      mutation_attempted = true;
      model_->SetDateFolderModified(
          folder, base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(
                      static_cast<int64_t>(modified_windows_us))));
    }
  }
  model_->EndExtensiveChanges();

  if (!projection_complete) {
    return mutation_attempted
               ? BackupBrowserTargetWriteResult::kOutcomeUnknown
               : BackupBrowserTargetWriteResult::kRefusedBeforeMutation;
  }
  auto observed = ReadBack();
  if (!observed || *observed != expected) {
    return mutation_attempted
               ? BackupBrowserTargetWriteResult::kOutcomeUnknown
               : BackupBrowserTargetWriteResult::kRefusedBeforeMutation;
  }
  return BackupBrowserTargetWriteResult::kAppliedAndReadBack;
}

}  // namespace taffy
