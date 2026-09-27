// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/encrypted_backup_archive.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/numerics/byte_conversions.h"
#include "crypto/secure_util.h"

namespace taffy::storage::backup {
namespace {

constexpr size_t kFormatOffset = 8;
constexpr size_t kCipherOffset = 10;
constexpr size_t kArchiveIdOffset = 12;
constexpr size_t kWrapNonceOffset = 28;
constexpr size_t kManifestBytesOffset = 40;
constexpr size_t kPayloadBytesOffset = 48;
constexpr uint64_t kSectionFrameOverhead =
    kNonceBytes + kAuthenticationTagBytes;

template <typename Range>
bool IsAllZero(const Range& bytes) {
  return std::ranges::all_of(bytes, [](uint8_t byte) { return byte == 0; });
}

std::optional<uint64_t> CheckedAdd(uint64_t left, uint64_t right) {
  if (right > std::numeric_limits<uint64_t>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

std::optional<uint64_t> PayloadSectionBytes(uint64_t plaintext_bytes) {
  if (plaintext_bytes == 0) {
    return 0;
  }
  const uint64_t chunk_count =
      1 + ((plaintext_bytes - 1) / kPayloadPlaintextChunkBytes);
  if (chunk_count >
      std::numeric_limits<uint64_t>::max() / kSectionFrameOverhead) {
    return std::nullopt;
  }
  return CheckedAdd(plaintext_bytes, chunk_count * kSectionFrameOverhead);
}

bool IsCanonicalPayloadSectionBytes(uint64_t section_bytes) {
  if (section_bytes == 0) {
    return true;
  }
  constexpr uint64_t kFullFrameBytes =
      kPayloadPlaintextChunkBytes + kSectionFrameOverhead;
  const uint64_t final_frame_bytes = section_bytes % kFullFrameBytes;
  return final_frame_bytes == 0 || final_frame_bytes > kSectionFrameOverhead;
}

std::optional<uint64_t> ArchiveBytes(const AibPublicHeader& header) {
  auto size = CheckedAdd(kAibPublicHeaderBytes, kWrappedArchiveKeyBytes);
  if (!size) {
    return std::nullopt;
  }
  size = CheckedAdd(*size, header.encrypted_manifest_bytes);
  return size ? CheckedAdd(*size, header.encrypted_payload_bytes)
              : std::nullopt;
}

bool Write(base::File* destination, base::span<const uint8_t> bytes) {
  return destination->WriteAtCurrentPosAndCheck(bytes);
}

bool Read(base::File* source, base::span<uint8_t> bytes) {
  return source->ReadAtCurrentPosAndCheck(bytes);
}

template <typename Result>
base::expected<Result, AibArchiveError> WriteFailure(base::File* destination,
                                                     AibArchiveError error) {
  destination->SetLength(0);
  return base::unexpected(error);
}

class ScopedSecretWipe {
 public:
  explicit ScopedSecretWipe(Secret* secret) : secret_(secret) {}
  ScopedSecretWipe(const ScopedSecretWipe&) = delete;
  ScopedSecretWipe& operator=(const ScopedSecretWipe&) = delete;
  ~ScopedSecretWipe() { crypto::SecureZeroBuffer(*secret_); }

 private:
  raw_ptr<Secret> secret_;
};

std::optional<Nonce> FreshNonce(std::set<Nonce>* used) {
  Nonce nonce = GenerateNonce();
  if (IsAllZero(nonce) || !used->insert(nonce).second) {
    return std::nullopt;
  }
  return nonce;
}

}  // namespace

std::array<uint8_t, kAibPublicHeaderBytes> EncodeAibPublicHeader(
    const AibPublicHeader& header) {
  std::array<uint8_t, kAibPublicHeaderBytes> encoded{};
  std::ranges::copy(kAibMagic, encoded.begin());
  std::ranges::copy(base::U16ToLittleEndian(kAibFormatVersion),
                    encoded.begin() + kFormatOffset);
  std::ranges::copy(base::U16ToLittleEndian(kAibCipherSuiteAes256GcmHkdfSha256),
                    encoded.begin() + kCipherOffset);
  std::ranges::copy(header.archive_id, encoded.begin() + kArchiveIdOffset);
  std::ranges::copy(header.key_wrap_nonce, encoded.begin() + kWrapNonceOffset);
  std::ranges::copy(base::U64ToLittleEndian(header.encrypted_manifest_bytes),
                    encoded.begin() + kManifestBytesOffset);
  std::ranges::copy(base::U64ToLittleEndian(header.encrypted_payload_bytes),
                    encoded.begin() + kPayloadBytesOffset);
  return encoded;
}

AibHeaderResult DecodeAibPublicHeader(
    base::span<const uint8_t> encoded_header) {
  if (encoded_header.size() != kAibPublicHeaderBytes) {
    return base::unexpected(AibArchiveError::kMalformedHeader);
  }
  if (!std::ranges::equal(encoded_header.first<kAibMagic.size()>(),
                          kAibMagic)) {
    return base::unexpected(AibArchiveError::kWrongMagic);
  }
  if (base::U16FromLittleEndian(
          encoded_header.subspan<kFormatOffset, sizeof(uint16_t)>()) !=
      kAibFormatVersion) {
    return base::unexpected(AibArchiveError::kUnsupportedFormat);
  }
  if (base::U16FromLittleEndian(
          encoded_header.subspan<kCipherOffset, sizeof(uint16_t)>()) !=
      kAibCipherSuiteAes256GcmHkdfSha256) {
    return base::unexpected(AibArchiveError::kUnsupportedCipherSuite);
  }

  AibPublicHeader header;
  std::ranges::copy(encoded_header.subspan<kArchiveIdOffset, kArchiveIdBytes>(),
                    header.archive_id.begin());
  std::ranges::copy(encoded_header.subspan<kWrapNonceOffset, kNonceBytes>(),
                    header.key_wrap_nonce.begin());
  header.encrypted_manifest_bytes = base::U64FromLittleEndian(
      encoded_header.subspan<kManifestBytesOffset, sizeof(uint64_t)>());
  header.encrypted_payload_bytes = base::U64FromLittleEndian(
      encoded_header.subspan<kPayloadBytesOffset, sizeof(uint64_t)>());

  if (IsAllZero(header.archive_id) || IsAllZero(header.key_wrap_nonce) ||
      header.encrypted_manifest_bytes <= kSectionFrameOverhead) {
    return base::unexpected(AibArchiveError::kMalformedHeader);
  }
  if (header.encrypted_manifest_bytes >
      kMaxManifestPlaintextBytes + kSectionFrameOverhead) {
    return base::unexpected(AibArchiveError::kManifestTooLarge);
  }
  const auto max_payload_section =
      PayloadSectionBytes(kMaxPayloadPlaintextBytes);
  if (!IsCanonicalPayloadSectionBytes(header.encrypted_payload_bytes)) {
    return base::unexpected(AibArchiveError::kMalformedHeader);
  }
  if (!max_payload_section ||
      header.encrypted_payload_bytes > *max_payload_section) {
    return base::unexpected(AibArchiveError::kPayloadTooLarge);
  }
  const auto encrypted_sections = CheckedAdd(header.encrypted_manifest_bytes,
                                             header.encrypted_payload_bytes);
  if (!encrypted_sections || *encrypted_sections > kMaxEncryptedSectionsBytes) {
    return base::unexpected(AibArchiveError::kArchiveTooLarge);
  }
  return header;
}

AibHeaderResult InspectAibArchive(base::File* source) {
  if (!source || !source->IsValid() ||
      source->Seek(base::File::FROM_BEGIN, 0) != 0) {
    return base::unexpected(AibArchiveError::kUnreadableSource);
  }
  std::array<uint8_t, kAibPublicHeaderBytes> encoded{};
  if (!Read(source, encoded)) {
    return base::unexpected(AibArchiveError::kTruncated);
  }
  return DecodeAibPublicHeader(encoded);
}

AibWriteArchiveResult WriteAibArchive(
    base::File* destination,
    base::File* plaintext_payload,
    uint64_t payload_plaintext_bytes,
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> manifest_plaintext) {
  if (!destination || !destination->IsValid() ||
      recovery_key.size() != kSecretBytes || manifest_plaintext.empty()) {
    return destination ? WriteFailure<AibWriteResult>(
                             destination, AibArchiveError::kInvalidArgument)
                       : base::unexpected(AibArchiveError::kInvalidArgument);
  }
  if (manifest_plaintext.size() > kMaxManifestPlaintextBytes) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kManifestTooLarge);
  }
  if (payload_plaintext_bytes > kMaxPayloadPlaintextBytes) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kPayloadTooLarge);
  }
  if ((payload_plaintext_bytes != 0 &&
       (!plaintext_payload || !plaintext_payload->IsValid())) ||
      (plaintext_payload && plaintext_payload->IsValid() &&
       (plaintext_payload->GetLength() < 0 ||
        static_cast<uint64_t>(plaintext_payload->GetLength()) !=
            payload_plaintext_bytes))) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kUnreadableSource);
  }
  if (!destination->SetLength(0) ||
      destination->Seek(base::File::FROM_BEGIN, 0) != 0 ||
      (payload_plaintext_bytes != 0 &&
       plaintext_payload->Seek(base::File::FROM_BEGIN, 0) != 0)) {
    return WriteFailure<AibWriteResult>(
        destination, AibArchiveError::kUnwritableDestination);
  }

  AibPublicHeader header;
  header.archive_id = GenerateArchiveId();
  header.key_wrap_nonce = GenerateNonce();
  if (IsAllZero(header.archive_id) || IsAllZero(header.key_wrap_nonce)) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kNonceReuse);
  }
  std::set<Nonce> used_nonces = {header.key_wrap_nonce};
  const std::optional<Nonce> manifest_nonce = FreshNonce(&used_nonces);
  if (!manifest_nonce) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kNonceReuse);
  }

  Secret archive_key = GenerateSecret();
  ScopedSecretWipe wipe_archive_key(&archive_key);
  const auto payload_section = PayloadSectionBytes(payload_plaintext_bytes);
  if (!payload_section) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kAuthenticationFailed);
  }
  header.encrypted_manifest_bytes = manifest_nonce->size() +
                                    manifest_plaintext.size() +
                                    kAuthenticationTagBytes;
  header.encrypted_payload_bytes = *payload_section;
  const auto archive_bytes = ArchiveBytes(header);
  if (!archive_bytes ||
      header.encrypted_manifest_bytes + header.encrypted_payload_bytes >
          kMaxEncryptedSectionsBytes) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kArchiveTooLarge);
  }

  const auto encoded_header = EncodeAibPublicHeader(header);
  const auto wrapped_key =
      WrapArchiveKey(recovery_key, header.archive_id, encoded_header,
                     header.key_wrap_nonce, archive_key);
  const auto sealed_manifest = SealChunk(archive_key, encoded_header, 0,
                                         *manifest_nonce, manifest_plaintext);
  if (!wrapped_key || !sealed_manifest ||
      sealed_manifest->size() + manifest_nonce->size() !=
          header.encrypted_manifest_bytes) {
    return WriteFailure<AibWriteResult>(destination,
                                        AibArchiveError::kAuthenticationFailed);
  }
  if (!Write(destination, encoded_header) ||
      !Write(destination, *wrapped_key) ||
      !Write(destination, *manifest_nonce) ||
      !Write(destination, *sealed_manifest)) {
    return WriteFailure<AibWriteResult>(
        destination, AibArchiveError::kUnwritableDestination);
  }

  uint64_t remaining = payload_plaintext_bytes;
  uint32_t chunk_index = 1;
  std::vector<uint8_t> plaintext(kPayloadPlaintextChunkBytes);
  while (remaining != 0) {
    const size_t chunk_bytes = static_cast<size_t>(
        std::min<uint64_t>(remaining, kPayloadPlaintextChunkBytes));
    auto chunk = base::span(plaintext).first(chunk_bytes);
    if (!Read(plaintext_payload, chunk)) {
      crypto::SecureZeroBuffer(plaintext);
      return WriteFailure<AibWriteResult>(destination,
                                          AibArchiveError::kUnreadableSource);
    }
    const std::optional<Nonce> nonce = FreshNonce(&used_nonces);
    const auto sealed = nonce ? SealChunk(archive_key, encoded_header,
                                          chunk_index, *nonce, chunk)
                              : std::nullopt;
    crypto::SecureZeroBuffer(chunk);
    if (!nonce || !sealed) {
      return WriteFailure<AibWriteResult>(destination,
                                          AibArchiveError::kNonceReuse);
    }
    if (!Write(destination, *nonce) || !Write(destination, *sealed)) {
      return WriteFailure<AibWriteResult>(
          destination, AibArchiveError::kUnwritableDestination);
    }
    remaining -= chunk_bytes;
    ++chunk_index;
  }
  if (!destination->Flush() || destination->GetLength() < 0 ||
      static_cast<uint64_t>(destination->GetLength()) != *archive_bytes) {
    return WriteFailure<AibWriteResult>(
        destination, AibArchiveError::kUnwritableDestination);
  }
  return AibWriteResult{.header = header, .archive_bytes = *archive_bytes};
}

}  // namespace taffy::storage::backup
