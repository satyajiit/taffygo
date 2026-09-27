// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_BROWSER_TARGET_WRITERS_H_
#define TAFFY_BROWSER_BACKUP_BROWSER_TARGET_WRITERS_H_

#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/types/expected.h"
#include "taffy/components/storage/browser/backup_browser_record_codec.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"

class PrefService;

namespace bookmarks {
class BookmarkModel;
}

namespace taffy {

// This is an in-memory owner result, not a durable restore result. Once a
// mutation is attempted, an unverified final model is unknown: callers must
// retain recovery custody and must not retry or invent rollback.
enum class BackupBrowserTargetWriteResult {
  kAppliedAndReadBack,
  kRefusedBeforeMutation,
  kOutcomeUnknown,
};

using BackupBookmarkTargetReadback =
    base::expected<std::vector<storage::backup::BackupBookmarkRecord>,
                   storage::backup::BackupSnapshotError>;
using BackupBrowserPreferenceTargetReadback =
    base::expected<storage::backup::BackupBrowserPreferenceRecord,
                   storage::backup::BackupSnapshotError>;

// Typed browser-owner seams for an already-created dormant target. Neither
// interface constructs a profile, admits archive bytes, starts Core, makes the
// target visible, drains a store, or claims cross-owner atomicity.
class BackupBookmarkTargetWriter {
 public:
  BackupBookmarkTargetWriter() = default;
  BackupBookmarkTargetWriter(const BackupBookmarkTargetWriter&) = delete;
  BackupBookmarkTargetWriter& operator=(const BackupBookmarkTargetWriter&) =
      delete;
  virtual ~BackupBookmarkTargetWriter() = default;

  virtual BackupBookmarkTargetReadback ReadBack() const = 0;
  virtual BackupBrowserTargetWriteResult WriteAndReadBack(
      base::span<const storage::backup::BackupBookmarkRecord> records) = 0;
};

class BackupBrowserPreferenceTargetWriter {
 public:
  BackupBrowserPreferenceTargetWriter() = default;
  BackupBrowserPreferenceTargetWriter(
      const BackupBrowserPreferenceTargetWriter&) = delete;
  BackupBrowserPreferenceTargetWriter& operator=(
      const BackupBrowserPreferenceTargetWriter&) = delete;
  virtual ~BackupBrowserPreferenceTargetWriter() = default;

  virtual BackupBrowserPreferenceTargetReadback ReadBack() const = 0;
  virtual BackupBrowserTargetWriteResult WriteAndReadBack(
      const storage::backup::BackupBrowserPreferenceRecord& record) = 0;
};

// Pinned-Chromium adapters. Construction grants no authority: WriteAndReadBack
// still refuses unless the supplied typed owner is pristine and local.
class ChromiumBackupBookmarkTargetWriter final
    : public BackupBookmarkTargetWriter {
 public:
  explicit ChromiumBackupBookmarkTargetWriter(
      bookmarks::BookmarkModel* model);
  ~ChromiumBackupBookmarkTargetWriter() override;

  BackupBookmarkTargetReadback ReadBack() const override;
  BackupBrowserTargetWriteResult WriteAndReadBack(
      base::span<const storage::backup::BackupBookmarkRecord> records)
      override;

 private:
  raw_ptr<bookmarks::BookmarkModel> model_;
};

class ChromiumBackupBrowserPreferenceTargetWriter final
    : public BackupBrowserPreferenceTargetWriter {
 public:
  explicit ChromiumBackupBrowserPreferenceTargetWriter(
      PrefService* preferences);
  ~ChromiumBackupBrowserPreferenceTargetWriter() override;

  BackupBrowserPreferenceTargetReadback ReadBack() const override;
  BackupBrowserTargetWriteResult WriteAndReadBack(
      const storage::backup::BackupBrowserPreferenceRecord& record) override;

 private:
  raw_ptr<PrefService> preferences_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_BROWSER_TARGET_WRITERS_H_
