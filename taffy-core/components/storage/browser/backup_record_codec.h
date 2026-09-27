// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECORD_CODEC_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECORD_CODEC_H_

#include <stdint.h>

#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {

enum class BackupRecordCodecError {
  kInvalidRecord,
  kMalformedPayload,
  kDescriptorMismatch,
  kPayloadTooLarge,
};

inline constexpr uint32_t kLibraryBackupSchemaVersion = 1;
inline constexpr uint32_t kMemoryBackupSchemaVersion = 1;
inline constexpr uint32_t kAssistantConfigurationBackupSchemaVersion = 1;
inline constexpr uint32_t kSavedWorkspaceBackupSchemaVersion = 1;
inline constexpr uint32_t kSkillBackupSchemaVersion = 1;
inline constexpr char kAssistantConfigurationBackupStableId[] =
    "assistant-configuration";

using EncodedBackupRecord =
    base::expected<std::vector<uint8_t>, BackupRecordCodecError>;

// Explicit version-1 plaintext projections for the encrypted payload only.
// The fixed little-endian format includes a kind/version prefix and the exact
// record identity/revision, with bounded UTF-8 and closed enums. No generated
// Mojo serialization, SQL row, effect identity or whole bootstrap is copied.
// Decoders consume the complete input and bind it to the authenticated manifest
// descriptor. Tombstones have zero bytes and never enter these active codecs.
EncodedBackupRecord EncodeLibraryRecordV1(
    const core_service::mojom::LibraryEntryRecord& record);
base::expected<core_service::mojom::LibraryEntryRecordPtr,
               BackupRecordCodecError>
DecodeLibraryRecordV1(base::span<const uint8_t> bytes,
                      std::string_view expected_stable_id,
                      uint64_t expected_revision);

EncodedBackupRecord EncodeMemoryRecordV1(
    const core_service::mojom::MemoryRecord& record);
base::expected<core_service::mojom::MemoryRecordPtr, BackupRecordCodecError>
DecodeMemoryRecordV1(base::span<const uint8_t> bytes,
                     std::string_view expected_stable_id,
                     uint64_t expected_revision);

EncodedBackupRecord EncodeAssistantConfigurationV1(
    const core_service::mojom::AssistantConfiguration& record);
base::expected<core_service::mojom::AssistantConfigurationPtr,
               BackupRecordCodecError>
DecodeAssistantConfigurationV1(base::span<const uint8_t> bytes,
                               uint64_t expected_revision);

// A workspace payload wraps one field-by-field validated TAFFYWS snapshot and
// no SQL metadata. The encoder admits only a snapshot the person explicitly
// saved. Deletion markers are descriptor-only and do not enter this codec.
EncodedBackupRecord EncodeSavedWorkspaceRecordV1(
    const core_service::mojom::WorkspaceRestoreRecord& record);
base::expected<core_service::mojom::WorkspaceRestoreRecordPtr,
               BackupRecordCodecError>
DecodeSavedWorkspaceRecordV1(base::span<const uint8_t> bytes,
                             std::string_view expected_stable_id,
                             uint64_t expected_revision);

// Authored skills and learned procedures are one durable record family. The
// kind must agree exactly with provenance. This wrapper carries the current
// installed definition/status only: superseded definitions and run history
// are deliberately not archive records. The portable procedure owner remains
// responsible for full definition decoding and policy validation before the
// source Core may authorize committing the staged candidate.
EncodedBackupRecord EncodeSkillRecordV1(
    const core_service::mojom::SkillRecord& record,
    core_service::mojom::BackupRecordKind kind);
// The same canonical identity rule is used by the payload codec and by the
// browser's descriptor-only export boundary. This grants no procedure
// authority.
bool IsCanonicalBackupSkillId(std::string_view value);
base::expected<core_service::mojom::SkillRecordPtr, BackupRecordCodecError>
DecodeSkillRecordV1(base::span<const uint8_t> bytes,
                    core_service::mojom::BackupRecordKind expected_kind,
                    std::string_view expected_stable_id,
                    uint64_t expected_revision);

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECORD_CODEC_H_
