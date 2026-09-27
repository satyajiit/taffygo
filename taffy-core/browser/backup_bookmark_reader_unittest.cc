// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/strings/string_view_util.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "crypto/hash.h"
#include "taffy/browser/backup_browser_record_adapters.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

using namespace storage::backup;

class BackupBookmarkReaderTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<bookmarks::BookmarkModel> model_ =
      bookmarks::TestBookmarkClient::CreateModel();
};

TEST_F(BackupBookmarkReaderTest, PreservesFolderHierarchyOrderAndExactTimes) {
  const auto* folder = model_->AddFolder(model_->mobile_node(), 0, u"Research");
  const auto* first =
      model_->AddURL(folder, 0, u"First", GURL("https://a.test/"));
  const auto* second =
      model_->AddURL(folder, 1, u"Second", GURL("https://b.test/"));
  const auto* empty = model_->AddFolder(model_->other_node(), 0, u"Empty");
  model_->SetDateAdded(
      first, base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(123)));
  model_->SetDateFolderModified(
      folder, base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(456)));
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 4u);
  for (const auto& record : *snapshot) {
    ASSERT_TRUE(record.descriptor);
    EXPECT_EQ(record.descriptor->kind,
              core_service::mojom::BackupRecordKind::kBookmark);
    EXPECT_EQ(record.descriptor->plaintext_bytes, record.plaintext.size());
    EXPECT_TRUE(std::ranges::equal(record.descriptor->plaintext_sha256,
                                   crypto::hash::Sha256(record.plaintext)));
    auto decoded =
        DecodeBookmarkRecordV1(record.plaintext, record.descriptor->stable_id,
                               record.descriptor->revision);
    ASSERT_TRUE(decoded.has_value());
    if (decoded->stable_id == folder->uuid().AsLowercaseString()) {
      EXPECT_EQ(decoded->parent_id, kMobileBookmarksBackupRoot);
      EXPECT_EQ(decoded->position, 0u);
      EXPECT_FALSE(decoded->url.has_value());
      EXPECT_EQ(decoded->date_folder_modified_windows_us, 456u);
    } else if (decoded->stable_id == empty->uuid().AsLowercaseString()) {
      EXPECT_EQ(decoded->parent_id, kOtherBookmarksBackupRoot);
      EXPECT_EQ(decoded->title, "Empty");
      EXPECT_FALSE(decoded->url.has_value());
    } else {
      EXPECT_EQ(decoded->parent_id, folder->uuid().AsLowercaseString());
      if (decoded->stable_id == first->uuid().AsLowercaseString()) {
        EXPECT_EQ(decoded->position, 0u);
        EXPECT_EQ(decoded->url, "https://a.test/");
        EXPECT_EQ(decoded->date_added_windows_us, 123u);
      } else {
        EXPECT_EQ(decoded->stable_id, second->uuid().AsLowercaseString());
        EXPECT_EQ(decoded->position, 1u);
        EXPECT_EQ(decoded->url, "https://b.test/");
      }
      EXPECT_EQ(decoded->date_folder_modified_windows_us, 0u);
    }
  }
  EXPECT_TRUE(std::ranges::is_sorted(*snapshot, {},
                                     [](const BackupSnapshotRecord& record) {
                                       return record.descriptor->stable_id;
                                     }));
}

TEST_F(BackupBookmarkReaderTest, ArbitraryMetadataNeverEntersPayload) {
  const auto* node = model_->AddURL(model_->bookmark_bar_node(), 0, u"Saved",
                                    GURL("https://example.test/"));
  auto before = ReadBookmarkBackupRecords(model_.get());
  ASSERT_TRUE(before.has_value());
  model_->SetNodeMetaInfo(node, "unknown-field", "private-metadata-value");
  auto after = ReadBookmarkBackupRecords(model_.get());
  ASSERT_TRUE(after.has_value());
  ASSERT_EQ(after->size(), 1u);
  EXPECT_EQ(before->front().plaintext, after->front().plaintext);
  EXPECT_EQ(
      base::as_string_view(after->front().plaintext).find("private-metadata"),
      std::string_view::npos);
}

