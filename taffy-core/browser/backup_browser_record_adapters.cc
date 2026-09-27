// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "crypto/hash.h"
#include "taffy/browser/backup_browser_record_adapters_internal.h"

namespace taffy::backup_browser_internal {

base::expected<storage::backup::BackupSnapshotRecord,
               storage::backup::BackupSnapshotError>
MakeSnapshotRecord(core_service::mojom::BackupRecordKind kind,
                   std::string stable_id,
                   storage::backup::EncodedBackupRecord encoded) {
  namespace mojom = core_service::mojom;
  using namespace storage::backup;
  if (!encoded || (kind != mojom::BackupRecordKind::kBookmark &&
                   kind != mojom::BackupRecordKind::kBrowserPreference)) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  BackupSnapshotRecord record;
  record.plaintext = std::move(*encoded);
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = kBrowserBackupSnapshotRevision;
  record.descriptor->schema_version =
      kind == mojom::BackupRecordKind::kBookmark
          ? kBookmarkBackupSchemaVersion
          : kBrowserPreferenceBackupSchemaVersion;
  record.descriptor->state = mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  return record;
}

}  // namespace taffy::backup_browser_internal
