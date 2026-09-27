// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/test/recovery/core_api_status_reader.h"

namespace taffy::test::internal {

std::optional<ObservedMemoryStatus> CoreStatusWireReader::ReadMemory() {
  const std::optional<api::MemoryAvailability> availability =
      ReadClosedEnum(api::MemoryAvailability::kAvailable,
                     api::MemoryAvailability::kUnavailable);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<uint32_t> record_count =
      ReadLength(api::kMaxMemoryRecords);
  if (!availability || !revision || !record_count) {
    return std::nullopt;
  }
  std::vector<ObservedMemoryRecord> records;
  records.reserve(*record_count);
  for (uint32_t index = 0u; index < *record_count; ++index) {
    std::optional<ObservedMemoryRecord> record = ReadMemoryRecord();
    if (!record) {
      return std::nullopt;
    }
    records.push_back(std::move(*record));
  }
  const std::optional<bool> search_present = ReadBool();
  if (!search_present) {
    return std::nullopt;
  }
  std::optional<ObservedMemorySearch> search;
  if (*search_present) {
    search = ReadMemorySearch();
    if (!search) {
      return std::nullopt;
    }
  }
  return ObservedMemoryStatus{*availability, *revision, std::move(records),
                              std::move(search)};
}

std::optional<ObservedMemoryRecord> CoreStatusWireReader::ReadMemoryRecord() {
  std::optional<std::string> memory_id = ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> revision = ReadU64();
  std::optional<std::string> statement =
      ReadString(api::kMaxMemoryStatementBytes);
  const std::optional<api::MemorySourceKind> source_kind = ReadClosedEnum(
      api::MemorySourceKind::kYouWrote, api::MemorySourceKind::kTaffySuggested);
  const std::optional<bool> task_present = ReadBool();
  if (!memory_id || !revision || !statement || !source_kind || !task_present) {
    return std::nullopt;
  }
  std::optional<std::string> task_id;
  if (*task_present) {
    task_id = ReadString(api::kMaxIdentifierBytes);
    if (!task_id) {
      return std::nullopt;
    }
  }
  const std::optional<bool> source_workspace_present = ReadBool();
  if (!source_workspace_present) {
    return std::nullopt;
  }
  std::optional<ObservedMemoryWorkspace> source_workspace;
  if (*source_workspace_present) {
    source_workspace = ReadMemoryWorkspace();
    if (!source_workspace) {
      return std::nullopt;
    }
  }
  const std::optional<api::MemoryScopeKind> scope = ReadClosedEnum(
      api::MemoryScopeKind::kAllTasks, api::MemoryScopeKind::kWorkspace);
  const std::optional<bool> scope_workspace_present = ReadBool();
  if (!scope || !scope_workspace_present) {
    return std::nullopt;
  }
  std::optional<ObservedMemoryWorkspace> scope_workspace;
  if (*scope_workspace_present) {
    scope_workspace = ReadMemoryWorkspace();
    if (!scope_workspace) {
      return std::nullopt;
    }
  }
  const std::optional<api::MemorySensitivity> sensitivity = ReadClosedEnum(
      api::MemorySensitivity::kStandard, api::MemorySensitivity::kSensitive);
  const std::optional<uint64_t> created = ReadU64();
  const std::optional<uint64_t> updated = ReadU64();
  const std::optional<uint64_t> reviewed = ReadU64();
  const std::optional<uint64_t> expires = ReadU64();
  if (!sensitivity || !created || !updated || !reviewed || !expires) {
    return std::nullopt;
  }
  return ObservedMemoryRecord{std::move(*memory_id),
                              *revision,
                              std::move(*statement),
                              *source_kind,
                              std::move(task_id),
                              std::move(source_workspace),
                              *scope,
                              std::move(scope_workspace),
                              *sensitivity,
                              *created,
                              *updated,
                              *reviewed,
                              *expires};
}

std::optional<ObservedMemoryWorkspace>
CoreStatusWireReader::ReadMemoryWorkspace() {
  std::optional<std::string> workspace_id =
      ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> display_name =
      ReadString(api::kMaxWorkspaceDisplayNameBytes);
  if (!workspace_id || !display_name) {
    return std::nullopt;
  }
  return ObservedMemoryWorkspace{std::move(*workspace_id),
                                 std::move(*display_name)};
}

std::optional<ObservedMemorySearch> CoreStatusWireReader::ReadMemorySearch() {
  std::optional<std::string> request_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> query = ReadString(api::kMaxMemoryQueryBytes);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<uint32_t> hit_count =
      ReadLength(api::kMaxMemorySearchResults);
  if (!request_id || !query || !revision || !hit_count) {
    return std::nullopt;
  }
  std::vector<std::string> memory_ids;
  memory_ids.reserve(*hit_count);
  for (uint32_t index = 0u; index < *hit_count; ++index) {
    std::optional<std::string> memory_id = ReadString(api::kMaxIdentifierBytes);
    if (!memory_id) {
      return std::nullopt;
    }
    memory_ids.push_back(std::move(*memory_id));
  }
  return ObservedMemorySearch{std::move(*request_id), std::move(*query),
                              *revision, std::move(memory_ids)};
}

}  // namespace taffy::test::internal
