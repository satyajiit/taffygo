// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "crypto/secure_util.h"
#include "taffy/components/storage/browser/encrypted_backup_archive.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

namespace taffy::storage::backup {
namespace {

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

std::optional<uint64_t> ExpectedArchiveBytes(const AibPublicHeader& header) {
  auto size = CheckedAdd(kAibPublicHeaderBytes, kWrappedArchiveKeyBytes);
  size =
      size ? CheckedAdd(*size, header.encrypted_manifest_bytes) : std::nullopt;
  return size ? CheckedAdd(*size, header.encrypted_payload_bytes)
              : std::nullopt;
}

bool Read(base::File* source, base::span<uint8_t> bytes) {
  return source->ReadAtCurrentPosAndCheck(bytes);
}

std::optional<bool> FilesAlias(base::File* source,
                               base::File* destination) {
  if (!destination) {
    return false;
  }
  if (source == destination ||
      source->GetPlatformFile() == destination->GetPlatformFile()) {
    return true;
  }
#if BUILDFLAG(IS_POSIX)
  struct stat source_stat = {};
  struct stat destination_stat = {};
  if (fstat(source->GetPlatformFile(), &source_stat) != 0 ||
      fstat(destination->GetPlatformFile(), &destination_stat) != 0) {
    return std::nullopt;
  }
  return source_stat.st_dev == destination_stat.st_dev &&
         source_stat.st_ino == destination_stat.st_ino;
#else
  return false;
#endif
}

template <typename Result>
base::expected<Result, AibArchiveError> Failure(
    base::File* plaintext_destination,
    AibArchiveError error) {
  if (plaintext_destination) {
    plaintext_destination->SetLength(0);
  }
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

class ScopedVectorWipe {
 public:
  explicit ScopedVectorWipe(std::vector<uint8_t>* bytes) : bytes_(bytes) {}
  ScopedVectorWipe(const ScopedVectorWipe&) = delete;
  ScopedVectorWipe& operator=(const ScopedVectorWipe&) = delete;
  ~ScopedVectorWipe() {
    if (armed_) {
      crypto::SecureZeroBuffer(*bytes_);
    }
  }

  // Ownership of successfully authenticated manifest plaintext moves to the
  // caller. Every failure after opening it leaves this guard armed so the
  // identifying metadata and hashes do not remain in a freed heap buffer.
  void Release() { armed_ = false; }

 private:
  raw_ptr<std::vector<uint8_t>> bytes_;
  bool armed_ = true;
};

}  // namespace

AibOpenArchiveResult OpenAibArchive(base::File* source,
                                    base::File* plaintext_payload_destination,
                                    base::span<const uint8_t> recovery_key) {
  if (!source || !source->IsValid() || recovery_key.size() != kSecretBytes ||
      (plaintext_payload_destination &&
       !plaintext_payload_destination->IsValid())) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kInvalidArgument);
  }
  const std::optional<bool> aliases =
      FilesAlias(source, plaintext_payload_destination);
  if (!aliases || *aliases) {
    return Failure<AibOpenResult>(nullptr,
                                  AibArchiveError::kInvalidArgument);
  }
  const int64_t source_length = source->GetLength();
  const AibHeaderResult header_result = InspectAibArchive(source);
  if (!header_result) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  header_result.error());
  }
  const AibPublicHeader& header = *header_result;
  const auto encoded_header = EncodeAibPublicHeader(header);
  const auto expected_archive_bytes = ExpectedArchiveBytes(header);
  if (source_length < 0 || !expected_archive_bytes ||
      static_cast<uint64_t>(source_length) < *expected_archive_bytes) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kTruncated);
  }
  if (static_cast<uint64_t>(source_length) > *expected_archive_bytes) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kTrailingData);
  }
  std::array<uint8_t, kWrappedArchiveKeyBytes> wrapped_key{};
  if (!Read(source, wrapped_key)) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kTruncated);
  }
  auto archive_key =
      UnwrapArchiveKey(recovery_key, header.archive_id, encoded_header,
                       header.key_wrap_nonce, wrapped_key);
  if (!archive_key) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kAuthenticationFailed);
  }
  ScopedSecretWipe wipe_archive_key(&*archive_key);
  std::set<Nonce> used_nonces = {header.key_wrap_nonce};

  Nonce manifest_nonce{};
  const size_t manifest_ciphertext_bytes =
      static_cast<size_t>(header.encrypted_manifest_bytes - kNonceBytes);
  std::vector<uint8_t> sealed_manifest(manifest_ciphertext_bytes);
  if (!Read(source, manifest_nonce) || !Read(source, sealed_manifest)) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kTruncated);
  }
  if (IsAllZero(manifest_nonce) || !used_nonces.insert(manifest_nonce).second) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kNonceReuse);
  }
  auto manifest = OpenChunk(*archive_key, encoded_header, 0, manifest_nonce,
                            sealed_manifest);
  if (!manifest || manifest->empty()) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kAuthenticationFailed);
  }
  ScopedVectorWipe wipe_manifest(&*manifest);
  if (plaintext_payload_destination &&
      (!plaintext_payload_destination->SetLength(0) ||
       plaintext_payload_destination->Seek(base::File::FROM_BEGIN, 0) != 0)) {
    return Failure<AibOpenResult>(plaintext_payload_destination,
                                  AibArchiveError::kUnwritableDestination);
  }

  uint64_t section_remaining = header.encrypted_payload_bytes;
  uint64_t plaintext_bytes = 0;
  uint32_t chunk_index = 1;
  constexpr uint64_t kFullFrameBytes =
      kPayloadPlaintextChunkBytes + kSectionFrameOverhead;
  while (section_remaining != 0) {
    const uint64_t frame_bytes = std::min(section_remaining, kFullFrameBytes);
    if (frame_bytes <= kSectionFrameOverhead) {
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kTruncated);
    }
    Nonce nonce{};
    std::vector<uint8_t> sealed(static_cast<size_t>(frame_bytes - kNonceBytes));
    if (!Read(source, nonce) || !Read(source, sealed)) {
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kTruncated);
    }
    if (IsAllZero(nonce) || !used_nonces.insert(nonce).second) {
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kNonceReuse);
    }
    auto opened =
        OpenChunk(*archive_key, encoded_header, chunk_index, nonce, sealed);
    if (!opened || opened->empty() ||
        opened->size() > kPayloadPlaintextChunkBytes) {
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kAuthenticationFailed);
    }
    plaintext_bytes += opened->size();
    if (plaintext_bytes > kMaxPayloadPlaintextBytes) {
      crypto::SecureZeroBuffer(*opened);
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kPayloadTooLarge);
    }
    if (plaintext_payload_destination &&
        !plaintext_payload_destination->WriteAtCurrentPosAndCheck(*opened)) {
      crypto::SecureZeroBuffer(*opened);
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kUnwritableDestination);
    }
    crypto::SecureZeroBuffer(*opened);
    section_remaining -= frame_bytes;
    ++chunk_index;
  }
  if (plaintext_payload_destination) {
    const bool flushed = plaintext_payload_destination->Flush();
    const int64_t destination_length =
        plaintext_payload_destination->GetLength();
    if (!flushed || destination_length < 0 ||
        static_cast<uint64_t>(destination_length) != plaintext_bytes) {
      return Failure<AibOpenResult>(plaintext_payload_destination,
                                    AibArchiveError::kUnwritableDestination);
    }
  }
  const int64_t final_source_position =
      source->Seek(base::File::FROM_CURRENT, 0);
  const int64_t final_source_length = source->GetLength();
  if (final_source_position < 0 || final_source_length < 0 ||
      static_cast<uint64_t>(final_source_position) !=
          *expected_archive_bytes ||
      static_cast<uint64_t>(final_source_length) != *expected_archive_bytes) {
    const AibArchiveError error =
        final_source_length >= 0 &&
                static_cast<uint64_t>(final_source_length) >
                    *expected_archive_bytes
            ? AibArchiveError::kTrailingData
            : AibArchiveError::kTruncated;
    return Failure<AibOpenResult>(plaintext_payload_destination, error);
  }
  wipe_manifest.Release();
  return AibOpenResult{.header = header,
                       .manifest = std::move(*manifest),
                       .payload_plaintext_bytes = plaintext_bytes};
}

}  // namespace taffy::storage::backup
