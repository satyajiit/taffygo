// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_bookmark_forest_validation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "components/bookmarks/browser/bookmark_uuids.h"
#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using storage::backup::BackupBookmarkRecord;
using storage::backup::kBookmarkBarBackupRoot;
using storage::backup::kMobileBookmarksBackupRoot;
using storage::backup::kOtherBookmarksBackupRoot;

std::string Id(uint32_t value) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string id = "00000000-0000-4000-8000-000000000000";
  uint32_t remaining = value + 1u;
  for (size_t offset = 0u; offset < 8u; ++offset) {
    id[id.size() - 1u - offset] = kHex[remaining & 0x0fu];
    remaining >>= 4u;
  }
  return id;
}

BackupBookmarkRecord Folder(std::string id,
                            std::string parent,
                            uint32_t position) {
  return {std::move(id),
          std::move(parent),
          position,
          "Folder",
          std::nullopt,
          1u,
          2u};
}

BackupBookmarkRecord Url(std::string id,
                         std::string parent,
                         uint32_t position,
                         std::string url = "https://example.test/") {
  return {std::move(id),
          std::move(parent),
          position,
          "Saved",
          std::move(url),
          1u,
          0u};
}

TEST(BackupBookmarkForestValidationTest,
     EmptyAndCompleteThreeRootForestsAreValidInAnyInputOrder) {
  EXPECT_TRUE(ValidateBackupBookmarkForest({}));
  std::vector records = {
      Url(Id(3), Id(0), 1u),
      Folder(Id(0), kBookmarkBarBackupRoot, 0u),
      Url(Id(2), kOtherBookmarksBackupRoot, 0u),
      Url(Id(1), Id(0), 0u),
      Folder(Id(4), kMobileBookmarksBackupRoot, 0u),
  };
  EXPECT_TRUE(ValidateBackupBookmarkForest(records));
  std::ranges::reverse(records);
  EXPECT_TRUE(ValidateBackupBookmarkForest(records));
}

TEST(BackupBookmarkForestValidationTest,
     ChromiumPermanentAndBannedIdentitiesAreRefused) {
  const std::array refused = {bookmarks::kRootNodeUuid,
                              bookmarks::kBookmarkBarNodeUuid,
                              bookmarks::kOtherBookmarksNodeUuid,
                              bookmarks::kMobileBookmarksNodeUuid,
                              bookmarks::kManagedNodeUuid,
                              bookmarks::kBannedUuidDueToPastSyncBug};
  for (const char* id : refused) {
    EXPECT_FALSE(ValidateBackupBookmarkForest(
        std::array{Folder(id, kBookmarkBarBackupRoot, 0u)}));
  }
  EXPECT_TRUE(ValidateBackupBookmarkForest(std::array{
      Folder(bookmarks::kShoppingCollectionUuid, kBookmarkBarBackupRoot, 0u)}));
}

TEST(BackupBookmarkForestValidationTest,
     IdentityMustBeCanonicalUniqueAndUseAnExactSupportedRoot) {
  EXPECT_FALSE(ValidateBackupBookmarkForest(
      std::array{Folder("NOT-A-UUID", kBookmarkBarBackupRoot, 0u)}));
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Folder(Id(0), kBookmarkBarBackupRoot, 0u),
      Url(Id(0), kOtherBookmarksBackupRoot, 0u),
  }));
  EXPECT_FALSE(ValidateBackupBookmarkForest(
      std::array{Folder(Id(0), "account-bookmarks", 0u)}));
}

TEST(BackupBookmarkForestValidationTest,
     OrphanSelfParentAndMultiNodeCyclesAreRefused) {
  EXPECT_FALSE(
      ValidateBackupBookmarkForest(std::array{Folder(Id(0), Id(1), 0u)}));
  EXPECT_FALSE(
      ValidateBackupBookmarkForest(std::array{Folder(Id(0), Id(0), 0u)}));
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Folder(Id(0), Id(1), 0u),
      Folder(Id(1), Id(0), 0u),
  }));
}

TEST(BackupBookmarkForestValidationTest, UrlRecordsCanNeverBeParents) {
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Url(Id(0), kBookmarkBarBackupRoot, 0u),
      Url(Id(1), Id(0), 0u),
  }));
}

