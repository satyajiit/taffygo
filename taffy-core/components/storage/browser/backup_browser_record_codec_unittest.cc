// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_browser_record_codec.h"

#include <array>
#include <limits>
#include <string>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

constexpr char kBookmarkId[] = "11111111-1111-4111-8111-111111111111";
constexpr char kFolderId[] = "22222222-2222-4222-8222-222222222222";

BackupBookmarkRecord Bookmark() {
  return {
      kBookmarkId, kFolderId, 3, "Saved page", "https://example.test/path?q=1",
      123456,      0};
}

BackupBrowserPreferenceRecord Preferences() {
  return {"DARK", "HINDI", "IN", true};
}

TEST(BackupBrowserRecordCodecTest,
     CompleteVersionOneKnownAnswerBytesAreFrozen) {
  // Independent fixed vectors pin field order, framing, widths and byte order.
  // Writer/reader round trips alone would agree on an accidental format change.
  auto bookmark = EncodeBookmarkRecordV1(Bookmark());
  auto folder = EncodeBookmarkRecordV1(
      {kFolderId, kMobileBookmarksBackupRoot, 0, "", std::nullopt, 0, 789});
  auto preferences = EncodeBrowserPreferenceRecordV1(Preferences());
  ASSERT_TRUE(bookmark.has_value());
  ASSERT_TRUE(folder.has_value());
  ASSERT_TRUE(preferences.has_value());
  EXPECT_EQ(base::HexEncodeLower(*bookmark),
            "544146465952454301000000060000002400000031313131313131312d313131"
            "312d343131312d383131312d3131313131313131313131310100000000000000"
            "2400000032323232323232322d323232322d343232322d383232322d32323232"
            "3232323232323232030000000a00000053617665642070616765011d00000068"
            "747470733a2f2f6578616d706c652e746573742f706174683f713d3140e20100"
            "000000000000000000000000");
  EXPECT_EQ(base::HexEncodeLower(*folder),
            "544146465952454301000000060000002400000032323232323232322d323232"
            "322d343232322d383232322d3232323232323232323232320100000000000000"
            "100000006d6f62696c652d626f6f6b6d61726b73000000000000000000000000"
            "00000000001503000000000000");
  EXPECT_EQ(base::HexEncodeLower(*preferences),
            "544146465952454301000000070000001400000062726f777365722d70726573"
            "656e746174696f6e0100000000000000040000004441524b0500000048494e44"
            "4902000000494e01");
}

TEST(BackupBrowserRecordCodecTest, BookmarkPreservesEveryField) {
  auto value = Bookmark();
  auto encoded = EncodeBookmarkRecordV1(value);
  ASSERT_TRUE(encoded.has_value());
  auto decoded = DecodeBookmarkRecordV1(*encoded, kBookmarkId, 1);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, value);
}

TEST(BackupBrowserRecordCodecTest, EmptyFolderAndAllLocalRootsRoundTrip) {
  for (const auto* root : {kBookmarkBarBackupRoot, kOtherBookmarksBackupRoot,
                           kMobileBookmarksBackupRoot}) {
    BackupBookmarkRecord value{kFolderId, root, 0, "", std::nullopt, 0, 789};
    auto encoded = EncodeBookmarkRecordV1(value);
    ASSERT_TRUE(encoded.has_value());
    auto decoded = DecodeBookmarkRecordV1(*encoded, kFolderId, 1);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, value);
  }
}

TEST(BackupBrowserRecordCodecTest, FrozenRecordTagsAreNotMojoSerialization) {
  auto bookmark = EncodeBookmarkRecordV1(Bookmark());
  auto preferences = EncodeBrowserPreferenceRecordV1(Preferences());
  ASSERT_TRUE(bookmark.has_value());
  ASSERT_TRUE(preferences.has_value());
  const std::array<uint8_t, 16> prefix = {
      'T', 'A', 'F', 'F', 'Y', 'R', 'E', 'C', 1, 0, 0, 0, 6, 0, 0, 0};
  EXPECT_EQ(base::span(*bookmark).first<16>(), base::span(prefix));
  auto preference_prefix = prefix;
  preference_prefix[12] = 7;
  EXPECT_EQ(base::span(*preferences).first<16>(),
            base::span(preference_prefix));
  EXPECT_FALSE(DecodeBookmarkRecordV1(*preferences, kBookmarkId, 1));
  EXPECT_FALSE(DecodeBrowserPreferenceRecordV1(
      *bookmark, kBrowserPreferenceBackupStableId, 1));
}

TEST(BackupBrowserRecordCodecTest, DescriptorIdentityAndRevisionBindBothKinds) {
  auto bookmark = EncodeBookmarkRecordV1(Bookmark());
  auto preferences = EncodeBrowserPreferenceRecordV1(Preferences());
  ASSERT_TRUE(bookmark.has_value());
  ASSERT_TRUE(preferences.has_value());
  EXPECT_FALSE(DecodeBookmarkRecordV1(*bookmark, kFolderId, 1));
  EXPECT_FALSE(DecodeBookmarkRecordV1(*bookmark, kBookmarkId, 0));
  EXPECT_FALSE(DecodeBookmarkRecordV1(*bookmark, kBookmarkId, 2));
  EXPECT_FALSE(DecodeBrowserPreferenceRecordV1(*preferences, "theme", 1));
  EXPECT_FALSE(DecodeBrowserPreferenceRecordV1(
      *preferences, kBrowserPreferenceBackupStableId, 2));
}

