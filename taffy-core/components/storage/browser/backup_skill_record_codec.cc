// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"
#include "taffy/components/storage/browser/core_storage_skill_shape.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using codec_internal::RecordReader;
using codec_internal::RecordTag;
using codec_internal::RecordWriter;

constexpr std::array<uint8_t, 4> kDefinitionMagic = {'T', 'F', 'S', 'K'};

std::optional<RecordTag> TagForKind(mojom::BackupRecordKind kind) {
  switch (kind) {
    case mojom::BackupRecordKind::kUserAuthoredSkill:
      return RecordTag::kUserAuthoredSkill;
    case mojom::BackupRecordKind::kLearnedProcedure:
      return RecordTag::kLearnedProcedure;
    default:
      return std::nullopt;
  }
}

std::optional<mojom::SkillProvenance> ProvenanceForKind(
    mojom::BackupRecordKind kind) {
  switch (kind) {
    case mojom::BackupRecordKind::kUserAuthoredSkill:
      return mojom::SkillProvenance::kAuthored;
    case mojom::BackupRecordKind::kLearnedProcedure:
      return mojom::SkillProvenance::kRecordedFromTask;
    default:
      return std::nullopt;
  }
}

std::string_view StatusLabel(mojom::SkillStatus status) {
  switch (status) {
    case mojom::SkillStatus::kDraft:
      return "draft";
    case mojom::SkillStatus::kActive:
      return "active";
    case mojom::SkillStatus::kSuperseded:
      return "superseded";
    case mojom::SkillStatus::kRetired:
      return "retired";
    case mojom::SkillStatus::kDisabled:
      return "disabled";
    default:
      return {};
  }
}

bool IsDefinitionStatus(std::string_view value) {
  return std::ranges::any_of(
      std::array{mojom::SkillStatus::kDraft, mojom::SkillStatus::kActive,
                 mojom::SkillStatus::kSuperseded, mojom::SkillStatus::kRetired,
                 mojom::SkillStatus::kDisabled},
      [value](mojom::SkillStatus status) { return value == StatusLabel(status); });
}

std::string_view ProvenanceLabel(mojom::SkillProvenance provenance) {
  switch (provenance) {
    case mojom::SkillProvenance::kAuthored:
      return "authored";
    case mojom::SkillProvenance::kRecordedFromTask:
      return "recorded_from_task";
    case mojom::SkillProvenance::kInstalledFromPack:
      return "installed_from_pack";
    default:
      return {};
  }
}

bool CanonicalOrigin(std::string_view value) {
  if (!skill_internal::IsOrigin(std::string(value))) {
    return false;
  }
  const GURL url(value);
  const url::Origin origin = url::Origin::Create(url);
  return url.is_valid() && !origin.opaque() && origin.Serialize() == value;
}

class DefinitionEnvelopeReader {
 public:
  explicit DefinitionEnvelopeReader(base::span<const uint8_t> bytes)
      : remaining_(bytes) {}

  bool U8(uint8_t* value) {
    const auto bytes = Take(1u);
    if (!bytes) {
      return false;
    }
    *value = (*bytes)[0];
    return true;
  }

  bool U16(uint16_t* value) {
    const auto bytes = Take(2u);
    if (!bytes) {
      return false;
    }
    *value = static_cast<uint16_t>((*bytes)[0]) << 8u |
             static_cast<uint16_t>((*bytes)[1]);
    return true;
  }

  bool U32(uint32_t* value) {
    const auto bytes = Take(4u);
    if (!bytes) {
      return false;
    }
    *value = 0u;
    for (uint32_t index = 0u; index < 4u; ++index) {
      *value = (*value << 8u) | (*bytes)[index];
    }
    return true;
  }

  bool Text(std::string_view* value) {
    uint16_t length = 0u;
    if (!U16(&length)) {
      return false;
    }
    const auto bytes = Take(length);
    if (!bytes || !base::IsStringUTF8(base::as_string_view(*bytes))) {
      return false;
    }
    *value = base::as_string_view(*bytes);
    return true;
  }

  bool HasRemaining() const { return !remaining_.empty(); }

 private:
  std::optional<base::span<const uint8_t>> Take(size_t count) {
    if (count > remaining_.size()) {
      return std::nullopt;
    }
    const auto value = remaining_.first(count);
    remaining_ = remaining_.subspan(count);
    return value;
  }

  // A span held as a class member must be a raw_span: it carries the same
  // bounds and additionally the temporal safety of raw_ptr. It converts to
  // span implicitly, so `Take` still hands out a plain span.
  base::raw_span<const uint8_t> remaining_;
};

bool ReadTaskAssociation(DefinitionEnvelopeReader& reader,
                         mojom::SkillProvenance provenance) {
  uint8_t present = 0u;
  if (!reader.U8(&present)) {
    return false;
  }
  if (present == 0u) {
    return true;
  }
  std::string_view task;
  return present == 1u &&
         provenance == mojom::SkillProvenance::kRecordedFromTask &&
         reader.Text(&task) && !task.empty() &&
         task.size() <= mojom::kMaxIdentifierBytes &&
         std::ranges::all_of(task, [](char byte) {
           return base::IsAsciiAlphaNumeric(byte) || byte == '_' ||
                  byte == '-' || byte == '.' || byte == ':';
         });
}

