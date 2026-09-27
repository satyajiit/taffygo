// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"
#include "taffy/components/storage/browser/core_storage_memory_validation.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using codec_internal::RecordReader;
using codec_internal::RecordTag;
using codec_internal::RecordWriter;

constexpr size_t kIdBytes = 32;

void WriteWorkspace(RecordWriter* writer,
                    const mojom::MemoryWorkspaceRecordPtr& workspace) {
  writer->Boolean(static_cast<bool>(workspace));
  if (workspace) {
    writer->String(workspace->workspace_id);
    writer->String(workspace->display_name);
  }
}

bool ReadWorkspace(RecordReader* reader,
                   mojom::MemoryWorkspaceRecordPtr* workspace) {
  bool present = false;
  if (!reader->Boolean(&present)) {
    return false;
  }
  if (!present) {
    workspace->reset();
    return true;
  }
  auto record = mojom::MemoryWorkspaceRecord::New();
  if (!reader->String(kIdBytes, &record->workspace_id) ||
      !reader->String(mojom::kMaxWorkspaceDisplayNameBytes,
                      &record->display_name)) {
    return false;
  }
  *workspace = std::move(record);
  return true;
}

}  // namespace

EncodedBackupRecord EncodeMemoryRecordV1(const mojom::MemoryRecord& record) {
  if (record.revision == 0 ||
      !IsValidMemoryRecord(record, record.revision - 1)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  RecordWriter writer(RecordTag::kMemory);
  writer.String(record.memory_id);
  writer.U64(record.revision);
  writer.String(record.statement);
  writer.U32(static_cast<uint32_t>(record.source_kind));
  writer.OptionalString(record.source_task_id);
  WriteWorkspace(&writer, record.source_workspace);
  writer.U32(static_cast<uint32_t>(record.scope_kind));
  WriteWorkspace(&writer, record.scope_workspace);
  writer.U32(static_cast<uint32_t>(record.sensitivity));
  writer.U64(record.created_at_epoch_ms);
  writer.U64(record.updated_at_epoch_ms);
  writer.U64(record.reviewed_at_epoch_ms);
  writer.U64(record.expires_at_epoch_ms);
  return std::move(writer).Finish();
}

base::expected<mojom::MemoryRecordPtr, BackupRecordCodecError>
DecodeMemoryRecordV1(base::span<const uint8_t> bytes,
                     std::string_view expected_stable_id,
                     uint64_t expected_revision) {
  RecordReader reader(bytes, RecordTag::kMemory);
  auto record = mojom::MemoryRecord::New();
  uint32_t source_kind = 0;
  uint32_t scope_kind = 0;
  uint32_t sensitivity = 0;
  if (!reader.String(kIdBytes, &record->memory_id) ||
      !reader.U64(&record->revision) ||
      !reader.String(mojom::kMaxMemoryStatementBytes, &record->statement) ||
      !reader.U32(&source_kind) || source_kind > 1u ||
      !reader.OptionalString(mojom::kMaxIdentifierBytes,
                             &record->source_task_id) ||
      !ReadWorkspace(&reader, &record->source_workspace) ||
      !reader.U32(&scope_kind) || scope_kind > 1u ||
      !ReadWorkspace(&reader, &record->scope_workspace) ||
      !reader.U32(&sensitivity) || sensitivity > 1u ||
      !reader.U64(&record->created_at_epoch_ms) ||
      !reader.U64(&record->updated_at_epoch_ms) ||
      !reader.U64(&record->reviewed_at_epoch_ms) ||
      !reader.U64(&record->expires_at_epoch_ms) || !reader.Complete()) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  record->source_kind = static_cast<mojom::MemorySourceKind>(source_kind);
  record->scope_kind = static_cast<mojom::MemoryScopeKind>(scope_kind);
  record->sensitivity = static_cast<mojom::MemorySensitivity>(sensitivity);
  if (record->memory_id != expected_stable_id ||
      record->revision != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  if (record->revision == 0 ||
      !IsValidMemoryRecord(*record, record->revision - 1)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  return record;
}

}  // namespace taffy::storage::backup