TEST(BackupBrowserRecordCodecTest, EveryTruncationAndTrailingDataRefused) {
  auto bookmark = EncodeBookmarkRecordV1(Bookmark());
  auto preferences = EncodeBrowserPreferenceRecordV1(Preferences());
  ASSERT_TRUE(bookmark.has_value());
  ASSERT_TRUE(preferences.has_value());
  for (size_t size = 0; size < bookmark->size(); ++size) {
    EXPECT_FALSE(DecodeBookmarkRecordV1(base::span(*bookmark).first(size),
                                        kBookmarkId, 1));
  }
  for (size_t size = 0; size < preferences->size(); ++size) {
    EXPECT_FALSE(
        DecodeBrowserPreferenceRecordV1(base::span(*preferences).first(size),
                                        kBrowserPreferenceBackupStableId, 1));
  }
  bookmark->push_back(0);
  preferences->push_back(0);
  EXPECT_FALSE(DecodeBookmarkRecordV1(*bookmark, kBookmarkId, 1));
  EXPECT_FALSE(DecodeBrowserPreferenceRecordV1(
      *preferences, kBrowserPreferenceBackupStableId, 1));
}

TEST(BackupBrowserRecordCodecTest, ClosedBooleanAndVersionRefused) {
  auto preferences = EncodeBrowserPreferenceRecordV1(Preferences());
  ASSERT_TRUE(preferences.has_value());
  preferences->back() = 2;
  EXPECT_FALSE(DecodeBrowserPreferenceRecordV1(
      *preferences, kBrowserPreferenceBackupStableId, 1));
  preferences->back() = 1;
  (*preferences)[8] = 2;
  EXPECT_FALSE(DecodeBrowserPreferenceRecordV1(
      *preferences, kBrowserPreferenceBackupStableId, 1));
}

TEST(BackupBrowserRecordCodecTest,
     BookmarkIdentitiesMustBeCanonicalAndDistinct) {
  for (const auto* id : {"", "123", "11111111-1111-4111-8111-11111111111A"}) {
    auto record = Bookmark();
    record.stable_id = id;
    EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  }
  auto record = Bookmark();
  record.parent_id = kBookmarkId;
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record.parent_id = "account-mobile";
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
}

TEST(BackupBrowserRecordCodecTest,
     ExecutableLocalCredentialAndNoncanonicalUrlsRefused) {
  for (const auto* url :
       {"javascript:alert(1)", "data:text/plain,saved",
        "file:///private/document", "content://provider/id",
        "chrome://settings", "https://name@example.test/",
        "https://name:password@example.test/", "https://EXAMPLE.test", ""}) {
    auto record = Bookmark();
    record.url = url;
    EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  }
}

TEST(BackupBrowserRecordCodecTest, BookmarkBoundsAndInvalidTextRefused) {
  auto record = Bookmark();
  record.position = core_service::mojom::kMaxBackupRecords;
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record = Bookmark();
  record.title.assign(4097, 'a');
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record.title = std::string("a\0b", 3);
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record.title = std::string(1, static_cast<char>(0xff));
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record = Bookmark();
  record.url = "https://example.test/" + std::string(65536, 'a');
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record = Bookmark();
  record.date_added_windows_us = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
  record = Bookmark();
  record.date_folder_modified_windows_us = 1;
  EXPECT_FALSE(EncodeBookmarkRecordV1(record));
}

TEST(BackupBrowserRecordCodecTest,
     PresentationChoicesRoundTripWithoutCoercion) {
  for (const auto* theme : {"SYSTEM", "LIGHT", "DARK"}) {
    for (const auto* language : {"SYSTEM", "ENGLISH", "HINDI"}) {
      for (bool dark : {false, true}) {
        BackupBrowserPreferenceRecord value{theme, language, "GB", dark};
        auto encoded = EncodeBrowserPreferenceRecordV1(value);
        ASSERT_TRUE(encoded.has_value());
        auto decoded = DecodeBrowserPreferenceRecordV1(
            *encoded, kBrowserPreferenceBackupStableId, 1);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(*decoded, value);
      }
    }
  }
}

TEST(BackupBrowserRecordCodecTest, PreferenceNamesCannotBeSmuggledAsValues) {
  auto value = Preferences();
  value.theme = "taffy.security.account_session_records";
  EXPECT_FALSE(EncodeBrowserPreferenceRecordV1(value));
  value = Preferences();
  value.app_language = "english";
  EXPECT_FALSE(EncodeBrowserPreferenceRecordV1(value));
  for (const auto* region : {"", "in", "IND", "I1", "I\n"}) {
    value = Preferences();
    value.region_code = region;
    EXPECT_FALSE(EncodeBrowserPreferenceRecordV1(value));
  }
}

}  // namespace
}  // namespace taffy::storage::backup
