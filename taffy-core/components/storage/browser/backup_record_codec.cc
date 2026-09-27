// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_record_codec.h"

#include <limits>
#include <utility>

#include "taffy/components/storage/browser/backup_record_codec_internal.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using codec_internal::RecordReader;
using codec_internal::RecordTag;
using codec_internal::RecordWriter;

bool ValidConfiguration(const mojom::AssistantConfiguration& record) {
  if (record.revision == 0 ||
      record.revision > uint64_t{std::numeric_limits<int64_t>::max()} ||
      record.disabled_abilities.size() > 16u || record.pace > 2u ||
      record.length > 2u || record.check_in > 2u) {
    return false;
  }
  switch (record.preset) {
    case mojom::PersonalityPreset::kCarefulResearcher:
    case mojom::PersonalityPreset::kQuickShopper:
    case mojom::PersonalityPreset::kTripPlanner:
      break;
    default:
      return false;
  }
  // V1 freezes the sixteen existing ability tags and their increasing order;
  // adding a product ability cannot silently alter an old archive's meaning.
  int32_t previous = -1;
  for (auto ability : record.disabled_abilities) {
    const int32_t value = static_cast<int32_t>(ability);
    if (value < 0 || value > 15 || value <= previous) {
      return false;
    }
    previous = value;
  }
  return true;
}

}  // namespace

EncodedBackupRecord EncodeAssistantConfigurationV1(
    const mojom::AssistantConfiguration& record) {
  if (!ValidConfiguration(record)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  RecordWriter writer(RecordTag::kAssistantConfiguration);
  writer.String(kAssistantConfigurationBackupStableId);
  writer.U64(record.revision);
  writer.U32(static_cast<uint32_t>(record.disabled_abilities.size()));
  for (auto ability : record.disabled_abilities) {
    writer.U32(static_cast<uint32_t>(ability));
  }
  writer.U32(static_cast<uint32_t>(record.preset));
  writer.U32(record.pace);
  writer.U32(record.length);
  writer.U32(record.check_in);
  return std::move(writer).Finish();
}

base::expected<mojom::AssistantConfigurationPtr, BackupRecordCodecError>
DecodeAssistantConfigurationV1(base::span<const uint8_t> bytes,
                               uint64_t expected_revision) {
  RecordReader reader(bytes, RecordTag::kAssistantConfiguration);
  auto record = mojom::AssistantConfiguration::New();
  std::string stable_id;
  uint32_t abilities = 0;
  if (!reader.String(sizeof(kAssistantConfigurationBackupStableId) - 1,
                     &stable_id) ||
      !reader.U64(&record->revision) || !reader.U32(&abilities) ||
      abilities > 16u) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  for (uint32_t index = 0; index < abilities; ++index) {
    uint32_t value = 0;
    if (!reader.U32(&value) || value > 15u) {
      return base::unexpected(BackupRecordCodecError::kMalformedPayload);
    }
    record->disabled_abilities.push_back(
        static_cast<mojom::AssistantAbility>(value));
  }
  uint32_t preset = 0;
  if (!reader.U32(&preset) || preset > 2u || !reader.U32(&record->pace) ||
      !reader.U32(&record->length) || !reader.U32(&record->check_in) ||
      !reader.Complete()) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  record->preset = static_cast<mojom::PersonalityPreset>(preset);
  if (stable_id != kAssistantConfigurationBackupStableId ||
      record->revision != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  if (!ValidConfiguration(*record)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  return record;
}

}  // namespace taffy::storage::backup
