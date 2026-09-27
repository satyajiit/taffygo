// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_BROWSER_RECORD_CODEC_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_BROWSER_RECORD_CODEC_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "taffy/components/storage/browser/backup_record_codec.h"

namespace taffy::storage::backup {

inline constexpr uint32_t kBookmarkBackupSchemaVersion = 1;
inline constexpr uint32_t kBrowserPreferenceBackupSchemaVersion = 1;
inline constexpr char kBrowserPreferenceBackupStableId[] =
    "browser-presentation";
inline constexpr char kBookmarkBarBackupRoot[] = "bookmark-bar";
inline constexpr char kOtherBookmarksBackupRoot[] = "other-bookmarks";
inline constexpr char kMobileBookmarksBackupRoot[] = "mobile-bookmarks";

// A snapshot revision, not a fabricated mutation clock. Chromium's typed
// bookmark/preference owners expose no durable per-record revision. Version 1
// therefore admits revision 1 only; a changed digest at the same revision is
// a conflict, never evidence that one side is newer. No tombstones are
// invented.
inline constexpr uint64_t kBrowserBackupSnapshotRevision = 1;

// No Chromium integer IDs, account/sync metadata, favicon, last-visited time,
// arbitrary node metadata or executable/local URLs enter this projection.
// Parent is a local permanent-root token or another record's canonical UUID.
// Tree completeness, unique sibling positions and cycles must be checked over
// the complete selection; this single-record decoder does not establish them.
struct BackupBookmarkRecord {
  std::string stable_id;
  std::string parent_id;
  uint32_t position = 0;
  std::string title;
  std::optional<std::string> url;
  uint64_t date_added_windows_us = 0;
  uint64_t date_folder_modified_windows_us = 0;

  bool operator==(const BackupBookmarkRecord&) const = default;
};

// Exactly four presentation choices, never a dictionary of preference names.
// Consent, network/model routing, site exceptions, history, account/provider
// material and handles are deliberately unrepresentable. Region has a bounded
// canonical alpha-2 shape here; the browser owner checks platform membership.
struct BackupBrowserPreferenceRecord {
  std::string theme;
  std::string app_language;
  std::string region_code;
  bool force_dark_web = false;

  bool operator==(const BackupBrowserPreferenceRecord&) const = default;
};

EncodedBackupRecord EncodeBookmarkRecordV1(const BackupBookmarkRecord& record);
base::expected<BackupBookmarkRecord, BackupRecordCodecError>
DecodeBookmarkRecordV1(base::span<const uint8_t> bytes,
                       std::string_view expected_stable_id,
                       uint64_t expected_revision);

EncodedBackupRecord EncodeBrowserPreferenceRecordV1(
    const BackupBrowserPreferenceRecord& record);
base::expected<BackupBrowserPreferenceRecord, BackupRecordCodecError>
DecodeBrowserPreferenceRecordV1(base::span<const uint8_t> bytes,
                                std::string_view expected_stable_id,
                                uint64_t expected_revision);

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_BROWSER_RECORD_CODEC_H_
