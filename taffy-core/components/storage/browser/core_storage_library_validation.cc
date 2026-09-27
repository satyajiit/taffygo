// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_library_validation.h"

#include <algorithm>
#include <limits>
#include <string_view>

#include "base/strings/string_util.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr size_t kMaxLibraryFieldBytes = 256u;
constexpr size_t kMaxLibrarySourceTitleBytes = 1024u;
constexpr size_t kMaxLibrarySourceHostBytes = 253u;

bool IsCanonicalId(std::string_view value) {
  return value.size() == 32u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsBoundedText(std::string_view value, size_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) && value.front() != ' ' &&
         value.back() != ' ' &&
         std::none_of(value.begin(), value.end(), [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

bool IsDisplayHost(std::string_view value) {
  return IsBoundedText(value, kMaxLibrarySourceHostBytes) &&
         std::none_of(value.begin(), value.end(), [](unsigned char character) {
           return character > 0x7fu || character == '/' || character == '?' ||
                  character == '#' || character == '@';
         });
}

bool FitsSqlInt(uint64_t value) {
  return value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool HasOnlyLibraryEntry(const mojom::StorageCommitEffect& body) {
  return body.operation_kind == mojom::StorageOperation::kUpsertLibraryEntry &&
         body.library_entry && !body.library_deletion && !body.workspace &&
         !body.install_skill && !body.skill_status && !body.skill_run &&
         !body.forget_skill && !body.source_deletion &&
         !body.assistant_configuration && !body.workspace_deletion &&
         body.task_id.empty() && body.transaction_batch.empty() &&
         storage_internal::IsNeutralTaskIdSeed(body.task_id_seed);
}

bool HasOnlyLibraryDeletion(const mojom::StorageCommitEffect& body) {
  return body.operation_kind == mojom::StorageOperation::kRemoveLibraryEntry &&
         body.library_deletion && !body.library_entry && !body.workspace &&
         !body.install_skill && !body.skill_status && !body.skill_run &&
         !body.forget_skill && !body.source_deletion &&
         !body.assistant_configuration && !body.workspace_deletion &&
         body.task_id.empty() && body.transaction_batch.empty() &&
         storage_internal::IsNeutralTaskIdSeed(body.task_id_seed);
}

bool HasExactGlobalRevisionStep(const mojom::StorageCommitEffect& body) {
  return FitsSqlInt(body.expected_revision) &&
         FitsSqlInt(body.resulting_revision) &&
         body.expected_revision != std::numeric_limits<uint64_t>::max() &&
         body.resulting_revision == body.expected_revision + 1u;
}

}  // namespace

bool IsValidLibraryEntry(const mojom::LibraryEntryRecord& entry,
                         uint64_t expected_entry_revision) {
  if (!FitsSqlInt(expected_entry_revision) ||
      expected_entry_revision == std::numeric_limits<uint64_t>::max() ||
      !IsCanonicalId(entry.entry_id) || !IsCanonicalId(entry.collection_id) ||
      !IsCanonicalId(entry.source_workspace_id) ||
      !IsCanonicalId(entry.source_fact_id) ||
      !IsBoundedText(entry.collection_name,
                     mojom::kMaxWorkspaceDisplayNameBytes) ||
      !IsBoundedText(entry.field, kMaxLibraryFieldBytes) ||
      !IsBoundedText(entry.original_value, mojom::kMaxWorkspaceValueBytes) ||
      (entry.correction &&
       !IsBoundedText(*entry.correction, mojom::kMaxWorkspaceValueBytes)) ||
      entry.source_workspace_revision == 0u ||
      !FitsSqlInt(entry.source_workspace_revision) ||
      entry.revision != expected_entry_revision + 1u ||
      !FitsSqlInt(entry.revision) || entry.sources.empty() ||
      entry.sources.size() > mojom::kMaxLibrarySources ||
      !FitsSqlInt(entry.captured_at_epoch_ms) ||
      !FitsSqlInt(entry.last_checked_epoch_ms) ||
      entry.captured_at_epoch_ms < entry.last_checked_epoch_ms) {
    return false;
  }
  std::string_view previous_id;
  uint64_t newest_observation = 0u;
  for (const auto& source : entry.sources) {
    if (!source || !IsCanonicalId(source->source_id) ||
        (!previous_id.empty() && previous_id >= source->source_id) ||
        !IsBoundedText(source->title, kMaxLibrarySourceTitleBytes) ||
        !IsDisplayHost(source->host) ||
        !FitsSqlInt(source->observed_at_epoch_ms)) {
      return false;
    }
    previous_id = source->source_id;
    newest_observation =
        std::max(newest_observation, source->observed_at_epoch_ms);
  }
  return newest_observation == entry.last_checked_epoch_ms;
}

bool IsValidLibraryStorageCommitBody(const mojom::StorageCommitEffect& body) {
  if (!HasExactGlobalRevisionStep(body)) {
    return false;
  }
  switch (body.operation_kind) {
    case mojom::StorageOperation::kUpsertLibraryEntry:
      return HasOnlyLibraryEntry(body) && body.library_entry->entry &&
             IsValidLibraryEntry(*body.library_entry->entry,
                                 body.library_entry->expected_entry_revision);
    case mojom::StorageOperation::kRemoveLibraryEntry:
      return HasOnlyLibraryDeletion(body) &&
             IsCanonicalId(body.library_deletion->entry_id) &&
             FitsSqlInt(body.library_deletion->expected_entry_revision) &&
             FitsSqlInt(body.library_deletion->resulting_entry_revision) &&
             FitsSqlInt(body.library_deletion->removed_at_epoch_ms) &&
             body.library_deletion->expected_entry_revision !=
                 std::numeric_limits<uint64_t>::max() &&
             body.library_deletion->resulting_entry_revision ==
                 body.library_deletion->expected_entry_revision + 1u;
    default:
      return false;
  }
}

}  // namespace taffy
