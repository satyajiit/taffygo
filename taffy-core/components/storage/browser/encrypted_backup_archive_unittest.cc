// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/encrypted_backup_archive.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

constexpr char kAibV1PublicHeaderGoldenHex[] =
#include "taffy/components/storage/core/schema/aib_v1_public_header_golden.inc"
    ;

constexpr Secret kRecoveryKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
constexpr std::string_view kManifest =
    "authenticated manifest identity that must not appear in ciphertext";

class EncryptedBackupArchiveTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  base::FilePath Path(std::string_view name) const {
    return directory_.GetPath().AppendASCII(name);
  }

  base::File Create(std::string_view name,
                    base::span<const uint8_t> bytes = {}) const {
    const base::FilePath path = Path(name);
    EXPECT_TRUE(base::WriteFile(path, bytes));
    return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                base::File::FLAG_WRITE);
  }

  base::File Open(std::string_view name) const {
    return base::File(Path(name), base::File::FLAG_OPEN |
                                      base::File::FLAG_READ |
                                      base::File::FLAG_WRITE);
  }

  std::vector<uint8_t> Bytes(std::string_view name) const {
    const auto bytes = base::ReadFileToBytes(Path(name));
    EXPECT_TRUE(bytes);
    return bytes.value_or(std::vector<uint8_t>());
  }

  AibWriteArchiveResult WriteArchive(base::span<const uint8_t> payload) {
    payload_ = Create("payload", payload);
    archive_ = Create("archive");
    return WriteAibArchive(&archive_, &payload_, payload.size(), kRecoveryKey,
                           base::as_byte_span(kManifest));
  }

  base::ScopedTempDir directory_;
  base::File payload_;
  base::File archive_;
};

TEST_F(EncryptedBackupArchiveTest, StreamsMultipleChunksAndRoundTrips) {
  std::vector<uint8_t> payload(kPayloadPlaintextChunkBytes + 37);
  for (size_t index = 0; index < payload.size(); ++index) {
    payload[index] = static_cast<uint8_t>(index % 251);
  }
  const AibWriteArchiveResult written = WriteArchive(payload);
  ASSERT_TRUE(written);
  EXPECT_EQ(written->archive_bytes,
            static_cast<uint64_t>(archive_.GetLength()));

  const AibHeaderResult inspected = InspectAibArchive(&archive_);
  ASSERT_TRUE(inspected);
  EXPECT_EQ(*inspected, written->header);
  base::File opened_payload = Create("opened");
  const AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, &opened_payload, kRecoveryKey);
  ASSERT_TRUE(opened);
  EXPECT_EQ(opened->header, written->header);
  EXPECT_EQ(opened->manifest,
            std::vector<uint8_t>(kManifest.begin(), kManifest.end()));
  EXPECT_EQ(opened->payload_plaintext_bytes, payload.size());
  EXPECT_EQ(Bytes("opened"), payload);

  const std::vector<uint8_t> archive_bytes = Bytes("archive");
  EXPECT_EQ(std::search(archive_bytes.begin(), archive_bytes.end(),
                        kManifest.begin(), kManifest.end()),
            archive_bytes.end());
  EXPECT_EQ(std::search(archive_bytes.begin(), archive_bytes.end(),
                        payload.begin(), payload.end()),
            archive_bytes.end());
}

TEST_F(EncryptedBackupArchiveTest, ReadbackCanAuthenticateWithoutCopying) {
  const std::array<uint8_t, 4> payload = {1, 2, 3, 4};
  ASSERT_TRUE(WriteArchive(payload));
  const AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, nullptr, kRecoveryKey);
  ASSERT_TRUE(opened);
  EXPECT_EQ(opened->payload_plaintext_bytes, payload.size());
  EXPECT_EQ(opened->manifest,
            std::vector<uint8_t>(kManifest.begin(), kManifest.end()));
}

TEST_F(EncryptedBackupArchiveTest, WrongKeyFailsAndClearsStaging) {
  const std::array<uint8_t, 5> payload = {5, 4, 3, 2, 1};
  ASSERT_TRUE(WriteArchive(payload));
  const std::array<uint8_t, 3> stale = {9, 9, 9};
  base::File staging = Create("staging", stale);
  Secret wrong_key = kRecoveryKey;
  wrong_key.front() ^= 1;
  const AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, &staging, wrong_key);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kAuthenticationFailed);
  EXPECT_EQ(staging.GetLength(), 0);
}

