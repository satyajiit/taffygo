// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_recovery_key_codec.h"

#include <algorithm>
#include <string_view>

#include "crypto/secure_util.h"

namespace taffy::storage::backup {
namespace {

constexpr std::u16string_view kPrefix = u"TAFFY1-";
constexpr std::u16string_view kDigits = u"0123456789ABCDEF";
static_assert(kRecoveryKeyTextChars == kPrefix.size() + kSecretBytes * 2 + 7);

std::optional<uint8_t> HexDigit(char16_t value) {
  if (value >= u'0' && value <= u'9') {
    return static_cast<uint8_t>(value - u'0');
  }
  if (value >= u'A' && value <= u'F') {
    return static_cast<uint8_t>(value - u'A' + 10);
  }
  if (value >= u'a' && value <= u'f') {
    return static_cast<uint8_t>(value - u'a' + 10);
  }
  return std::nullopt;
}

}  // namespace

std::optional<RecoveryKeyText> FormatRecoveryKeyForDisplay(
    base::span<const uint8_t> key) {
  if (key.size() != kSecretBytes) {
    return std::nullopt;
  }
  RecoveryKeyText text{};
  std::ranges::copy(kPrefix, text.begin());
  size_t position = kPrefix.size();
  for (size_t index = 0; index < key.size(); ++index) {
    if (index != 0 && index % 4 == 0) {
      text[position++] = u'-';
    }
    text[position++] = kDigits[key[index] >> 4];
    text[position++] = kDigits[key[index] & 0xf];
  }
  return text;
}

std::optional<Secret> ParseRecoveryKeyFromInput(
    base::span<const char16_t> text) {
  if (text.size() != kRecoveryKeyTextChars ||
      !std::ranges::equal(text.first(kPrefix.size()), kPrefix)) {
    return std::nullopt;
  }
  std::optional<Secret> key(std::in_place);
  size_t position = kPrefix.size();
  for (size_t index = 0; index < key->size(); ++index) {
    if (index != 0 && index % 4 == 0 && text[position++] != u'-') {
      crypto::SecureZeroBuffer(*key);
      return std::nullopt;
    }
    const auto high = HexDigit(text[position++]);
    const auto low = HexDigit(text[position++]);
    if (!high || !low) {
      crypto::SecureZeroBuffer(*key);
      return std::nullopt;
    }
    (*key)[index] = static_cast<uint8_t>((*high << 4) | *low);
  }
  return key;
}

}  // namespace taffy::storage::backup