TEST_F(BackupBookmarkReaderTest,
       UnsupportedBookmarkRefusesRatherThanDroppingOne) {
  model_->AddURL(model_->mobile_node(), 0, u"Valid",
                 GURL("https://example.test/"));
  model_->AddURL(model_->mobile_node(), 1, u"Script",
                 GURL("javascript:alert(1)"));
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kInvalidRecord);
  EXPECT_EQ(model_->mobile_node()->children().size(), 2u);
}

TEST_F(BackupBookmarkReaderTest, AccountOwnedContentRefusesWholeSelection) {
  model_->CreateAccountPermanentFolders();
  model_->AddURL(model_->account_mobile_node(), 0, u"Account",
                 GURL("https://example.test/"));
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kUnsupportedSelection);
}

TEST_F(BackupBookmarkReaderTest, DepthLimitIsBoundedWithoutRecursiveTraversal) {
  const bookmarks::BookmarkNode* parent = model_->mobile_node();
  for (size_t depth = 0; depth < 128; ++depth) {
    parent = model_->AddFolder(parent, 0, u"Folder");
  }
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->size(), 128u);
  model_->AddFolder(parent, 0, u"Too deep");
  snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kCapacityExceeded);
}

TEST_F(BackupBookmarkReaderTest, RecordLimitRefusesCompleteSelection) {
  for (size_t index = 0; index < 10001; ++index) {
    model_->AddFolder(model_->mobile_node(), index, u"Folder");
  }
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kCapacityExceeded);
}

TEST_F(BackupBookmarkReaderTest, EmptyLocalModelIsAnEmptySelection) {
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_TRUE(snapshot->empty());
}

TEST_F(BackupBookmarkReaderTest,
       LocalRootsUnderChromiumSyncAreNotLocalOnlyData) {
  auto client = std::make_unique<bookmarks::TestBookmarkClient>();
  client->SetIsSyncFeatureEnabledIncludingBookmarks(true);
  auto model =
      bookmarks::TestBookmarkClient::CreateModelWithClient(std::move(client));
  model->AddURL(model->mobile_node(), 0, u"Synced",
                GURL("https://example.test/"));
  auto snapshot = ReadBookmarkBackupRecords(model.get());
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kUnsupportedSelection);
}

TEST_F(BackupBookmarkReaderTest, ManagedRootIsNotCopiedAsPersonOwnedData) {
  auto client = std::make_unique<bookmarks::TestBookmarkClient>();
  const auto* managed = client->EnableManagedNode();
  auto model =
      bookmarks::TestBookmarkClient::CreateModelWithClient(std::move(client));
  const auto* enterprise = model->AddURL(managed, 0, u"Managed policy record",
                                         GURL("https://managed.example.test/"));
  model->SetNodeMetaInfo(enterprise, "policy", "managed-policy-value");
  const auto* local = model->AddURL(model->mobile_node(), 0, u"Local",
                                    GURL("https://local.example.test/"));
  auto snapshot = ReadBookmarkBackupRecords(model.get());
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  EXPECT_EQ(snapshot->front().descriptor->stable_id,
            local->uuid().AsLowercaseString());
  EXPECT_EQ(base::as_string_view(snapshot->front().plaintext).find("managed"),
            std::string_view::npos);
}

TEST_F(BackupBookmarkReaderTest,
       AggregateByteLimitRefusesWithoutReturningPartialRecords) {
  const GURL url("https://example.test/" + std::string(60000, 'a'));
  ASSERT_TRUE(url.is_valid());
  for (size_t index = 0; index < 600; ++index) {
    model_->AddURL(model_->mobile_node(), index, u"Saved", url);
  }
  auto snapshot = ReadBookmarkBackupRecords(model_.get());
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kCapacityExceeded);
}

TEST_F(BackupBookmarkReaderTest, UnavailableModelDoesNotMasqueradeAsEmpty) {
  EXPECT_FALSE(ReadBookmarkBackupRecords(nullptr));
  bookmarks::BookmarkModel unloaded(
      std::make_unique<bookmarks::TestBookmarkClient>());
  auto snapshot = ReadBookmarkBackupRecords(&unloaded);
  ASSERT_FALSE(snapshot.has_value());
  EXPECT_EQ(snapshot.error(), BackupSnapshotError::kUnavailable);
}

}  // namespace
}  // namespace taffy
