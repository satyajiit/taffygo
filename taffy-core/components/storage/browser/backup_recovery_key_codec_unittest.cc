// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_recovery_key_codec.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

constexpr std::u16string_view kGolden =
    u"TAFFY1-00010203-04050607-08090A0B-0C0D0E0F-10111213-14151617-18191A1B-"
    u"1C1D1E1F";

TEST(BackupRecoveryKeyCodecTest,
     FullStrengthKeyMatchesIndependentDisplayVector) {
  Secret key{};
  for (size_t index = 0; index < key.size(); ++index) {
    key[index] = static_cast<uint8_t>(index);
  }
  auto text = FormatRecoveryKeyForDisplay(key);
  ASSERT_TRUE(text.has_value());
  EXPECT_EQ(std::u16string_view(text->data(), text->size()), kGolden);
  auto restored = ParseRecoveryKeyFromInput(*text);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(*restored, key);
}

TEST(BackupRecoveryKeyCodecTest, ExactLengthAndPunctuationAreRequired) {
  for (size_t length = 0; length < kGolden.size(); ++length) {
    EXPECT_FALSE(ParseRecoveryKeyFromInput(base::span(kGolden).first(length)));
  }
  auto longer = std::u16string(kGolden) + u"0";
  EXPECT_FALSE(ParseRecoveryKeyFromInput(longer));
  for (size_t position : {0u, 5u, 6u, 15u, 24u, 33u, 42u, 51u, 60u, 69u}) {
    auto text = std::u16string(kGolden);
    text[position] = u' ';
    EXPECT_FALSE(ParseRecoveryKeyFromInput(text));
  }
}

TEST(BackupRecoveryKeyCodecTest,
     HexCaseIsAcceptedButLookalikesAndWhitespaceAreNot) {
  auto lower = std::u16string(kGolden);
  for (size_t index = 7; index < lower.size(); ++index) {
    if (lower[index] >= u'A' && lower[index] <= u'F') {
      lower[index] = static_cast<char16_t>(lower[index] + (u'a' - u'A'));
    }
  }
  EXPECT_TRUE(ParseRecoveryKeyFromInput(lower));
  for (char16_t invalid : {u'O', u'I', u'\uFF10', u'\u200B', u'\n', u'\0'}) {
    auto text = std::u16string(kGolden);
    text[7] = invalid;
    EXPECT_FALSE(ParseRecoveryKeyFromInput(text));
  }
  auto unsupported = std::u16string(kGolden);
  unsupported[5] = u'2';
  EXPECT_FALSE(ParseRecoveryKeyFromInput(unsupported));
}

TEST(BackupRecoveryKeyCodecTest, DisplayRefusesAnyOtherKeyWidth) {
  const Secret key{};
  EXPECT_FALSE(FormatRecoveryKeyForDisplay(base::span(key).first<31>()));
  EXPECT_FALSE(FormatRecoveryKeyForDisplay(base::span<const uint8_t>()));
  EXPECT_TRUE(FormatRecoveryKeyForDisplay(key));
}

}  // namespace
}  // namespace taffy::storage::backup
