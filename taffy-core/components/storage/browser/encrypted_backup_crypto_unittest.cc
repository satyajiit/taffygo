// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/encrypted_backup_crypto.h"

#include <array>
#include <cstdint>
#include <vector>

#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

constexpr Secret kRecoveryKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
constexpr Secret kArchiveKey = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};
constexpr ArchiveId kArchiveId = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
};
constexpr Nonce kWrapNonce = {
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b,
};
constexpr Nonce kChunkNonce = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
};
constexpr std::array<uint8_t, kArchiveHeaderBindingBytes> kArchiveHeader = {
    'T',  'A',  'F',  'F',  'Y',  'A',  'I',  'B',  1,    0,
    1,    0,    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
};

TEST(EncryptedBackupCryptoTest, WrapRoundTripsOnlyUnderExactArchiveAndKey) {
  const auto wrapped = WrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                                      kWrapNonce, kArchiveKey);
  ASSERT_TRUE(wrapped);
  EXPECT_EQ(wrapped->size(), kSecretBytes + kAuthenticationTagBytes);
  EXPECT_EQ(UnwrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                             kWrapNonce, *wrapped),
            kArchiveKey);

  Secret wrong_key = kRecoveryKey;
  wrong_key.front() ^= 1;
  EXPECT_FALSE(UnwrapArchiveKey(wrong_key, kArchiveId, kArchiveHeader,
                                kWrapNonce, *wrapped));

  ArchiveId wrong_archive = kArchiveId;
  wrong_archive.back() ^= 1;
  EXPECT_FALSE(UnwrapArchiveKey(kRecoveryKey, wrong_archive, kArchiveHeader,
                                kWrapNonce, *wrapped));

  auto wrong_header = kArchiveHeader;
  wrong_header.back() ^= 1;
  EXPECT_FALSE(UnwrapArchiveKey(kRecoveryKey, kArchiveId, wrong_header,
                                kWrapNonce, *wrapped));
}

TEST(EncryptedBackupCryptoTest, WrappedKeyRefusesEveryByteShapeMutation) {
  const auto wrapped = WrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                                      kWrapNonce, kArchiveKey);
  ASSERT_TRUE(wrapped);

  for (size_t index = 0; index < wrapped->size(); ++index) {
    std::vector<uint8_t> mutated = *wrapped;
    mutated[index] ^= 1;
    EXPECT_FALSE(UnwrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                                  kWrapNonce, mutated));
  }
  std::vector<uint8_t> truncated(wrapped->begin(), wrapped->end() - 1);
  EXPECT_FALSE(UnwrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                                kWrapNonce, truncated));
  std::vector<uint8_t> extended = *wrapped;
  extended.push_back(0);
  EXPECT_FALSE(UnwrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                                kWrapNonce, extended));
}

TEST(EncryptedBackupCryptoTest, ChunkBindsArchiveIndexNonceAndCiphertext) {
  constexpr std::array<uint8_t, 7> kPlaintext = {'s', 'e', 'c', 'r',
                                                 'e', 't', 0};
  const auto sealed = SealChunk(kArchiveKey, kArchiveHeader, 7, kChunkNonce,
                                base::span(kPlaintext));
  ASSERT_TRUE(sealed);
  EXPECT_EQ(OpenChunk(kArchiveKey, kArchiveHeader, 7, kChunkNonce, *sealed),
            std::vector<uint8_t>(kPlaintext.begin(), kPlaintext.end()));
  EXPECT_FALSE(OpenChunk(kArchiveKey, kArchiveHeader, 8, kChunkNonce, *sealed));

  Nonce wrong_nonce = kChunkNonce;
  wrong_nonce.front() ^= 1;
  EXPECT_FALSE(OpenChunk(kArchiveKey, kArchiveHeader, 7, wrong_nonce, *sealed));

  auto wrong_header = kArchiveHeader;
  wrong_header.back() ^= 1;
  EXPECT_FALSE(OpenChunk(kArchiveKey, wrong_header, 7, kChunkNonce, *sealed));

  std::vector<uint8_t> modified = *sealed;
  modified.back() ^= 1;
  EXPECT_FALSE(
      OpenChunk(kArchiveKey, kArchiveHeader, 7, kChunkNonce, modified));
}

TEST(EncryptedBackupCryptoTest, InvalidSizesFailBeforeCryptoChecks) {
  constexpr std::array<uint8_t, 1> kShort = {0};
  EXPECT_FALSE(WrapArchiveKey(kShort, kArchiveId, kArchiveHeader, kWrapNonce,
                              kArchiveKey));
  EXPECT_FALSE(WrapArchiveKey(kRecoveryKey, kShort, kArchiveHeader, kWrapNonce,
                              kArchiveKey));
  EXPECT_FALSE(WrapArchiveKey(kRecoveryKey, kArchiveId, kShort, kWrapNonce,
                              kArchiveKey));
  EXPECT_FALSE(WrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader, kShort,
                              kArchiveKey));
  EXPECT_FALSE(WrapArchiveKey(kRecoveryKey, kArchiveId, kArchiveHeader,
                              kWrapNonce, kShort));
  EXPECT_FALSE(OpenChunk(kShort, kArchiveHeader, 0, kChunkNonce, kShort));
}

TEST(EncryptedBackupCryptoTest, GeneratedMaterialUsesFreshRandomDraws) {
  EXPECT_NE(GenerateSecret(), GenerateSecret());
  EXPECT_NE(GenerateArchiveId(), GenerateArchiveId());
  EXPECT_NE(GenerateNonce(), GenerateNonce());
}

}  // namespace
}  // namespace taffy::storage::backup
