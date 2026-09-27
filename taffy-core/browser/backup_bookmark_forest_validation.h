// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_BOOKMARK_FOREST_VALIDATION_H_
#define TAFFY_BROWSER_BACKUP_BOOKMARK_FOREST_VALIDATION_H_

#include "base/containers/span.h"
#include "taffy/components/storage/browser/backup_browser_record_codec.h"

namespace taffy {

// Validates one complete local bookmark projection without modifying a
// BookmarkModel. Input order is irrelevant; each parent's positions must form
// exactly [0, child_count). An empty projection is a valid empty forest.
//
// This browser-owned check complements the portable single-record codec with
// Chromium's permanent and banned UUID rules, then proves that every record is
// reachable from exactly one of the three supported local roots. It grants no
// restore authority and does not make bookmark backup selectable.
bool ValidateBackupBookmarkForest(
    base::span<const storage::backup::BackupBookmarkRecord> records);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_BOOKMARK_FOREST_VALIDATION_H_
