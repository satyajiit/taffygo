// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <utility>

#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"
#include "url/gurl.h"

namespace taffy::storage::backup {
namespace {

constexpr size_t kMaxTitleBytes = 4096;
constexpr size_t kMaxUrlBytes = 65536;

bool CanonicalUuid(std::string_view value) {
  const auto uuid = base::Uuid::ParseLowercase(value);
  return uuid.is_valid() && uuid.AsLowercaseString() == value;
}

bool Valid(const BackupBookmarkRecord& record) {
  const bool root = record.parent_id == kBookmarkBarBackupRoot ||
                    record.parent_id == kOtherBookmarksBackupRoot ||
                    record.parent_id == kMobileBookmarksBackupRoot;
  if (!CanonicalUuid(record.stable_id) ||
      (!root && !CanonicalUuid(record.parent_id)) ||
      record.parent_id == record.stable_id ||
      record.position >= core_service::mojom::kMaxBackupRecords ||
      record.title.size() > kMaxTitleBytes ||
      !base::IsStringUTF8(record.title) ||
      record.title.find('\0') != std::string::npos ||
      record.date_added_windows_us >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      record.date_folder_modified_windows_us >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  if (!record.url) {
    return true;
  }
  if (record.url->size() > kMaxUrlBytes ||
      record.date_folder_modified_windows_us != 0) {
    return false;
  }
  const GURL url(*record.url);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password() && url.spec() == *record.url;
}

}  // namespace

EncodedBackupRecord EncodeBookmarkRecordV1(const BackupBookmarkRecord& record) {
  if (!Valid(record)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  codec_internal::RecordWriter writer(codec_internal::RecordTag::kBookmark);
  writer.String(record.stable_id);
  writer.U64(kBrowserBackupSnapshotRevision);
  writer.String(record.parent_id);
  writer.U32(record.position);
  writer.String(record.title);
  writer.OptionalString(record.url);
  writer.U64(record.date_added_windows_us);
  writer.U64(record.date_folder_modified_windows_us);
  return std::move(writer).Finish();
}

base::expected<BackupBookmarkRecord, BackupRecordCodecError>
DecodeBookmarkRecordV1(base::span<const uint8_t> bytes,
                       std::string_view expected_stable_id,
                       uint64_t expected_revision) {
  codec_internal::RecordReader reader(bytes,
                                      codec_internal::RecordTag::kBookmark);
  BackupBookmarkRecord record;
  uint64_t revision = 0;
  if (!reader.String(36, &record.stable_id) || !reader.U64(&revision) ||
      !reader.String(36, &record.parent_id) || !reader.U32(&record.position) ||
      !reader.String(kMaxTitleBytes, &record.title) ||
      !reader.OptionalString(kMaxUrlBytes, &record.url) ||
      !reader.U64(&record.date_added_windows_us) ||
      !reader.U64(&record.date_folder_modified_windows_us) ||
      !reader.Complete() || !Valid(record) ||
      revision != kBrowserBackupSnapshotRevision) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  if (record.stable_id != expected_stable_id || revision != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  return record;
}

}  // namespace taffy::storage::backup