TEST_F(EncryptedBackupArchiveTest, SourceAndStagingMustNotAlias) {
  const std::array<uint8_t, 5> payload = {5, 4, 3, 2, 1};
  ASSERT_TRUE(WriteArchive(payload));
  const std::vector<uint8_t> original = Bytes("archive");

  AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, &archive_, kRecoveryKey);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kInvalidArgument);
  EXPECT_EQ(Bytes("archive"), original);

  base::File second_handle = Open("archive");
  opened = OpenAibArchive(&archive_, &second_handle, kRecoveryKey);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kInvalidArgument);
  EXPECT_EQ(Bytes("archive"), original);
}

TEST_F(EncryptedBackupArchiveTest, TruncationAndTrailingDataFailClosed) {
  const std::array<uint8_t, 3> payload = {7, 8, 9};
  ASSERT_TRUE(WriteArchive(payload));
  archive_.Close();
  std::vector<uint8_t> bytes = Bytes("archive");

  bytes.pop_back();
  ASSERT_TRUE(base::WriteFile(Path("archive"), bytes));
  archive_ = Open("archive");
  auto opened = OpenAibArchive(&archive_, nullptr, kRecoveryKey);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kTruncated);

  archive_.Close();
  bytes.push_back(0);
  bytes.push_back(0);
  ASSERT_TRUE(base::WriteFile(Path("archive"), bytes));
  archive_ = Open("archive");
  opened = OpenAibArchive(&archive_, nullptr, kRecoveryKey);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kTrailingData);
}

TEST_F(EncryptedBackupArchiveTest, EveryEncryptedSectionIsAuthenticated) {
  const std::array<uint8_t, 6> payload = {1, 1, 2, 3, 5, 8};
  const AibWriteArchiveResult written = WriteArchive(payload);
  ASSERT_TRUE(written);
  archive_.Close();
  const std::vector<uint8_t> original = Bytes("archive");
  const size_t wrapped_offset = kAibPublicHeaderBytes;
  const size_t manifest_ciphertext_offset =
      wrapped_offset + kWrappedArchiveKeyBytes + kNonceBytes;
  const size_t payload_ciphertext_offset =
      wrapped_offset + kWrappedArchiveKeyBytes +
      static_cast<size_t>(written->header.encrypted_manifest_bytes) +
      kNonceBytes;

  for (size_t offset : {wrapped_offset, manifest_ciphertext_offset,
                        payload_ciphertext_offset}) {
    std::vector<uint8_t> corrupted = original;
    corrupted[offset] ^= 1;
    ASSERT_TRUE(base::WriteFile(Path("corrupt"), corrupted));
    base::File source = Open("corrupt");
    const AibOpenArchiveResult opened =
        OpenAibArchive(&source, nullptr, kRecoveryKey);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), AibArchiveError::kAuthenticationFailed);
  }
}

TEST_F(EncryptedBackupArchiveTest, PublicSectionLengthsAreAuthenticated) {
  std::vector<uint8_t> payload(kPayloadPlaintextChunkBytes + 1u, 0x5a);
  const AibWriteArchiveResult written = WriteArchive(payload);
  ASSERT_TRUE(written);
  archive_.Close();
  std::vector<uint8_t> bytes = Bytes("archive");

  // Keep the total file length and both section shapes valid while moving one
  // complete payload frame into the claimed manifest section. Without binding
  // the public prefix into AEAD this mutation would rely only on incidental
  // ciphertext boundary failure rather than authenticating the header itself.
  constexpr uint64_t kFrameBytes =
      kPayloadPlaintextChunkBytes + kNonceBytes + kAuthenticationTagBytes;
  AibPublicHeader mutated = written->header;
  mutated.encrypted_manifest_bytes += kFrameBytes;
  mutated.encrypted_payload_bytes -= kFrameBytes;
  const auto encoded = EncodeAibPublicHeader(mutated);
  std::ranges::copy(encoded, bytes.begin());
  ASSERT_TRUE(base::WriteFile(Path("archive"), bytes));
  archive_ = Open("archive");
  const AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, nullptr, kRecoveryKey);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kAuthenticationFailed);
}

