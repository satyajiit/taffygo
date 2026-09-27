// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator_internal.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/components/storage/browser/backup_record_codec.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr size_t kHashBufferBytes = 64u * 1024u;

template <typename Range>
bool AllZero(const Range& bytes) {
  return std::ranges::all_of(bytes, [](uint8_t byte) { return byte == 0u; });
}

bool CanonicalRecordId(std::string_view id) {
  return id.size() == 32u && std::ranges::all_of(id, [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsSelected(core_mojom::BackupRecordKind kind,
                base::span<const core_mojom::BackupRecordKind> selection) {
  return std::ranges::find(selection, kind) != selection.end();
}

bool IsSupportedDescriptor(
    const core_mojom::BackupRecordDescriptor& descriptor) {
  if (descriptor.schema_version != 1u || descriptor.revision == 0u ||
      descriptor.revision >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      descriptor.stable_id.empty() ||
      descriptor.stable_id.size() > core_mojom::kMaxBackupIdBytes ||
      descriptor.plaintext_sha256.size() != crypto::hash::kSha256Size) {
    return false;
  }
  switch (descriptor.kind) {
    case core_mojom::BackupRecordKind::kAssistantConfiguration:
      return descriptor.stable_id ==
             storage::backup::kAssistantConfigurationBackupStableId;
    case core_mojom::BackupRecordKind::kLibraryEntry:
    case core_mojom::BackupRecordKind::kMemoryRecord:
    case core_mojom::BackupRecordKind::kSavedWorkspace:
      return CanonicalRecordId(descriptor.stable_id);
    case core_mojom::BackupRecordKind::kUserAuthoredSkill:
    case core_mojom::BackupRecordKind::kLearnedProcedure:
      return storage::backup::IsCanonicalBackupSkillId(descriptor.stable_id) &&
             descriptor.revision <= core_mojom::kMaxSkillVersionsPerSkill;
    case core_mojom::BackupRecordKind::kBookmark:
    case core_mojom::BackupRecordKind::kBrowserPreference:
    default:
      return false;
  }
}

ProfileBackupError MapStageError(storage::backup::BackupStageError error) {
  switch (error) {
    case storage::backup::BackupStageError::kAlreadyExists:
    case storage::backup::BackupStageError::kCapacityExceeded:
      return ProfileBackupError::kBusy;
    case storage::backup::BackupStageError::kIoFailure:
      return ProfileBackupError::kIoFailure;
    case storage::backup::BackupStageError::kInvalidArgument:
      return ProfileBackupError::kInvalidArgument;
    case storage::backup::BackupStageError::kArchiveRefused:
    default:
      return ProfileBackupError::kPlanRefused;
  }
}

}  // namespace

SensitiveBackupSecret::SensitiveBackupSecret() = default;
SensitiveBackupSecret::SensitiveBackupSecret(storage::backup::Secret source)
    : bytes(source) {
  crypto::SecureZeroBuffer(source);
}
SensitiveBackupSecret::SensitiveBackupSecret(SensitiveBackupSecret&& other)
    : bytes(other.bytes) {
  crypto::SecureZeroBuffer(other.bytes);
}
SensitiveBackupSecret& SensitiveBackupSecret::operator=(
    SensitiveBackupSecret&& other) {
  if (this != &other) {
    crypto::SecureZeroBuffer(bytes);
    bytes = other.bytes;
    crypto::SecureZeroBuffer(other.bytes);
  }
  return *this;
}
SensitiveBackupSecret::~SensitiveBackupSecret() {
  crypto::SecureZeroBuffer(bytes);
}

ValidatedBackupSnapshot::ValidatedBackupSnapshot() = default;
ValidatedBackupSnapshot::ValidatedBackupSnapshot(ValidatedBackupSnapshot&&) =
    default;
ValidatedBackupSnapshot& ValidatedBackupSnapshot::operator=(
    ValidatedBackupSnapshot&&) = default;
ValidatedBackupSnapshot::~ValidatedBackupSnapshot() = default;

VerifiedBackupImportPayload::VerifiedBackupImportPayload() = default;
VerifiedBackupImportPayload::VerifiedBackupImportPayload(
    VerifiedBackupImportPayload&&) = default;
VerifiedBackupImportPayload& VerifiedBackupImportPayload::operator=(
    VerifiedBackupImportPayload&&) = default;
VerifiedBackupImportPayload::~VerifiedBackupImportPayload() = default;

HashedBackupImport::HashedBackupImport() = default;
HashedBackupImport::HashedBackupImport(HashedBackupImport&&) = default;
HashedBackupImport& HashedBackupImport::operator=(HashedBackupImport&&) =
    default;
HashedBackupImport::~HashedBackupImport() = default;

bool IsValidProfileBackupOperationId(std::string_view operation_id) {
  return !operation_id.empty() &&
         operation_id.size() <= core_mojom::kMaxOperationIdBytes &&
         base::IsStringUTF8(operation_id) &&
         std::ranges::none_of(operation_id, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

core_mojom::OperationEnvelopePtr NewBackupOperation(
    uint64_t service_generation,
    std::string_view kind,
    std::string_view public_operation_id) {
  constexpr uint64_t kTimeoutMillis = 30'000u;
  if (!IsValidProfileBackupOperationId(kind) ||
      !IsValidProfileBackupOperationId(public_operation_id)) {
    return nullptr;
  }
  const uint64_t now = BackupPlanningNowMonotonicMillis();
  const base::Uuid operation_nonce = base::Uuid::GenerateRandomV4();
  const base::Uuid idempotency_nonce = base::Uuid::GenerateRandomV4();
  if (service_generation == 0u || !operation_nonce.is_valid() ||
      !idempotency_nonce.is_valid() ||
      now > std::numeric_limits<uint64_t>::max() - kTimeoutMillis) {
    return nullptr;
  }
  auto operation = core_mojom::OperationEnvelope::New(
      std::string(kind) + "-" + operation_nonce.AsLowercaseString(),
      service_generation, 0u, now + kTimeoutMillis,
      std::string(kind) + "-" + idempotency_nonce.AsLowercaseString() + "-" +
          std::string(public_operation_id));
  if (!IsLiveBackupOperation(operation.get(), service_generation, now)) {
    return nullptr;
  }
  return operation;
}

bool IsSupportedBackupManifestSelection(
    base::span<const core_mojom::BackupRecordKind> selection) {
  return storage::backup::IsSupportedBackupStorageSelection(selection);
}

base::expected<ValidatedBackupSnapshot, ProfileBackupError>
ValidateBackupSnapshot(
    storage::backup::BackupSnapshotResult snapshot,
    base::span<const core_mojom::BackupRecordKind> selection) {
  if (!snapshot) {
    switch (snapshot.error()) {
      case storage::backup::BackupSnapshotError::kUnsupportedSelection:
        return base::unexpected(ProfileBackupError::kInvalidArgument);
      case storage::backup::BackupSnapshotError::kUnavailable:
        return base::unexpected(ProfileBackupError::kStorageUnavailable);
      case storage::backup::BackupSnapshotError::kInvalidRecord:
        return base::unexpected(ProfileBackupError::kSnapshotMismatch);
      case storage::backup::BackupSnapshotError::kCapacityExceeded:
      default:
        return base::unexpected(ProfileBackupError::kPlanRefused);
    }
  }
  if (!IsSupportedBackupManifestSelection(selection) ||
      snapshot->size() > core_mojom::kMaxBackupRecords) {
    return base::unexpected(ProfileBackupError::kInvalidArgument);
  }

  ValidatedBackupSnapshot output;
  output.records = std::move(*snapshot);
  output.descriptors.reserve(output.records.size());
  std::set<std::pair<core_mojom::BackupRecordKind, std::string>> identities;
  std::set<std::string> procedure_identities;
  for (const auto& record : output.records) {
    const core_mojom::BackupRecordDescriptor* descriptor =
        record.descriptor.get();
    if (!descriptor || !IsSelected(descriptor->kind, selection) ||
        !IsSupportedDescriptor(*descriptor) ||
        !identities.emplace(descriptor->kind, descriptor->stable_id).second ||
        descriptor->plaintext_bytes > core_mojom::kMaxBackupRecordBytes ||
        descriptor->plaintext_bytes > core_mojom::kMaxBackupPlaintextBytes -
                                          output.payload_plaintext_bytes) {
      return base::unexpected(ProfileBackupError::kSnapshotMismatch);
    }
    const bool zero_digest = AllZero(descriptor->plaintext_sha256);
    const bool procedure =
        descriptor->kind == core_mojom::BackupRecordKind::kUserAuthoredSkill ||
        descriptor->kind == core_mojom::BackupRecordKind::kLearnedProcedure;
    if (procedure &&
        (!procedure_identities.insert(descriptor->stable_id).second ||
         procedure_identities.size() > core_mojom::kMaxSkillsPerProfile)) {
      return base::unexpected(ProfileBackupError::kSnapshotMismatch);
    }
    switch (descriptor->state) {
      case core_mojom::BackupRecordState::kActive: {
        if (record.plaintext.empty() ||
            descriptor->plaintext_bytes != record.plaintext.size() ||
            zero_digest) {
          return base::unexpected(ProfileBackupError::kSnapshotMismatch);
        }
        const auto digest = crypto::hash::Sha256(record.plaintext);
        if (!std::ranges::equal(digest, descriptor->plaintext_sha256)) {
          return base::unexpected(ProfileBackupError::kSnapshotMismatch);
        }
        break;
      }
      case core_mojom::BackupRecordState::kTombstone:
        if (!record.plaintext.empty() || descriptor->plaintext_bytes != 0u ||
            !zero_digest || procedure ||
            descriptor->kind ==
                core_mojom::BackupRecordKind::kAssistantConfiguration) {
          return base::unexpected(ProfileBackupError::kSnapshotMismatch);
        }
        break;
      default:
        return base::unexpected(ProfileBackupError::kSnapshotMismatch);
    }
    output.payload_plaintext_bytes += descriptor->plaintext_bytes;
    output.descriptors.push_back(descriptor->Clone());
  }
  return output;
}

base::expected<PreparedProfileBackupExport, ProfileBackupError>
SealValidatedBackupSnapshot(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id,
    SensitiveBackupSecret recovery_key,
    std::vector<uint8_t> manifest_plaintext,
    std::array<uint8_t, 32> snapshot_sha256,
    std::vector<uint32_t> source_order,
    uint64_t expected_payload_plaintext_bytes,
    ValidatedBackupSnapshot snapshot) {
  base::ScopedClosureRunner wipe(base::BindOnce(
      [](storage::backup::Secret* key, std::vector<uint8_t>* manifest) {
        crypto::SecureZeroBuffer(*key);
        crypto::SecureZeroBuffer(*manifest);
      },
      base::Unretained(&recovery_key.bytes),
      base::Unretained(&manifest_plaintext)));
  if (!stage_store || !IsValidProfileBackupOperationId(operation_id) ||
      manifest_plaintext.empty() || AllZero(snapshot_sha256) ||
      expected_payload_plaintext_bytes != snapshot.payload_plaintext_bytes ||
      source_order.size() != snapshot.records.size()) {
    return base::unexpected(ProfileBackupError::kPlanRefused);
  }
  std::vector<bool> seen(source_order.size());
  for (uint32_t ordinal : source_order) {
    if (ordinal >= seen.size() || seen[ordinal]) {
      return base::unexpected(ProfileBackupError::kPlanRefused);
    }
    seen[ordinal] = true;
  }

  base::File payload = stage_store->CreatePlaintextScratchFile();
  if (!payload.IsValid()) {
    return base::unexpected(ProfileBackupError::kIoFailure);
  }
  uint64_t written = 0u;
  for (uint32_t ordinal : source_order) {
    const auto& plaintext = snapshot.records[ordinal].plaintext;
    if (!plaintext.empty() && !payload.WriteAtCurrentPosAndCheck(plaintext)) {
      return base::unexpected(ProfileBackupError::kIoFailure);
    }
    written += plaintext.size();
  }
  if (written != expected_payload_plaintext_bytes || !payload.Flush() ||
      payload.GetLength() != static_cast<int64_t>(written) ||
      payload.Seek(base::File::FROM_BEGIN, 0) != 0) {
    return base::unexpected(ProfileBackupError::kIoFailure);
  }
  auto prepared =
      stage_store->PrepareExport(operation_id, std::move(payload), written,
                                 recovery_key.bytes, manifest_plaintext);
  if (!prepared) {
    return base::unexpected(MapStageError(prepared.error()));
  }
  return PreparedProfileBackupExport{
      .archive_bytes = prepared->archive_bytes,
      .snapshot_sha256 = snapshot_sha256,
  };
}

base::expected<VerifiedBackupImportPayload, ProfileBackupError>
ReadVerifiedBackupImport(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id) {
  if (!stage_store || !IsValidProfileBackupOperationId(operation_id)) {
    return base::unexpected(ProfileBackupError::kInvalidArgument);
  }
  std::optional<storage::backup::VerifiedBackupImport> verified =
      stage_store->ReadVerifiedImport(operation_id);
  if (!verified) {
    return base::unexpected(ProfileBackupError::kPlanRefused);
  }
  VerifiedBackupImportPayload output;
  output.verified = std::move(*verified);
  return output;
}

base::expected<HashedBackupImport, ProfileBackupError> HashBackupImportPayload(
    VerifiedBackupImportPayload imported,
    const core_mojom::BackupManifestInspectResult& inspection) {
  base::File& payload = imported.verified.plaintext_payload;
  if (inspection.status != core_mojom::BackupPlanningStatus::kSucceeded ||
      !IsSupportedBackupManifestSelection(inspection.selection) ||
      inspection.record_count != inspection.records.size() ||
      inspection.records.size() > core_mojom::kMaxBackupRecords ||
      inspection.payload_plaintext_bytes !=
          imported.verified.payload_plaintext_bytes ||
      inspection.payload_plaintext_bytes >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      !payload.IsValid() ||
      payload.GetLength() !=
          static_cast<int64_t>(inspection.payload_plaintext_bytes) ||
      payload.Seek(base::File::FROM_BEGIN, 0) != 0) {
    return base::unexpected(ProfileBackupError::kSnapshotMismatch);
  }

  HashedBackupImport output;
  output.staged_records.reserve(inspection.records.size());
  std::array<uint8_t, kHashBufferBytes> buffer{};
  uint64_t total = 0u;
  for (const auto& layout : inspection.records) {
    if (!layout ||
        layout->plaintext_bytes > core_mojom::kMaxBackupRecordBytes ||
        layout->plaintext_bytes >
            core_mojom::kMaxBackupPlaintextBytes - total) {
      return base::unexpected(ProfileBackupError::kPlanRefused);
    }
    const bool active = layout->state == core_mojom::BackupRecordState::kActive;
    if ((!active &&
         layout->state != core_mojom::BackupRecordState::kTombstone) ||
        (active && layout->plaintext_bytes == 0u) ||
        (!active && layout->plaintext_bytes != 0u)) {
      return base::unexpected(ProfileBackupError::kPlanRefused);
    }
    auto staged = core_mojom::StagedBackupRecord::New();
    staged->plaintext_bytes = layout->plaintext_bytes;
    staged->plaintext_sha256.assign(crypto::hash::kSha256Size, 0u);
    if (active) {
      crypto::hash::Hasher hasher(crypto::hash::kSha256);
      uint64_t remaining = layout->plaintext_bytes;
      while (remaining != 0u) {
        const size_t chunk =
            static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        const std::optional<size_t> read =
            payload.ReadAtCurrentPos(base::span(buffer).first(chunk));
        if (!read || *read != chunk) {
          return base::unexpected(ProfileBackupError::kSnapshotMismatch);
        }
        hasher.Update(base::span(buffer).first(chunk));
        remaining -= chunk;
      }
      hasher.Finish(staged->plaintext_sha256);
      if (AllZero(staged->plaintext_sha256)) {
        return base::unexpected(ProfileBackupError::kSnapshotMismatch);
      }
    }
    total += layout->plaintext_bytes;
    output.staged_records.push_back(std::move(staged));
  }
  std::array<uint8_t, 1> trailing{};
  const std::optional<size_t> trailing_read =
      payload.ReadAtCurrentPos(trailing);
  if (total != inspection.payload_plaintext_bytes || !trailing_read ||
      *trailing_read != 0u ||
      payload.GetLength() != static_cast<int64_t>(total)) {
    return base::unexpected(ProfileBackupError::kSnapshotMismatch);
  }
  output.verified = std::move(imported.verified);
  return output;
}

}  // namespace taffy
