// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_ENCRYPTED_BACKUP_ARCHIVE_H_
#define TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_ENCRYPTED_BACKUP_ARCHIVE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/types/expected.h"
#include "taffy/components/storage/browser/encrypted_backup_crypto.h"

namespace taffy::storage::backup {

inline constexpr std::array<uint8_t, 8> kAibMagic = {'T', 'A', 'F', 'F',
                                                     'Y', 'A', 'I', 'B'};
inline constexpr uint16_t kAibFormatVersion = 1;
inline constexpr uint16_t kAibCipherSuiteAes256GcmHkdfSha256 = 1;
inline constexpr size_t kAibPublicHeaderBytes = 56;
static_assert(kAibPublicHeaderBytes == kArchiveHeaderBindingBytes);
inline constexpr size_t kWrappedArchiveKeyBytes =
    kSecretBytes + kAuthenticationTagBytes;
inline constexpr size_t kPayloadPlaintextChunkBytes = 1024 * 1024;
inline constexpr uint64_t kMaxManifestPlaintextBytes = 64 * 1024 * 1024;
inline constexpr uint64_t kMaxPayloadPlaintextBytes =
    uint64_t{8} * 1024 * 1024 * 1024;
inline constexpr uint64_t kMaxEncryptedSectionsBytes =
    uint64_t{9} * 1024 * 1024 * 1024;
inline constexpr uint64_t kMaxAibArchiveBytes =
    kAibPublicHeaderBytes + kWrappedArchiveKeyBytes +
    kMaxEncryptedSectionsBytes;

enum class AibArchiveError : uint8_t {
  kInvalidArgument,
  kUnreadableSource,
  kUnwritableDestination,
  kWrongMagic,
  kUnsupportedFormat,
  kUnsupportedCipherSuite,
  kMalformedHeader,
  kManifestTooLarge,
  kPayloadTooLarge,
  kArchiveTooLarge,
  kTruncated,
  kTrailingData,
  kNonceReuse,
  kAuthenticationFailed,
};

// The only content-free metadata visible to a document provider. Section byte
// counts include their nonce and authentication tag.
struct AibPublicHeader {
  ArchiveId archive_id{};
  Nonce key_wrap_nonce{};
  uint64_t encrypted_manifest_bytes = 0;
  uint64_t encrypted_payload_bytes = 0;

  bool operator==(const AibPublicHeader&) const = default;
};

struct AibWriteResult {
  AibPublicHeader header;
  uint64_t archive_bytes = 0;
};

struct AibOpenResult {
  AibPublicHeader header;
  std::vector<uint8_t> manifest;
  uint64_t payload_plaintext_bytes = 0;
};

using AibHeaderResult = base::expected<AibPublicHeader, AibArchiveError>;
using AibWriteArchiveResult = base::expected<AibWriteResult, AibArchiveError>;
using AibOpenArchiveResult = base::expected<AibOpenResult, AibArchiveError>;

// Serializes and parses the fixed little-endian public prefix. Parsing rejects
// unsupported versions, invalid lengths, and zero random values before any
// encrypted bytes are allocated.
std::array<uint8_t, kAibPublicHeaderBytes> EncodeAibPublicHeader(
    const AibPublicHeader& header);
AibHeaderResult DecodeAibPublicHeader(base::span<const uint8_t> encoded_header);
AibHeaderResult InspectAibArchive(base::File* source);

// Writes one immutable archive. The payload is read from offset zero in fixed
// plaintext chunks, so the peak allocation is independent of profile size.
// On any failure the destination is truncated and never represents success.
AibWriteArchiveResult WriteAibArchive(
    base::File* destination,
    base::File* plaintext_payload,
    uint64_t payload_plaintext_bytes,
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> manifest_plaintext);

// Authenticates the wrapped key, manifest, and every payload chunk. When
// `plaintext_payload_destination` is null the opened payload is deliberately
// discarded, which is the readback-verification path after archive creation.
// A supplied destination is truncated on any failure.
AibOpenArchiveResult OpenAibArchive(base::File* source,
                                    base::File* plaintext_payload_destination,
                                    base::span<const uint8_t> recovery_key);

}  // namespace taffy::storage::backup

#endif  // TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_ENCRYPTED_BACKUP_ARCHIVE_H_