TEST_F(EncryptedBackupArchiveTest, ReusedSectionNonceIsNamedBeforeOpening) {
  const std::array<uint8_t, 1> payload = {42};
  const AibWriteArchiveResult written = WriteArchive(payload);
  ASSERT_TRUE(written);
  archive_.Close();
  std::vector<uint8_t> bytes = Bytes("archive");
  const size_t manifest_nonce_offset =
      kAibPublicHeaderBytes + kWrappedArchiveKeyBytes;
  const size_t payload_nonce_offset =
      manifest_nonce_offset +
      static_cast<size_t>(written->header.encrypted_manifest_bytes);
  std::copy_n(bytes.begin() + manifest_nonce_offset, kNonceBytes,
              bytes.begin() + payload_nonce_offset);
  ASSERT_TRUE(base::WriteFile(Path("archive"), bytes));
  archive_ = Open("archive");
  const AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, nullptr, kRecoveryKey);
  ASSERT_FALSE(opened);
  EXPECT_EQ(opened.error(), AibArchiveError::kNonceReuse);
}

TEST_F(EncryptedBackupArchiveTest, HeaderRejectsPublicShapeMutations) {
  AibPublicHeader header{
      .archive_id = GenerateArchiveId(),
      .key_wrap_nonce = GenerateNonce(),
      .encrypted_manifest_bytes = kNonceBytes + kAuthenticationTagBytes + 1,
      .encrypted_payload_bytes = 0,
  };
  auto encoded = EncodeAibPublicHeader(header);
  ASSERT_TRUE(DecodeAibPublicHeader(encoded));

  encoded.front() ^= 1;
  auto decoded = DecodeAibPublicHeader(encoded);
  ASSERT_FALSE(decoded);
  EXPECT_EQ(decoded.error(), AibArchiveError::kWrongMagic);
  encoded = EncodeAibPublicHeader(header);
  encoded[8] = 2;
  decoded = DecodeAibPublicHeader(encoded);
  ASSERT_FALSE(decoded);
  EXPECT_EQ(decoded.error(), AibArchiveError::kUnsupportedFormat);
  encoded = EncodeAibPublicHeader(header);
  encoded[10] = 2;
  decoded = DecodeAibPublicHeader(encoded);
  ASSERT_FALSE(decoded);
  EXPECT_EQ(decoded.error(), AibArchiveError::kUnsupportedCipherSuite);

  header.encrypted_payload_bytes = 1;
  decoded = DecodeAibPublicHeader(EncodeAibPublicHeader(header));
  ASSERT_FALSE(decoded);
  EXPECT_EQ(decoded.error(), AibArchiveError::kMalformedHeader);
  header.encrypted_payload_bytes = 0;
  header.archive_id = {};
  decoded = DecodeAibPublicHeader(EncodeAibPublicHeader(header));
  ASSERT_FALSE(decoded);
  EXPECT_EQ(decoded.error(), AibArchiveError::kMalformedHeader);
}

TEST_F(EncryptedBackupArchiveTest, PublicHeaderMatchesPortableGolden) {
  AibPublicHeader header{
      .archive_id = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                     0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f},
      .key_wrap_nonce = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                         0x19, 0x1a, 0x1b},
      .encrypted_manifest_bytes = 0x101,
      .encrypted_payload_bytes = 0x10001c,
  };
  std::vector<uint8_t> golden;
  ASSERT_TRUE(base::HexStringToBytes(kAibV1PublicHeaderGoldenHex, &golden));
  EXPECT_EQ(base::as_byte_span(EncodeAibPublicHeader(header)),
            base::as_byte_span(golden));
  const AibHeaderResult decoded = DecodeAibPublicHeader(golden);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded, header);
}

TEST_F(EncryptedBackupArchiveTest, EmptyPayloadStillProducesValidArchive) {
  payload_ = Create("payload");
  archive_ = Create("archive");
  const AibWriteArchiveResult written = WriteAibArchive(
      &archive_, &payload_, 0, kRecoveryKey, base::as_byte_span(kManifest));
  ASSERT_TRUE(written);
  EXPECT_EQ(written->header.encrypted_payload_bytes, 0u);
  const AibOpenArchiveResult opened =
      OpenAibArchive(&archive_, nullptr, kRecoveryKey);
  ASSERT_TRUE(opened);
  EXPECT_EQ(opened->payload_plaintext_bytes, 0u);
}

}  // namespace
}  // namespace taffy::storage::backup
