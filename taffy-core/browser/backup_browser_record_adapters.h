// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_BROWSER_RECORD_ADAPTERS_H_
#define TAFFY_BROWSER_BACKUP_BROWSER_RECORD_ADAPTERS_H_

#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"

class PrefService;
namespace bookmarks {
class BookmarkModel;
}

namespace taffy {

// UI-sequence reads through the existing store owners, not their on-disk JSON.
// These functions establish no profile/selection authority: the workflow must
// first authorize the exact regular source profile and selected record class.
// No SQL or cross-owner atomicity is claimed. Until the hidden restore owner
// coordinates all three stores, the coordinator still refuses these classes.
// A local bookmark snapshot is all-or-nothing, capped at 10,000 nodes, depth
// 128 and 32 MiB. Account-owned bookmarks and unsupported URLs refuse the
// selection; managed roots are never part of a person's local snapshot.
storage::backup::BackupSnapshotResult ReadBookmarkBackupRecords(
    const bookmarks::BookmarkModel* model);
storage::backup::BackupSnapshotResult ReadBrowserPreferenceBackupRecords(
    const PrefService* preferences);

// Native store admission complements the format's alpha-2 shape check with
// the platform's actual ISO country table. It does not modify preferences.
bool ValidateBrowserPreferenceBackupRecord(
    const storage::backup::BackupBrowserPreferenceRecord& record);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_BROWSER_RECORD_ADAPTERS_H_
