// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"

namespace taffy::storage::backup {
namespace {

bool Valid(const BackupBrowserPreferenceRecord& record) {
  return (record.theme == "SYSTEM" || record.theme == "LIGHT" ||
          record.theme == "DARK") &&
         (record.app_language == "SYSTEM" || record.app_language == "ENGLISH" ||
          record.app_language == "HINDI") &&
         record.region_code.size() == 2 && record.region_code[0] >= 'A' &&
         record.region_code[0] <= 'Z' && record.region_code[1] >= 'A' &&
         record.region_code[1] <= 'Z';
}

}  // namespace

EncodedBackupRecord EncodeBrowserPreferenceRecordV1(
    const BackupBrowserPreferenceRecord& record) {
  if (!Valid(record)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  codec_internal::RecordWriter writer(
      codec_internal::RecordTag::kBrowserPreference);
  writer.String(kBrowserPreferenceBackupStableId);
  writer.U64(kBrowserBackupSnapshotRevision);
  writer.String(record.theme);
  writer.String(record.app_language);
  writer.String(record.region_code);
  writer.Boolean(record.force_dark_web);
  return std::move(writer).Finish();
}

base::expected<BackupBrowserPreferenceRecord, BackupRecordCodecError>
DecodeBrowserPreferenceRecordV1(base::span<const uint8_t> bytes,
                                std::string_view expected_stable_id,
                                uint64_t expected_revision) {
  codec_internal::RecordReader reader(
      bytes, codec_internal::RecordTag::kBrowserPreference);
  std::string id;
  uint64_t revision = 0;
  BackupBrowserPreferenceRecord record;
  if (!reader.String(32, &id) || !reader.U64(&revision) ||
      !reader.String(6, &record.theme) ||
      !reader.String(7, &record.app_language) ||
      !reader.String(2, &record.region_code) ||
      !reader.Boolean(&record.force_dark_web) || !reader.Complete() ||
      id != kBrowserPreferenceBackupStableId ||
      revision != kBrowserBackupSnapshotRevision || !Valid(record)) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  if (id != expected_stable_id || revision != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  return record;
}

}  // namespace taffy::storage::backup
