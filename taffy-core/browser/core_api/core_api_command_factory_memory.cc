// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

#include "base/strings/string_util.h"
#include "taffy/browser/core_api/core_api_command_factory.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

bool IsCanonicalId(const std::string& value) {
  return value.size() == 32u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsMemoryText(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         !std::isspace(static_cast<unsigned char>(value.front())) &&
         !std::isspace(static_cast<unsigned char>(value.back())) &&
         std::none_of(value.begin(), value.end(), [](unsigned char character) {
           return std::iscntrl(character);
         });
}

bool IsMemoryQuery(const std::string& value) {
  if (value.empty() || value.size() > api::kMaxMemoryQueryBytes ||
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

bool IsMemoryWorkspace(const api::MemoryWorkspaceView& workspace) {
  return IsCanonicalId(workspace.workspace_id) &&
         IsMemoryText(workspace.display_name,
                      api::kMaxWorkspaceDisplayNameBytes);
}

bool IsMemoryScope(api::MemoryScopeKind kind,
                   const api::MemoryWorkspaceViewPtr& workspace) {
  switch (kind) {
    case api::MemoryScopeKind::kAllTasks:
      return !workspace;
    case api::MemoryScopeKind::kWorkspace:
      return workspace && IsMemoryWorkspace(*workspace);
  }
  return false;
}

std::optional<service::MemoryScopeKind> ProjectScope(
    api::MemoryScopeKind kind) {
  switch (kind) {
    case api::MemoryScopeKind::kAllTasks:
      return service::MemoryScopeKind::kAllTasks;
    case api::MemoryScopeKind::kWorkspace:
      return service::MemoryScopeKind::kWorkspace;
  }
  return std::nullopt;
}

std::optional<service::MemorySensitivity> ProjectSensitivity(
    api::MemorySensitivity sensitivity) {
  switch (sensitivity) {
    case api::MemorySensitivity::kStandard:
      return service::MemorySensitivity::kStandard;
    case api::MemorySensitivity::kSensitive:
      return service::MemorySensitivity::kSensitive;
  }
  return std::nullopt;
}

api::MemoryWorkspaceViewPtr CopyApiWorkspace(
    const api::MemoryWorkspaceViewPtr& value) {
  return value ? api::MemoryWorkspaceView::New(value->workspace_id,
                                               value->display_name)
               : nullptr;
}

service::MemoryWorkspaceRecordPtr ProjectWorkspace(
    const api::MemoryWorkspaceViewPtr& value) {
  return value ? service::MemoryWorkspaceRecord::New(value->workspace_id,
                                                     value->display_name)
               : nullptr;
}

}  // namespace

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildSearchMemory(
    std::string request_id,
    std::string query,
    uint32_t limit,
    uint64_t requested_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (request_id.empty() || request_id.size() > api::kMaxIdentifierBytes ||
      !IsMemoryQuery(query) || limit == 0u ||
      limit > api::kMaxMemorySearchResults) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSearchMemory;
  core_command->search_memory = api::SearchMemoryBody::New(
      request_id, query, limit, requested_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSearchMemory;
  service_command->search_memory = service::SearchMemoryCommand::New(
      std::move(request_id), std::move(query), limit, requested_at_epoch_ms);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildUpsertMemory(
    std::optional<std::string> memory_id,
    std::string statement,
    api::MemoryScopeKind scope_kind,
    api::MemoryWorkspaceViewPtr scope_workspace,
    api::MemorySensitivity sensitivity,
    uint64_t expected_memory_revision,
    uint64_t expected_record_revision,
    uint64_t expires_at_epoch_ms,
    uint64_t approved_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  const std::optional<service::MemoryScopeKind> service_scope =
      ProjectScope(scope_kind);
  const std::optional<service::MemorySensitivity> service_sensitivity =
      ProjectSensitivity(sensitivity);
  if (!service_scope || !service_sensitivity ||
      (memory_id && !IsCanonicalId(*memory_id)) ||
      memory_id.has_value() != (expected_record_revision != 0u) ||
      !IsMemoryText(statement, api::kMaxMemoryStatementBytes) ||
      !IsMemoryScope(scope_kind, scope_workspace) ||
      (expires_at_epoch_ms != 0u &&
       expires_at_epoch_ms <= approved_at_epoch_ms)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kUpsertMemory;
  core_command->upsert_memory = api::UpsertMemoryBody::New(
      memory_id, statement, scope_kind, CopyApiWorkspace(scope_workspace),
      sensitivity, expected_memory_revision, expected_record_revision,
      expires_at_epoch_ms, approved_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kUpsertMemory;
  service_command->upsert_memory = service::UpsertMemoryCommand::New(
      std::move(memory_id), std::move(statement), *service_scope,
      ProjectWorkspace(scope_workspace), *service_sensitivity,
      expected_memory_revision, expected_record_revision, expires_at_epoch_ms,
      approved_at_epoch_ms);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildDeleteMemory(
    std::string memory_id,
    uint64_t expected_memory_revision,
    uint64_t expected_record_revision,
    uint64_t deleted_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsCanonicalId(memory_id) || expected_record_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kDeleteMemory;
  core_command->delete_memory =
      api::DeleteMemoryBody::New(memory_id, expected_memory_revision,
                                 expected_record_revision, deleted_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kDeleteMemory;
  service_command->delete_memory = service::DeleteMemoryCommand::New(
      std::move(memory_id), expected_memory_revision, expected_record_revision,
      deleted_at_epoch_ms);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

}  // namespace taffy
