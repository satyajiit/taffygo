// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <utility>

#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using codec_internal::RecordReader;
using codec_internal::RecordTag;
using codec_internal::RecordWriter;

bool ValidSavedWorkspace(const mojom::WorkspaceRestoreRecord& record) {
  if (record.revision == 0u ||
      record.revision >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      record.snapshot.empty() ||
      record.snapshot.size() > mojom::kMaxWorkspaceSnapshotBytes) {
    return false;
  }
  WorkspaceSnapshotShape shape;
  return DecodeWorkspaceSnapshotShape(record.snapshot, record.workspace_id,
                                      record.revision, &shape) &&
         shape.saved;
}

}  // namespace

EncodedBackupRecord EncodeSavedWorkspaceRecordV1(
    const mojom::WorkspaceRestoreRecord& record) {
  if (!ValidSavedWorkspace(record)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  RecordWriter writer(RecordTag::kSavedWorkspace);
  writer.String(record.workspace_id);
  writer.U64(record.revision);
  writer.Bytes(record.snapshot);
  return std::move(writer).Finish();
}

base::expected<mojom::WorkspaceRestoreRecordPtr, BackupRecordCodecError>
DecodeSavedWorkspaceRecordV1(base::span<const uint8_t> bytes,
                             std::string_view expected_stable_id,
                             uint64_t expected_revision) {
  RecordReader reader(bytes, RecordTag::kSavedWorkspace);
  auto record = mojom::WorkspaceRestoreRecord::New();
  if (!reader.String(mojom::kMaxIdentifierBytes, &record->workspace_id) ||
      !reader.U64(&record->revision) ||
      !reader.Bytes(mojom::kMaxWorkspaceSnapshotBytes, &record->snapshot) ||
      !reader.Complete()) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  if (record->workspace_id != expected_stable_id ||
      record->revision != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  if (!ValidSavedWorkspace(*record)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  return record;
}

}  // namespace taffy::storage::backup
