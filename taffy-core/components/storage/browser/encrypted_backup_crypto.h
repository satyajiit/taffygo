// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_ENCRYPTED_BACKUP_CRYPTO_H_
#define TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_ENCRYPTED_BACKUP_CRYPTO_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "base/containers/span.h"

namespace taffy::storage::backup {

inline constexpr size_t kSecretBytes = 32;
inline constexpr size_t kArchiveIdBytes = 16;
inline constexpr size_t kNonceBytes = 12;
inline constexpr size_t kAuthenticationTagBytes = 16;
// The exact public `.aib` prefix is authenticated by every seal. Keeping the
// binding width here lets the crypto adapter reject a caller that accidentally
// authenticates only part of the header, while the archive layer owns the
// fields and their encoding.
inline constexpr size_t kArchiveHeaderBindingBytes = 56;

using Secret = std::array<uint8_t, kSecretBytes>;
using ArchiveId = std::array<uint8_t, kArchiveIdBytes>;
using Nonce = std::array<uint8_t, kNonceBytes>;

// These values come from Chromium's cryptographically secure random source.
Secret GenerateSecret();
ArchiveId GenerateArchiveId();
Nonce GenerateNonce();

// Wraps one per-archive content key under a user-held recovery key. The
// archive identity is the HKDF salt. The exact public header is authenticated
// beside the fixed format context, so neither the wrapped key nor its section
// boundaries can be transplanted or rewritten.
std::optional<std::vector<uint8_t>> WrapArchiveKey(
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> archive_id,
    base::span<const uint8_t> archive_header,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> archive_key);

std::optional<Secret> UnwrapArchiveKey(
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> archive_id,
    base::span<const uint8_t> archive_header,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> wrapped_archive_key);

// Seals one manifest or payload chunk. The exact public header and zero-based
// chunk index are authenticated, preventing header rewriting, reordering, and
// cross-archive reuse.
std::optional<std::vector<uint8_t>> SealChunk(
    base::span<const uint8_t> archive_key,
    base::span<const uint8_t> archive_header,
    uint32_t chunk_index,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> plaintext);

std::optional<std::vector<uint8_t>> OpenChunk(
    base::span<const uint8_t> archive_key,
    base::span<const uint8_t> archive_header,
    uint32_t chunk_index,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> ciphertext);

}  // namespace taffy::storage::backup

#endif  // TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_ENCRYPTED_BACKUP_CRYPTO_H_
