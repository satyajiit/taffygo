// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/encrypted_backup_crypto.h"

#include <algorithm>
#include <string_view>

#include "base/containers/span.h"
#include "crypto/aead.h"
#include "crypto/hash.h"
#include "crypto/kdf.h"
#include "crypto/random.h"
#include "crypto/secure_util.h"

namespace taffy::storage::backup {
namespace {

constexpr std::string_view kWrapKdfContext =
    "taffygo.aib.v1.archive-key-wrap.hkdf-sha256";
constexpr std::string_view kWrapAadContext =
    "taffygo.aib.v1.archive-key-wrap.aes-256-gcm";
constexpr std::string_view kChunkAadContext =
    "taffygo.aib.v1.chunk.aes-256-gcm";

bool HasSizes(base::span<const uint8_t> key,
              base::span<const uint8_t> archive_id,
              base::span<const uint8_t> archive_header,
              base::span<const uint8_t> nonce) {
  return key.size() == kSecretBytes && archive_id.size() == kArchiveIdBytes &&
         archive_header.size() == kArchiveHeaderBindingBytes &&
         nonce.size() == kNonceBytes;
}

std::vector<uint8_t> WrapAad(base::span<const uint8_t> archive_header) {
  std::vector<uint8_t> aad;
  aad.reserve(kWrapAadContext.size() + archive_header.size());
  aad.insert(aad.end(), kWrapAadContext.begin(), kWrapAadContext.end());
  aad.insert(aad.end(), archive_header.begin(), archive_header.end());
  return aad;
}

std::vector<uint8_t> ChunkAad(base::span<const uint8_t> archive_header,
                              uint32_t chunk_index) {
  std::vector<uint8_t> aad;
  aad.reserve(kChunkAadContext.size() + archive_header.size() +
              sizeof(chunk_index));
  aad.insert(aad.end(), kChunkAadContext.begin(), kChunkAadContext.end());
  aad.insert(aad.end(), archive_header.begin(), archive_header.end());
  for (int shift : {24, 16, 8, 0}) {
    aad.push_back(static_cast<uint8_t>(chunk_index >> shift));
  }
  return aad;
}

Secret DeriveWrappingKey(base::span<const uint8_t> recovery_key,
                         base::span<const uint8_t> archive_id) {
  return crypto::kdf::Hkdf<kSecretBytes>(crypto::hash::kSha256, recovery_key,
                                         archive_id,
                                         base::as_byte_span(kWrapKdfContext));
}

}  // namespace

Secret GenerateSecret() {
  return crypto::RandBytesAsArray<kSecretBytes>();
}

ArchiveId GenerateArchiveId() {
  return crypto::RandBytesAsArray<kArchiveIdBytes>();
}

Nonce GenerateNonce() {
  return crypto::RandBytesAsArray<kNonceBytes>();
}

std::optional<std::vector<uint8_t>> WrapArchiveKey(
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> archive_id,
    base::span<const uint8_t> archive_header,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> archive_key) {
  if (!HasSizes(recovery_key, archive_id, archive_header, nonce) ||
      archive_key.size() != kSecretBytes) {
    return std::nullopt;
  }
  Secret wrapping_key = DeriveWrappingKey(recovery_key, archive_id);
  const std::vector<uint8_t> aad = WrapAad(archive_header);
  std::vector<uint8_t> wrapped = crypto::aead::Seal(
      crypto::aead::AES_256_GCM, wrapping_key, archive_key, nonce, aad);
  crypto::SecureZeroBuffer(wrapping_key);
  return wrapped;
}

std::optional<Secret> UnwrapArchiveKey(
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> archive_id,
    base::span<const uint8_t> archive_header,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> wrapped_archive_key) {
  if (!HasSizes(recovery_key, archive_id, archive_header, nonce) ||
      wrapped_archive_key.size() != kSecretBytes + kAuthenticationTagBytes) {
    return std::nullopt;
  }
  Secret wrapping_key = DeriveWrappingKey(recovery_key, archive_id);
  const std::vector<uint8_t> aad = WrapAad(archive_header);
  std::optional<std::vector<uint8_t>> opened = crypto::aead::Open(
      crypto::aead::AES_256_GCM, wrapping_key, wrapped_archive_key, nonce, aad);
  crypto::SecureZeroBuffer(wrapping_key);
  if (!opened || opened->size() != kSecretBytes) {
    return std::nullopt;
  }
  Secret archive_key;
  std::ranges::copy(*opened, archive_key.begin());
  crypto::SecureZeroBuffer(*opened);
  return archive_key;
}

std::optional<std::vector<uint8_t>> SealChunk(
    base::span<const uint8_t> archive_key,
    base::span<const uint8_t> archive_header,
    uint32_t chunk_index,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> plaintext) {
  if (archive_key.size() != kSecretBytes ||
      archive_header.size() != kArchiveHeaderBindingBytes ||
      nonce.size() != kNonceBytes) {
    return std::nullopt;
  }
  const std::vector<uint8_t> aad = ChunkAad(archive_header, chunk_index);
  return crypto::aead::Seal(crypto::aead::AES_256_GCM, archive_key, plaintext,
                            nonce, aad);
}

std::optional<std::vector<uint8_t>> OpenChunk(
    base::span<const uint8_t> archive_key,
    base::span<const uint8_t> archive_header,
    uint32_t chunk_index,
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> ciphertext) {
  if (archive_key.size() != kSecretBytes ||
      archive_header.size() != kArchiveHeaderBindingBytes ||
      nonce.size() != kNonceBytes ||
      ciphertext.size() < kAuthenticationTagBytes) {
    return std::nullopt;
  }
  const std::vector<uint8_t> aad = ChunkAad(archive_header, chunk_index);
  return crypto::aead::Open(crypto::aead::AES_256_GCM, archive_key, ciphertext,
                            nonce, aad);
}

}  // namespace taffy::storage::backup