bool DefinitionEnvelopeMatches(const mojom::SkillRecord& record) {
  DefinitionEnvelopeReader reader(record.definition);
  const auto magic =
      record.definition.size() >= kDefinitionMagic.size()
          ? base::span(record.definition).first<kDefinitionMagic.size()>()
          : base::span<const uint8_t>();
  if (!std::ranges::equal(magic, kDefinitionMagic)) {
    return false;
  }
  reader = DefinitionEnvelopeReader(
      base::span(record.definition).subspan(kDefinitionMagic.size()));
  uint16_t format = 0u;
  uint16_t steps = 0u;
  uint32_t version = 0u;
  std::string_view id;
  std::string_view status;
  std::string_view provenance;
  std::string_view origin;
  // The definition preserves the status at installation. The separately
  // committed row owns its current lifecycle; enabling must not rewrite the
  // reviewed bytes. Both status vocabularies still have to be valid.
  // This checks only the native envelope. Rust admits the complete definition
  // before any staged record can acquire restore commit authority.
  return reader.U16(&format) && (format == 1u || format == 2u) &&
         reader.U16(&steps) && steps == record.step_count && reader.Text(&id) &&
         id == record.skill_id && reader.U32(&version) &&
         version == record.active_version && reader.Text(&status) &&
         IsDefinitionStatus(status) && reader.Text(&provenance) &&
         provenance == ProvenanceLabel(record.provenance) &&
         (format == 1u || ReadTaskAssociation(reader, record.provenance)) &&
         reader.Text(&origin) && origin == record.origin &&
         reader.HasRemaining();
}

bool ValidSkill(const mojom::SkillRecord& record,
                mojom::BackupRecordKind kind) {
  const auto provenance = ProvenanceForKind(kind);
  return provenance && record.provenance == *provenance &&
         !StatusLabel(record.status).empty() &&
         !ProvenanceLabel(record.provenance).empty() &&
         IsCanonicalBackupSkillId(record.skill_id) &&
         CanonicalOrigin(record.origin) &&
         skill_internal::IsVersion(record.active_version) &&
         skill_internal::IsDefinition(record.definition) &&
         skill_internal::IsStepCount(record.step_count) &&
         skill_internal::IsTimestamp(record.installed_at_utc_ms) &&
         skill_internal::IsTimestamp(record.updated_at_utc_ms) &&
         DefinitionEnvelopeMatches(record);
}

}  // namespace

bool IsCanonicalBackupSkillId(std::string_view value) {
  return skill_internal::IsSkillIdentifier(std::string(value)) &&
         std::ranges::all_of(value, [](char byte) {
           return (byte >= 'a' && byte <= 'z') ||
                  (byte >= '0' && byte <= '9') || byte == '-' || byte == '.';
         });
}

EncodedBackupRecord EncodeSkillRecordV1(const mojom::SkillRecord& record,
                                        mojom::BackupRecordKind kind) {
  const auto tag = TagForKind(kind);
  if (!tag || !ValidSkill(record, kind)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  RecordWriter writer(*tag);
  writer.String(record.skill_id);
  writer.U64(record.active_version);
  writer.String(record.origin);
  writer.U32(static_cast<uint32_t>(record.provenance));
  writer.U32(static_cast<uint32_t>(record.status));
  writer.Bytes(record.definition);
  writer.U32(record.step_count);
  writer.U64(record.installed_at_utc_ms);
  writer.U64(record.updated_at_utc_ms);
  return std::move(writer).Finish();
}

base::expected<mojom::SkillRecordPtr, BackupRecordCodecError>
DecodeSkillRecordV1(base::span<const uint8_t> bytes,
                    mojom::BackupRecordKind expected_kind,
                    std::string_view expected_stable_id,
                    uint64_t expected_revision) {
  const auto tag = TagForKind(expected_kind);
  if (!tag) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  RecordReader reader(bytes, *tag);
  auto record = mojom::SkillRecord::New();
  uint64_t version = 0u;
  uint32_t provenance = 0u;
  uint32_t status = 0u;
  if (!reader.String(mojom::kMaxSkillIdBytes, &record->skill_id) ||
      !reader.U64(&version) || version > std::numeric_limits<uint32_t>::max() ||
      !reader.String(mojom::kMaxNormalizedOriginBytes, &record->origin) ||
      !reader.U32(&provenance) || provenance > 2u || !reader.U32(&status) ||
      status > 4u ||
      !reader.Bytes(mojom::kMaxSkillDefinitionBytes, &record->definition) ||
      !reader.U32(&record->step_count) ||
      !reader.U64(&record->installed_at_utc_ms) ||
      !reader.U64(&record->updated_at_utc_ms) || !reader.Complete()) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  record->active_version = static_cast<uint32_t>(version);
  record->provenance = static_cast<mojom::SkillProvenance>(provenance);
  record->status = static_cast<mojom::SkillStatus>(status);
  if (record->skill_id != expected_stable_id ||
      record->active_version != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  if (!ValidSkill(*record, expected_kind)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  return record;
}

}  // namespace taffy::storage::backup
