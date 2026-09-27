// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cctype>
#include <optional>

#include "taffy/browser/core_service_command_validation_internal.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool AddDisplayName(const std::string& value, size_t ceiling, size_t* total) {
  return !value.empty() &&
         value.size() <= mojom::kMaxWorkspaceDisplayNameBytes &&
         !std::isspace(static_cast<unsigned char>(value.front())) &&
         !std::isspace(static_cast<unsigned char>(value.back())) &&
         !std::any_of(
             value.begin(), value.end(),
             [](char character) {
               return std::iscntrl(static_cast<unsigned char>(character));
             }) &&
         AddBounded(value.size(), ceiling, total);
}

bool AddConfirmationToken(const std::string& value,
                          size_t ceiling,
                          size_t* total) {
  return value.size() == mojom::kMaxWorkspaceConfirmationTokenBytes &&
         std::all_of(value.begin(), value.end(),
                     [](char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                     }) &&
         AddBounded(value.size(), ceiling, total);
}

}  // namespace

std::optional<size_t> WorkspaceCommandByteSize(
    const mojom::CoreServiceCommand& command,
    size_t ceiling) {
  size_t total = 0u;
  if (command.correct_workspace_fact) {
    const auto& workspace = *command.correct_workspace_fact;
    return AddIdentifier(workspace.workspace_id, ceiling, &total) &&
                   AddIdentifier(workspace.fact_id, ceiling, &total) &&
                   !workspace.value.empty() &&
                   workspace.value.size() <= mojom::kMaxWorkspaceValueBytes &&
                   AddBounded(workspace.value.size(), ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.exclude_workspace_source) {
    const auto& workspace = *command.exclude_workspace_source;
    return AddIdentifier(workspace.workspace_id, ceiling, &total) &&
                   AddIdentifier(workspace.source_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.request_workspace_export) {
    const auto& workspace = *command.request_workspace_export;
    return AddIdentifier(workspace.request_id, ceiling, &total) &&
                   AddIdentifier(workspace.workspace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.save_workspace) {
    return AddIdentifier(command.save_workspace->workspace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.rename_workspace) {
    const auto& workspace = *command.rename_workspace;
    return AddIdentifier(workspace.workspace_id, ceiling, &total) &&
                   AddDisplayName(workspace.display_name, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.delete_workspace) {
    const auto& workspace = *command.delete_workspace;
    return AddIdentifier(workspace.workspace_id, ceiling, &total) &&
                   AddConfirmationToken(workspace.confirmation_token, ceiling,
                                        &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.discard_workspace) {
    return AddIdentifier(command.discard_workspace->workspace_id, ceiling,
                         &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace taffy
