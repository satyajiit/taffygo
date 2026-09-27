// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "taffy/browser/backup_browser_target_writers.h"
#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using storage::backup::BackupBookmarkRecord;
using storage::backup::kBookmarkBarBackupRoot;
using storage::backup::kMobileBookmarksBackupRoot;

constexpr char kFolderId[] = "31c22f59-36f3-4d99-90d1-0d54a8f10101";
constexpr char kNestedFolderId[] = "52c74750-5c5c-44e7-8b41-fb589a002202";
constexpr char kFirstUrlId[] = "659b7ee8-450d-47f1-8b18-9ed47f303303";
constexpr char kSecondUrlId[] = "fb6b796c-0385-45e8-86df-262cdb404404";

const bookmarks::BookmarkNode* Find(bookmarks::BookmarkModel* model,
                                    const char* id) {
  return model->GetNodeByUuid(
      base::Uuid::ParseLowercase(id),
      bookmarks::BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes);
}

class BackupBookmarkTargetWriterTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<bookmarks::BookmarkModel> model_ =
      bookmarks::TestBookmarkClient::CreateModel();
};

TEST_F(BackupBookmarkTargetWriterTest,
       PreservesUuidParentsOrderTitlesUrlsAndTimestamps) {
  std::vector<BackupBookmarkRecord> records = {
      {.stable_id = kSecondUrlId,
       .parent_id = kFolderId,
       .position = 1,
       .title = "Second",
       .url = "https://second.example.test/path",
       .date_added_windows_us = 404},
      {.stable_id = kNestedFolderId,
       .parent_id = kFolderId,
       .position = 0,
       .title = "Nested",
       .date_added_windows_us = 202,
       .date_folder_modified_windows_us = 707},
      {.stable_id = kFolderId,
       .parent_id = kMobileBookmarksBackupRoot,
       .position = 0,
       .title = "Research",
       .date_added_windows_us = 101,
       .date_folder_modified_windows_us = 808},
      {.stable_id = kFirstUrlId,
       .parent_id = kBookmarkBarBackupRoot,
       .position = 0,
       .title = "First ✓",
       .url = "https://first.example.test/",
       .date_added_windows_us = 303},
  };
  ChromiumBackupBookmarkTargetWriter chromium_writer(model_.get());
  BackupBookmarkTargetWriter& writer = chromium_writer;

  EXPECT_EQ(writer.WriteAndReadBack(records),
            BackupBrowserTargetWriteResult::kAppliedAndReadBack);
  auto expected = records;
  std::ranges::sort(expected, {}, &BackupBookmarkRecord::stable_id);
  auto readback = writer.ReadBack();
  ASSERT_TRUE(readback.has_value());
  EXPECT_EQ(*readback, expected);

  const auto* folder = Find(model_.get(), kFolderId);
  const auto* nested = Find(model_.get(), kNestedFolderId);
  ASSERT_TRUE(folder);
  ASSERT_TRUE(nested);
  ASSERT_EQ(folder->children().size(), 2u);
  EXPECT_EQ(folder->children()[0].get(), nested);
  EXPECT_EQ(folder->children()[1].get(), Find(model_.get(), kSecondUrlId));
  EXPECT_EQ(folder->date_added(),
            base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(101)));
  EXPECT_EQ(folder->date_folder_modified(),
            base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(808)));
  EXPECT_EQ(nested->date_folder_modified(),
            base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(707)));
}