TEST(BackupBookmarkForestValidationTest,
     EverySiblingGroupHasUniqueDensePositions) {
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Url(Id(0), kBookmarkBarBackupRoot, 0u),
      Url(Id(1), kBookmarkBarBackupRoot, 0u),
  }));
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Url(Id(0), kBookmarkBarBackupRoot, 1u),
  }));
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Folder(Id(0), kBookmarkBarBackupRoot, 0u),
      Url(Id(1), Id(0), 1u),
  }));
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{
      Folder(Id(0), kBookmarkBarBackupRoot, 0u),
      Url(Id(1), Id(0), 0u),
      Url(Id(2), Id(0), 0u),
  }));
  EXPECT_TRUE(ValidateBackupBookmarkForest(std::array{
      Folder(Id(0), kBookmarkBarBackupRoot, 0u),
      Folder(Id(1), kOtherBookmarksBackupRoot, 0u),
      Url(Id(2), Id(0), 0u),
      Url(Id(3), Id(1), 0u),
  }));
}

TEST(BackupBookmarkForestValidationTest, Depth128IsExact) {
  std::vector<BackupBookmarkRecord> records;
  std::string parent = kMobileBookmarksBackupRoot;
  for (uint32_t depth = 0u; depth < 128u; ++depth) {
    records.push_back(Folder(Id(depth), std::move(parent), 0u));
    parent = records.back().stable_id;
  }
  EXPECT_TRUE(ValidateBackupBookmarkForest(records));
  records.push_back(Folder(Id(128u), std::move(parent), 0u));
  EXPECT_FALSE(ValidateBackupBookmarkForest(records));
}

TEST(BackupBookmarkForestValidationTest, NodeCount10000IsExact) {
  std::vector<BackupBookmarkRecord> records;
  records.reserve(10'001u);
  for (uint32_t index = 0u; index < 10'000u; ++index) {
    records.push_back(Url(Id(index), kBookmarkBarBackupRoot, index));
  }
  EXPECT_TRUE(ValidateBackupBookmarkForest(records));
  records.push_back(Url(Id(10'000u), kBookmarkBarBackupRoot, 10'000u));
  EXPECT_FALSE(ValidateBackupBookmarkForest(records));
}

TEST(BackupBookmarkForestValidationTest, AggregateEncodedBytesAreBounded) {
  constexpr size_t kMaxBytes = 32u * 1024u * 1024u;
  constexpr std::string_view kUrlPrefix = "https://example.test/";
  constexpr size_t kLongSuffixBytes = 60'000u;
  std::vector<BackupBookmarkRecord> records;
  size_t bytes = 0u;
  uint32_t index = 0u;
  while (true) {
    auto shortest = Url(Id(index), kOtherBookmarksBackupRoot, index,
                        std::string(kUrlPrefix));
    auto shortest_encoded = storage::backup::EncodeBookmarkRecordV1(shortest);
    ASSERT_TRUE(shortest_encoded.has_value());
    const size_t remaining = kMaxBytes - bytes;
    if (remaining >= shortest_encoded->size() &&
        remaining - shortest_encoded->size() <= kLongSuffixBytes) {
      shortest.url->append(remaining - shortest_encoded->size(), 'a');
      auto exact = storage::backup::EncodeBookmarkRecordV1(shortest);
      ASSERT_TRUE(exact.has_value());
      ASSERT_EQ(exact->size(), remaining);
      records.push_back(std::move(shortest));
      bytes += exact->size();
      break;
    }
    auto long_record =
        Url(Id(index), kOtherBookmarksBackupRoot, index,
            std::string(kUrlPrefix) + std::string(kLongSuffixBytes, 'a'));
    auto encoded = storage::backup::EncodeBookmarkRecordV1(long_record);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_LT(encoded->size(), remaining);
    bytes += encoded->size();
    records.push_back(std::move(long_record));
    ++index;
  }
  ASSERT_EQ(bytes, kMaxBytes);
  EXPECT_TRUE(ValidateBackupBookmarkForest(records));
  ++index;
  records.push_back(Url(Id(index), kOtherBookmarksBackupRoot, index));
  EXPECT_FALSE(ValidateBackupBookmarkForest(records));
}

TEST(BackupBookmarkForestValidationTest,
     SingleRecordCodecRulesRemainPartOfForestAdmission) {
  auto credential =
      Url(Id(0), kBookmarkBarBackupRoot, 0u, "https://person@example.test/");
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{credential}));
  auto modified_url = Url(Id(1), kBookmarkBarBackupRoot, 0u);
  modified_url.date_folder_modified_windows_us = 1u;
  EXPECT_FALSE(ValidateBackupBookmarkForest(std::array{modified_url}));
}

}  // namespace
}  // namespace taffy
