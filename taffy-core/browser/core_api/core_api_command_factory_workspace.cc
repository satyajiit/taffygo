// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

#include "taffy/browser/core_api/core_api_command_factory.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

bool IsDisplayName(const std::string& value) {
  return !value.empty() && value.size() <= api::kMaxWorkspaceDisplayNameBytes &&
         !std::isspace(static_cast<unsigned char>(value.front())) &&
         !std::isspace(static_cast<unsigned char>(value.back())) &&
         !std::any_of(value.begin(), value.end(), [](char character) {
           return std::iscntrl(static_cast<unsigned char>(character));
         });
}

bool IsConfirmationToken(const std::string& value) {
  return value.size() == api::kMaxWorkspaceConfirmationTokenBytes &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsLibraryQuery(const std::string& value) {
  if (value.empty() || value.size() > api::kMaxLibraryQueryBytes) {
    return false;
  }
  bool has_non_whitespace = false;
  for (char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::iscntrl(byte) && !std::isspace(byte)) {
      return false;
    }
    has_non_whitespace = has_non_whitespace || !std::isspace(byte);
  }
  return has_non_whitespace;
}

std::optional<service::WorkspaceExportFormat> ProjectExportFormat(
    api::WorkspaceExportFormat format) {
  switch (format) {
    case api::WorkspaceExportFormat::kMarkdown:
      return service::WorkspaceExportFormat::kMarkdown;
    case api::WorkspaceExportFormat::kCsv:
      return service::WorkspaceExportFormat::kCsv;
  }
  return std::nullopt;
}

}  // namespace

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildCorrectWorkspaceFact(std::string workspace_id,
                                                 uint64_t expected_revision,
                                                 std::string fact_id,
                                                 std::string value,
                                                 uint64_t service_generation,
                                                 uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id) || !IsIdentifier(fact_id) || value.empty() ||
      value.size() > api::kMaxFactValueBytes) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kCorrectWorkspaceFact;
  core_command->correct_workspace_fact = api::CorrectWorkspaceFactBody::New(
      workspace_id, expected_revision, fact_id, value);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kCorrectWorkspaceFact;
  service_command->correct_workspace_fact =
      service::CorrectWorkspaceFactCommand::New(
          std::move(workspace_id), expected_revision, std::move(fact_id),
          std::move(value));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildExcludeWorkspaceSource(std::string workspace_id,
                                                   uint64_t expected_revision,
                                                   std::string source_id,
                                                   uint64_t service_generation,
                                                   uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id) || !IsIdentifier(source_id)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kExcludeWorkspaceSource;
  core_command->exclude_workspace_source = api::ExcludeWorkspaceSourceBody::New(
      workspace_id, expected_revision, source_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kExcludeWorkspaceSource;
  service_command->exclude_workspace_source =
      service::ExcludeWorkspaceSourceCommand::New(
          std::move(workspace_id), expected_revision, std::move(source_id));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRequestWorkspaceExport(
    std::string request_id,
    std::string workspace_id,
    uint64_t expected_revision,
    api::WorkspaceExportFormat format,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  const std::optional<service::WorkspaceExportFormat> service_format =
      ProjectExportFormat(format);
  if (!service_format || !IsIdentifier(request_id) ||
      !IsIdentifier(workspace_id)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRequestWorkspaceExport;
  core_command->request_workspace_export = api::RequestWorkspaceExportBody::New(
      request_id, workspace_id, expected_revision, format);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kRequestWorkspaceExport;
  service_command->request_workspace_export =
      service::RequestWorkspaceExportCommand::New(
          std::move(request_id), std::move(workspace_id), expected_revision,
          *service_format);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildSaveWorkspace(
    std::string workspace_id,
    uint64_t expected_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSaveWorkspace;
  core_command->save_workspace =
      api::SaveWorkspaceBody::New(workspace_id, expected_revision);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSaveWorkspace;
  service_command->save_workspace = service::SaveWorkspaceCommand::New(
      std::move(workspace_id), expected_revision);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildRenameWorkspace(
    std::string workspace_id,
    uint64_t expected_revision,
    std::string display_name,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id) || !IsDisplayName(display_name)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRenameWorkspace;
  core_command->rename_workspace = api::RenameWorkspaceBody::New(
      workspace_id, expected_revision, display_name);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kRenameWorkspace;
  service_command->rename_workspace = service::RenameWorkspaceCommand::New(
      std::move(workspace_id), expected_revision, std::move(display_name));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildDeleteWorkspace(
    std::string workspace_id,
    uint64_t expected_revision,
    std::string confirmation_token,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id) || !IsConfirmationToken(confirmation_token)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kDeleteWorkspace;
  core_command->delete_workspace = api::DeleteWorkspaceBody::New(
      workspace_id, expected_revision, confirmation_token);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kDeleteWorkspace;
  service_command->delete_workspace = service::DeleteWorkspaceCommand::New(
      std::move(workspace_id), expected_revision,
      std::move(confirmation_token));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildDiscardWorkspace(std::string workspace_id,
                                             uint64_t expected_revision,
                                             uint64_t service_generation,
                                             uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kDiscardWorkspace;
  core_command->discard_workspace =
      api::DiscardWorkspaceBody::New(workspace_id, expected_revision);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kDiscardWorkspace;
  service_command->discard_workspace = service::DiscardWorkspaceCommand::New(
      std::move(workspace_id), expected_revision);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildSearchLibrary(
    std::string request_id,
    std::string query,
    uint32_t limit,
    uint64_t requested_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(request_id) || !IsLibraryQuery(query) || limit == 0u ||
      limit > api::kMaxLibrarySearchResults) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSearchLibrary;
  core_command->search_library = api::SearchLibraryBody::New(
      request_id, query, limit, requested_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSearchLibrary;
  service_command->search_library = service::SearchLibraryCommand::New(
      std::move(request_id), std::move(query), limit, requested_at_epoch_ms);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildSaveLibraryFact(
    std::string workspace_id,
    uint64_t expected_workspace_revision,
    std::string fact_id,
    uint64_t expected_library_revision,
    uint64_t expected_entry_revision,
    uint64_t approved_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(workspace_id) || !IsIdentifier(fact_id) ||
      expected_workspace_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSaveLibraryFact;
  core_command->save_library_fact = api::SaveLibraryFactBody::New(
      workspace_id, expected_workspace_revision, fact_id,
      expected_library_revision, expected_entry_revision, approved_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSaveLibraryFact;
  service_command->save_library_fact = service::SaveLibraryFactCommand::New(
      std::move(workspace_id), expected_workspace_revision, std::move(fact_id),
      expected_library_revision, expected_entry_revision, approved_at_epoch_ms);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRemoveLibraryEntry(
    std::string entry_id,
    uint64_t expected_library_revision,
    uint64_t expected_entry_revision,
    uint64_t removed_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(entry_id) || expected_entry_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRemoveLibraryEntry;
  core_command->remove_library_entry = api::RemoveLibraryEntryBody::New(
      entry_id, expected_library_revision, expected_entry_revision,
      removed_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kRemoveLibraryEntry;
  service_command->remove_library_entry =
      service::RemoveLibraryEntryCommand::New(
          std::move(entry_id), expected_library_revision,
          expected_entry_revision, removed_at_epoch_ms);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRequestLibraryExport(
    std::string request_id,
    uint64_t expected_library_revision,
    std::optional<std::string> collection_id,
    api::WorkspaceExportFormat format,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  const std::optional<service::WorkspaceExportFormat> service_format =
      ProjectExportFormat(format);
  if (!service_format || !IsIdentifier(request_id) ||
      (collection_id && !IsIdentifier(*collection_id))) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRequestLibraryExport;
  core_command->request_library_export = api::RequestLibraryExportBody::New(
      request_id, expected_library_revision, collection_id, format);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kRequestLibraryExport;
  service_command->request_library_export =
      service::RequestLibraryExportCommand::New(
          std::move(request_id), expected_library_revision,
          std::move(collection_id), *service_format);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

}  // namespace taffy