TEST_F(BackupBookmarkTargetWriterTest,
       InvalidForestOrUrlIsRefusedBeforeFirstMutation) {
  const BackupBookmarkRecord local_url{.stable_id = kFirstUrlId,
                                       .parent_id = kMobileBookmarksBackupRoot,
                                       .title = "Local file",
                                       .url = "file:///data/private.txt"};
  EXPECT_EQ(ChromiumBackupBookmarkTargetWriter(model_.get())
                .WriteAndReadBack(base::span_from_ref(local_url)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);

  const BackupBookmarkRecord missing_parent{
      .stable_id = kFolderId, .parent_id = kNestedFolderId, .title = "Orphan"};
  EXPECT_EQ(ChromiumBackupBookmarkTargetWriter(model_.get())
                .WriteAndReadBack(base::span_from_ref(missing_parent)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_TRUE(model_->mobile_node()->children().empty());
}

TEST_F(BackupBookmarkTargetWriterTest,
       EmptySelectedForestReadsBackAsAnExplicitEmptyProjection) {
  ChromiumBackupBookmarkTargetWriter writer(model_.get());
  EXPECT_EQ(writer.WriteAndReadBack({}),
            BackupBrowserTargetWriteResult::kAppliedAndReadBack);
  auto readback = writer.ReadBack();
  ASSERT_TRUE(readback.has_value());
  EXPECT_TRUE(readback->empty());
  EXPECT_TRUE(model_->bookmark_bar_node()->children().empty());
  EXPECT_TRUE(model_->other_node()->children().empty());
  EXPECT_TRUE(model_->mobile_node()->children().empty());
}

TEST_F(BackupBookmarkTargetWriterTest,
       AnAppliedNonemptyProjectionCannotBeRepeatedAsAnotherWrite) {
  const BackupBookmarkRecord incoming{.stable_id = kFolderId,
                                      .parent_id = kMobileBookmarksBackupRoot,
                                      .title = "Restored"};
  ChromiumBackupBookmarkTargetWriter writer(model_.get());
  EXPECT_EQ(writer.WriteAndReadBack(base::span_from_ref(incoming)),
            BackupBrowserTargetWriteResult::kAppliedAndReadBack);
  const auto* original = Find(model_.get(), kFolderId);
  ASSERT_TRUE(original);
  EXPECT_EQ(writer.WriteAndReadBack(base::span_from_ref(incoming)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  ASSERT_EQ(model_->mobile_node()->children().size(), 1u);
  EXPECT_EQ(model_->mobile_node()->children().front().get(), original);
}

TEST_F(BackupBookmarkTargetWriterTest,
       ExistingLocalContentRefusesWithoutReplacingOrAppending) {
  const auto* existing = model_->AddFolder(model_->mobile_node(), 0, u"Keep");
  const BackupBookmarkRecord incoming{.stable_id = kFolderId,
                                      .parent_id = kMobileBookmarksBackupRoot,
                                      .title = "Incoming"};
  ChromiumBackupBookmarkTargetWriter writer(model_.get());
  EXPECT_EQ(writer.WriteAndReadBack(base::span_from_ref(incoming)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  ASSERT_EQ(model_->mobile_node()->children().size(), 1u);
  EXPECT_EQ(model_->mobile_node()->children()[0].get(), existing);
  EXPECT_FALSE(Find(model_.get(), kFolderId));
}

TEST(BackupBookmarkTargetWriterBoundaryTest,
     SyncAccountAndManagedRootsAreNeverRestoreTargets) {
  base::test::TaskEnvironment task_environment;
  const BackupBookmarkRecord incoming{.stable_id = kFolderId,
                                      .parent_id = kMobileBookmarksBackupRoot,
                                      .title = "Incoming"};

  auto sync_client = std::make_unique<bookmarks::TestBookmarkClient>();
  sync_client->SetIsSyncFeatureEnabledIncludingBookmarks(true);
  auto sync_model = bookmarks::TestBookmarkClient::CreateModelWithClient(
      std::move(sync_client));
  EXPECT_EQ(ChromiumBackupBookmarkTargetWriter(sync_model.get())
                .WriteAndReadBack(base::span_from_ref(incoming)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);

  auto account_model = bookmarks::TestBookmarkClient::CreateModel();
  account_model->CreateAccountPermanentFolders();
  EXPECT_EQ(ChromiumBackupBookmarkTargetWriter(account_model.get())
                .WriteAndReadBack(base::span_from_ref(incoming)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);

  auto managed_client = std::make_unique<bookmarks::TestBookmarkClient>();
  managed_client->EnableManagedNode();
  auto managed_model = bookmarks::TestBookmarkClient::CreateModelWithClient(
      std::move(managed_client));
  EXPECT_EQ(ChromiumBackupBookmarkTargetWriter(managed_model.get())
                .WriteAndReadBack(base::span_from_ref(incoming)),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
}

TEST(BackupBookmarkTargetWriterBoundaryTest, NullOrUnloadedOwnerIsRefused) {
  base::test::TaskEnvironment task_environment;
  ChromiumBackupBookmarkTargetWriter absent(nullptr);
  EXPECT_EQ(absent.WriteAndReadBack({}),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_FALSE(absent.ReadBack());

  bookmarks::BookmarkModel unloaded(
      std::make_unique<bookmarks::TestBookmarkClient>());
  ChromiumBackupBookmarkTargetWriter unavailable(&unloaded);
  EXPECT_EQ(unavailable.WriteAndReadBack({}),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_FALSE(unavailable.ReadBack());
}

}  // namespace
}  // namespace taffy
