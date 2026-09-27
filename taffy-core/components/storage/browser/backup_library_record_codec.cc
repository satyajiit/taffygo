// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"
#include "taffy/components/storage/browser/core_storage_library_validation.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using codec_internal::RecordReader;
using codec_internal::RecordTag;
using codec_internal::RecordWriter;

constexpr size_t kIdBytes = 32;
constexpr size_t kFieldBytes = 256;
constexpr size_t kSourceTitleBytes = 1024;
constexpr size_t kSourceHostBytes = 253;

bool ValidKind(mojom::LibraryFactKind kind) {
  switch (kind) {
    case mojom::LibraryFactKind::kFromPage:
    case mojom::LibraryFactKind::kSummarized:
    case mojom::LibraryFactKind::kTaffyInference:
    case mojom::LibraryFactKind::kUserEntered:
      return true;
  }
  return false;
}

}  // namespace

EncodedBackupRecord EncodeLibraryRecordV1(
    const mojom::LibraryEntryRecord& entry) {
  if (entry.revision == 0 || !ValidKind(entry.kind) ||
      !IsValidLibraryEntry(entry, entry.revision - 1)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  RecordWriter writer(RecordTag::kLibrary);
  writer.String(entry.entry_id);
  writer.U64(entry.revision);
  writer.String(entry.collection_id);
  writer.String(entry.collection_name);
  writer.String(entry.source_workspace_id);
  writer.U64(entry.source_workspace_revision);
  writer.String(entry.source_fact_id);
  writer.String(entry.field);
  writer.String(entry.original_value);
  writer.OptionalString(entry.correction);
  writer.U32(static_cast<uint32_t>(entry.kind));
  writer.U32(static_cast<uint32_t>(entry.sources.size()));
  for (const auto& source : entry.sources) {
    writer.String(source->source_id);
    writer.String(source->title);
    writer.String(source->host);
    writer.U64(source->observed_at_epoch_ms);
  }
  writer.U64(entry.captured_at_epoch_ms);
  writer.U64(entry.last_checked_epoch_ms);
  writer.Boolean(entry.has_conflict);
  return std::move(writer).Finish();
}

base::expected<mojom::LibraryEntryRecordPtr, BackupRecordCodecError>
DecodeLibraryRecordV1(base::span<const uint8_t> bytes,
                      std::string_view expected_stable_id,
                      uint64_t expected_revision) {
  RecordReader reader(bytes, RecordTag::kLibrary);
  auto entry = mojom::LibraryEntryRecord::New();
  uint32_t kind = 0;
  uint32_t source_count = 0;
  if (!reader.String(kIdBytes, &entry->entry_id) ||
      !reader.U64(&entry->revision) ||
      !reader.String(kIdBytes, &entry->collection_id) ||
      !reader.String(mojom::kMaxWorkspaceDisplayNameBytes,
                     &entry->collection_name) ||
      !reader.String(kIdBytes, &entry->source_workspace_id) ||
      !reader.U64(&entry->source_workspace_revision) ||
      !reader.String(kIdBytes, &entry->source_fact_id) ||
      !reader.String(kFieldBytes, &entry->field) ||
      !reader.String(mojom::kMaxWorkspaceValueBytes, &entry->original_value) ||
      !reader.OptionalString(mojom::kMaxWorkspaceValueBytes,
                             &entry->correction) ||
      !reader.U32(&kind) || kind > 3u || !reader.U32(&source_count) ||
      source_count == 0 || source_count > mojom::kMaxLibrarySources) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  entry->kind = static_cast<mojom::LibraryFactKind>(kind);
  for (uint32_t index = 0; index < source_count; ++index) {
    auto source = mojom::LibrarySourceRecord::New();
    if (!reader.String(kIdBytes, &source->source_id) ||
        !reader.String(kSourceTitleBytes, &source->title) ||
        !reader.String(kSourceHostBytes, &source->host) ||
        !reader.U64(&source->observed_at_epoch_ms)) {
      return base::unexpected(BackupRecordCodecError::kMalformedPayload);
    }
    entry->sources.push_back(std::move(source));
  }
  if (!reader.U64(&entry->captured_at_epoch_ms) ||
      !reader.U64(&entry->last_checked_epoch_ms) ||
      !reader.Boolean(&entry->has_conflict) || !reader.Complete()) {
    return base::unexpected(BackupRecordCodecError::kMalformedPayload);
  }
  if (entry->entry_id != expected_stable_id ||
      entry->revision != expected_revision) {
    return base::unexpected(BackupRecordCodecError::kDescriptorMismatch);
  }
  if (entry->revision == 0 || !ValidKind(entry->kind) ||
      !IsValidLibraryEntry(*entry, entry->revision - 1)) {
    return base::unexpected(BackupRecordCodecError::kInvalidRecord);
  }
  return entry;
}

}  // namespace taffy::storage::backup
