// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cctype>
#include <optional>

#include "base/strings/string_util.h"
#include "taffy/browser/core_service_command_validation_internal.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsMemoryText(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         !std::isspace(static_cast<unsigned char>(value.front())) &&
         !std::isspace(static_cast<unsigned char>(value.back())) &&
         std::none_of(value.begin(), value.end(),
                      [](unsigned char byte) { return std::iscntrl(byte); });
}

bool IsMemoryQuery(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxMemoryQueryBytes ||
      !base::IsStringUTF8(value)) {
    return false;
  }
  bool visible = false;
  for (char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::iscntrl(byte) && !std::isspace(byte)) {
      return false;
    }
    visible = visible || !std::isspace(byte);
  }
  return visible;
}

bool IsMemoryId(const std::string& value) {
  return value.size() == 32u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool AddMemoryId(const std::string& value, size_t ceiling, size_t* total) {
  return IsMemoryId(value) && AddBounded(value.size(), ceiling, total);
}

bool AddMemoryWorkspace(const mojom::MemoryWorkspaceRecord& workspace,
                        size_t ceiling,
                        size_t* total) {
  return AddMemoryId(workspace.workspace_id, ceiling, total) &&
         IsMemoryText(workspace.display_name,
                      mojom::kMaxWorkspaceDisplayNameBytes) &&
         AddBounded(workspace.display_name.size(), ceiling, total);
}

bool AddMemoryScope(const mojom::UpsertMemoryCommand& memory,
                    size_t ceiling,
                    size_t* total) {
  switch (memory.scope_kind) {
    case mojom::MemoryScopeKind::kAllTasks:
      return !memory.scope_workspace;
    case mojom::MemoryScopeKind::kWorkspace:
      return memory.scope_workspace &&
             AddMemoryWorkspace(*memory.scope_workspace, ceiling, total);
  }
  return false;
}

bool IsMemorySensitivity(mojom::MemorySensitivity sensitivity) {
  switch (sensitivity) {
    case mojom::MemorySensitivity::kStandard:
    case mojom::MemorySensitivity::kSensitive:
      return true;
  }
  return false;
}

}  // namespace

std::optional<size_t> MemoryCommandByteSize(
    const mojom::CoreServiceCommand& command,
    size_t ceiling) {
  size_t total = 0;
  if (command.search_memory) {
    const auto& memory = *command.search_memory;
    return AddIdentifier(memory.request_id, ceiling, &total) &&
                   IsMemoryQuery(memory.query) && memory.limit != 0u &&
                   memory.limit <= mojom::kMaxMemorySearchResults &&
                   AddBounded(memory.query.size(), ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.upsert_memory) {
    const auto& memory = *command.upsert_memory;
    if (memory.memory_id.has_value() !=
            (memory.expected_record_revision != 0u) ||
        (memory.memory_id &&
         !AddMemoryId(*memory.memory_id, ceiling, &total)) ||
        !IsMemoryText(memory.statement, mojom::kMaxMemoryStatementBytes) ||
        !AddBounded(memory.statement.size(), ceiling, &total) ||
        !AddMemoryScope(memory, ceiling, &total) ||
        !IsMemorySensitivity(memory.sensitivity) ||
        (memory.expires_at_epoch_ms != 0u &&
         memory.expires_at_epoch_ms <= memory.approved_at_epoch_ms)) {
      return std::nullopt;
    }
    return total;
  }
  if (command.delete_memory) {
    const auto& memory = *command.delete_memory;
    return memory.expected_record_revision != 0u &&
                   AddMemoryId(memory.memory_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace taffy
