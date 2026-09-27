// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_record_codec.h"

namespace taffy::storage::backup::restore_target_internal {
namespace {

namespace mojom = core_service::mojom;

constexpr std::string_view kWitnessDomain =
    "TaffyGo backup candidate descriptor witness";
constexpr std::string_view kMetadataDomain =
    "TaffyGo restored target local metadata";
constexpr uint32_t kEncodingVersion = 1u;

void Append(std::vector<uint8_t>* output, base::span<const uint8_t> bytes) {
  output->insert(output->end(), bytes.begin(), bytes.end());
}

void AppendU32(std::vector<uint8_t>* output, uint32_t value) {
  Append(output, base::U32ToLittleEndian(value));
}

void AppendU64(std::vector<uint8_t>* output, uint64_t value) {
  Append(output, base::U64ToLittleEndian(value));
}

void AppendString(std::vector<uint8_t>* output, std::string_view value) {
  AppendU32(output, static_cast<uint32_t>(value.size()));
  Append(output, base::as_byte_span(value));
}

std::optional<uint32_t> KindTag(mojom::BackupRecordKind kind) {
  if (kind == mojom::BackupRecordKind::kAssistantConfiguration) {
    return 0u;
  }
  if (kind == mojom::BackupRecordKind::kSavedWorkspace) {
    return 1u;
  }
  if (kind == mojom::BackupRecordKind::kLibraryEntry) {
    return 2u;
  }
  if (kind == mojom::BackupRecordKind::kMemoryRecord) {
    return 3u;
  }
  if (kind == mojom::BackupRecordKind::kUserAuthoredSkill) {
    return 4u;
  }
  if (kind == mojom::BackupRecordKind::kLearnedProcedure) {
    return 5u;
  }
  return std::nullopt;
}

std::optional<uint32_t> StateTag(mojom::BackupRecordState state) {
  if (state == mojom::BackupRecordState::kActive) {
    return 0u;
  }
  if (state == mojom::BackupRecordState::kTombstone) {
    return 1u;
  }
  return std::nullopt;
}

bool CanonicalRecordId(std::string_view id) {
  return id.size() == 32u && std::ranges::all_of(id, [](char digit) {
           return (digit >= '0' && digit <= '9') ||
                  (digit >= 'a' && digit <= 'f');
         });
}

bool CanonicalSkillId(std::string_view id) {
  return !id.empty() && id.size() <= mojom::kMaxSkillIdBytes &&
         std::ranges::all_of(id, [](char byte) {
           return (byte >= 'a' && byte <= 'z') ||
                  (byte >= '0' && byte <= '9') || byte == '-' || byte == '.';
         });
}

uint32_t SchemaVersion(mojom::BackupRecordKind kind) {
  if (kind == mojom::BackupRecordKind::kAssistantConfiguration) {
    return kAssistantConfigurationBackupSchemaVersion;
  }
  if (kind == mojom::BackupRecordKind::kSavedWorkspace) {
    return kSavedWorkspaceBackupSchemaVersion;
  }
  if (kind == mojom::BackupRecordKind::kLibraryEntry) {
    return kLibraryBackupSchemaVersion;
  }
  if (kind == mojom::BackupRecordKind::kMemoryRecord) {
    return kMemoryBackupSchemaVersion;
  }
  return kSkillBackupSchemaVersion;
}

bool CanonicalTargetId(std::string_view id) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(id);
  return parsed.is_valid() && parsed.AsLowercaseString() == id &&
         id.size() == 36u && id[14] == '4' &&
         (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');
}

bool NonzeroDigest(base::span<const uint8_t> digest) {
  return digest.size() == crypto::hash::kSha256Size &&
         std::ranges::any_of(digest, [](uint8_t byte) { return byte != 0u; });
}

bool ValidRecord(const BackupSnapshotRecord& record) {
  const mojom::BackupRecordDescriptor* descriptor = record.descriptor.get();
  if (!descriptor || descriptor->revision == 0u ||
      descriptor->revision >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      descriptor->plaintext_bytes > mojom::kMaxBackupRecordBytes ||
      descriptor->plaintext_sha256.size() != crypto::hash::kSha256Size ||
      !KindTag(descriptor->kind) || !StateTag(descriptor->state)) {
    return false;
  }
  const bool assistant =
      descriptor->kind == mojom::BackupRecordKind::kAssistantConfiguration;
  const bool skill =
      descriptor->kind == mojom::BackupRecordKind::kUserAuthoredSkill ||
      descriptor->kind == mojom::BackupRecordKind::kLearnedProcedure;
  if (descriptor->schema_version != SchemaVersion(descriptor->kind) ||
      (assistant
           ? descriptor->stable_id != kAssistantConfigurationBackupStableId
       : skill ? !CanonicalSkillId(descriptor->stable_id)
               : !CanonicalRecordId(descriptor->stable_id))) {
    return false;
  }
  if (descriptor->state == mojom::BackupRecordState::kActive) {
    return !record.plaintext.empty() &&
           descriptor->plaintext_bytes == record.plaintext.size() &&
           NonzeroDigest(descriptor->plaintext_sha256) &&
           std::ranges::equal(crypto::hash::Sha256(record.plaintext),
                              descriptor->plaintext_sha256);
  }
  return !assistant && !skill && descriptor->revision > 1u &&
         record.plaintext.empty() && descriptor->plaintext_bytes == 0u &&
         std::ranges::all_of(descriptor->plaintext_sha256,
                             [](uint8_t byte) { return byte == 0u; });
}

void AppendDescriptor(std::vector<uint8_t>* material,
                      const mojom::BackupRecordDescriptor& descriptor) {
  AppendU32(material, *KindTag(descriptor.kind));
  AppendString(material, descriptor.stable_id);
  AppendU64(material, descriptor.revision);
  AppendU32(material, descriptor.schema_version);
  AppendU32(material, *StateTag(descriptor.state));
  AppendU64(material, descriptor.plaintext_bytes);
  Append(material, descriptor.plaintext_sha256);
}

}  // namespace

mojom::BackupRestoreCandidateWitnessPtr BuildCandidateWitness(
    const std::vector<BackupSnapshotRecord>& records) {
  if (records.size() > mojom::kMaxBackupRecords) {
    return nullptr;
  }
  std::vector<const mojom::BackupRecordDescriptor*> descriptors;
  descriptors.reserve(records.size());
  for (const auto& record : records) {
    if (!ValidRecord(record)) {
      return nullptr;
    }
    descriptors.push_back(record.descriptor.get());
  }
  std::ranges::sort(descriptors, {}, [](const auto* descriptor) {
    return std::pair(*KindTag(descriptor->kind), descriptor->stable_id);
  });
  for (size_t index = 1u; index < descriptors.size(); ++index) {
    if (descriptors[index - 1u]->kind == descriptors[index]->kind &&
        descriptors[index - 1u]->stable_id == descriptors[index]->stable_id) {
      return nullptr;
    }
  }

  std::vector<mojom::BackupRecordKind> selection;
  for (const auto* descriptor : descriptors) {
    if (selection.empty() || selection.back() != descriptor->kind) {
      selection.push_back(descriptor->kind);
    }
  }

  std::vector<uint8_t> material;
  material.reserve(kWitnessDomain.size() + 16u + descriptors.size() * 96u);
  AppendString(&material, kWitnessDomain);
  AppendU32(&material, kEncodingVersion);
  AppendU32(&material, static_cast<uint32_t>(selection.size()));
  for (const auto kind : selection) {
    AppendU32(&material, *KindTag(kind));
  }
  AppendU64(&material, static_cast<uint64_t>(descriptors.size()));
  for (const auto* descriptor : descriptors) {
    AppendDescriptor(&material, *descriptor);
  }
  const auto digest = crypto::hash::Sha256(material);
  if (!NonzeroDigest(digest)) {
    return nullptr;
  }
  auto witness = mojom::BackupRestoreCandidateWitness::New();
  witness->selection = std::move(selection);
  witness->record_count = static_cast<uint64_t>(descriptors.size());
  witness->candidate_records_sha256.assign(digest.begin(), digest.end());
  return witness;
}

bool IsValidCandidateWitness(
    const mojom::BackupRestoreCandidateWitness& witness) {
  if (witness.record_count > mojom::kMaxBackupRecords ||
      witness.selection.size() > 6u ||
      witness.selection.size() > witness.record_count ||
      witness.selection.empty() != (witness.record_count == 0u) ||
      !NonzeroDigest(witness.candidate_records_sha256)) {
    return false;
  }
  std::optional<uint32_t> previous;
  for (const auto kind : witness.selection) {
    const auto tag = KindTag(kind);
    if (!tag || (previous && *tag <= *previous)) {
      return false;
    }
    previous = tag;
  }
  return true;
}

bool IsExactCandidateWitness(
    const mojom::BackupRestoreCandidateWitness& left,
    const mojom::BackupRestoreCandidateWitness& right) {
  return IsValidCandidateWitness(left) && IsValidCandidateWitness(right) &&
         left.selection == right.selection &&
         left.record_count == right.record_count &&
         left.candidate_records_sha256 == right.candidate_records_sha256;
}

std::string ImportedMetadataId(
    std::string_view target_profile_id,
    const mojom::BackupRestoreCandidateWitness& witness,
    ImportedMetadataRole role,
    mojom::BackupRecordKind kind,
    std::string_view stable_id) {
  const auto tag = KindTag(kind);
  const bool collection = role != ImportedMetadataRole::kRecord;
  if (!CanonicalTargetId(target_profile_id) ||
      !IsValidCandidateWitness(witness) || !tag ||
      (role != ImportedMetadataRole::kRecord &&
       role != ImportedMetadataRole::kLibraryCollection &&
       role != ImportedMetadataRole::kMemoryCollection) ||
      (collection != stable_id.empty()) ||
      (role == ImportedMetadataRole::kLibraryCollection &&
       kind != mojom::BackupRecordKind::kLibraryEntry) ||
      (role == ImportedMetadataRole::kMemoryCollection &&
       kind != mojom::BackupRecordKind::kMemoryRecord)) {
    return {};
  }
  std::vector<uint8_t> material;
  material.reserve(kMetadataDomain.size() + target_profile_id.size() +
                   stable_id.size() + 64u);
  AppendString(&material, kMetadataDomain);
  AppendU32(&material, kEncodingVersion);
  AppendString(&material, target_profile_id);
  Append(&material, witness.candidate_records_sha256);
  AppendU32(&material, static_cast<uint32_t>(role));
  AppendU32(&material, *tag);
  AppendString(&material, stable_id);
  return "backup-restore-v1-" +
         base::HexEncodeLower(crypto::hash::Sha256(material));
}

}  // namespace taffy::storage::backup::restore_target_internal
