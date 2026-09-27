// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_memory_validation.h"

#include <algorithm>
#include <limits>
#include <string_view>

#include "base/strings/string_util.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

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

bool FitsSqlInt(uint64_t value) {
  return value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool IsValidWorkspace(const mojom::MemoryWorkspaceRecord& workspace) {
  return IsCanonicalId(workspace.workspace_id) &&
         IsBoundedText(workspace.display_name,
                       mojom::kMaxWorkspaceDisplayNameBytes);
}

bool HasExactGlobalRevisionStep(const mojom::StorageCommitEffect& body) {
  return FitsSqlInt(body.expected_revision) &&
         FitsSqlInt(body.resulting_revision) &&
         body.expected_revision != std::numeric_limits<uint64_t>::max() &&
         body.resulting_revision == body.expected_revision + 1u;
}

bool HasNoOtherBody(const mojom::StorageCommitEffect& body) {
  return !body.workspace && !body.install_skill && !body.skill_status &&
         !body.skill_run && !body.forget_skill && !body.source_deletion &&
         !body.assistant_configuration && !body.workspace_deletion &&
         !body.library_entry && !body.library_deletion &&
         body.task_id.empty() && body.transaction_batch.empty() &&
         storage_internal::IsNeutralTaskIdSeed(body.task_id_seed);
}

bool HasOnlyMemoryRecord(const mojom::StorageCommitEffect& body) {
  return body.operation_kind == mojom::StorageOperation::kUpsertMemory &&
         body.memory_record && !body.memory_deletion && HasNoOtherBody(body);
}

bool HasOnlyMemoryDeletion(const mojom::StorageCommitEffect& body) {
  return body.operation_kind == mojom::StorageOperation::kDeleteMemory &&
         body.memory_deletion && !body.memory_record && HasNoOtherBody(body);
}

bool HasValidSource(const mojom::MemoryRecord& record) {
  switch (record.source_kind) {
    case mojom::MemorySourceKind::kUserEntered:
      return !record.source_task_id && !record.source_workspace &&
             record.reviewed_at_epoch_ms == 0u;
    case mojom::MemorySourceKind::kAcceptedTaskSuggestion:
      return record.source_task_id &&
             IsBoundedText(*record.source_task_id,
                           mojom::kMaxIdentifierBytes) &&
             (!record.source_workspace ||
              IsValidWorkspace(*record.source_workspace)) &&
             record.reviewed_at_epoch_ms != 0u;
  }
  return false;
}

bool HasValidScope(const mojom::MemoryRecord& record) {
  switch (record.scope_kind) {
    case mojom::MemoryScopeKind::kAllTasks:
      return !record.scope_workspace;
    case mojom::MemoryScopeKind::kWorkspace:
      return record.scope_workspace &&
             IsValidWorkspace(*record.scope_workspace);
  }
  return false;
}

}  // namespace

bool IsValidMemoryRecord(const mojom::MemoryRecord& record,
                         uint64_t expected_record_revision) {
  if (!FitsSqlInt(expected_record_revision) ||
      expected_record_revision == std::numeric_limits<uint64_t>::max() ||
      !IsCanonicalId(record.memory_id) ||
      record.revision != expected_record_revision + 1u ||
      !FitsSqlInt(record.revision) ||
      !IsBoundedText(record.statement, mojom::kMaxMemoryStatementBytes) ||
      !HasValidSource(record) || !HasValidScope(record) ||
      !FitsSqlInt(record.created_at_epoch_ms) ||
      !FitsSqlInt(record.updated_at_epoch_ms) ||
      !FitsSqlInt(record.reviewed_at_epoch_ms) ||
      !FitsSqlInt(record.expires_at_epoch_ms) ||
      record.updated_at_epoch_ms < record.created_at_epoch_ms ||
      (record.reviewed_at_epoch_ms != 0u &&
       (record.reviewed_at_epoch_ms < record.created_at_epoch_ms ||
        record.reviewed_at_epoch_ms > record.updated_at_epoch_ms)) ||
      (record.expires_at_epoch_ms != 0u &&
       record.expires_at_epoch_ms <= record.updated_at_epoch_ms)) {
    return false;
  }
  switch (record.sensitivity) {
    case mojom::MemorySensitivity::kStandard:
    case mojom::MemorySensitivity::kSensitive:
      return true;
  }
  return false;
}

bool IsValidMemoryStorageCommitBody(const mojom::StorageCommitEffect& body) {
  if (!HasExactGlobalRevisionStep(body)) {
    return false;
  }
  switch (body.operation_kind) {
    case mojom::StorageOperation::kUpsertMemory:
      return HasOnlyMemoryRecord(body) && body.memory_record->record &&
             IsValidMemoryRecord(*body.memory_record->record,
                                 body.memory_record->expected_record_revision);
    case mojom::StorageOperation::kDeleteMemory:
      return HasOnlyMemoryDeletion(body) &&
             IsCanonicalId(body.memory_deletion->memory_id) &&
             FitsSqlInt(body.memory_deletion->expected_record_revision) &&
             FitsSqlInt(body.memory_deletion->resulting_record_revision) &&
             FitsSqlInt(body.memory_deletion->deleted_at_epoch_ms) &&
             body.memory_deletion->expected_record_revision !=
                 std::numeric_limits<uint64_t>::max() &&
             body.memory_deletion->resulting_record_revision ==
                 body.memory_deletion->expected_record_revision + 1u;
    default:
      return false;
  }
}

}  // namespace taffy
