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

std::optional<ObservedWorkspaceStatus> CoreStatusWireReader::ReadWorkspace() {
  std::optional<std::string> workspace_id =
      ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> revision = ReadU64();
  std::optional<std::string> goal = ReadString(api::kMaxTaskGoalBytes);
  const std::optional<api::WorkspacePhase> phase = ReadClosedEnum(
      api::WorkspacePhase::kRunning, api::WorkspacePhase::kFailed);
  const std::optional<uint64_t> last_updated_epoch_ms = ReadU64();
  const std::optional<api::TaskTemplateId> template_id = ReadClosedEnum(
      api::TaskTemplateId::kCompareProducts, api::TaskTemplateId::kWebErrand);
  const std::optional<uint32_t> source_count =
      ReadLength(api::kMaxWorkspaceSources);
  if (!workspace_id || !revision || !goal || !phase || !last_updated_epoch_ms ||
      !template_id || !source_count) {
    return std::nullopt;
  }

  std::vector<ObservedWorkspaceSource> sources;
  sources.reserve(*source_count);
  for (uint32_t index = 0u; index < *source_count; ++index) {
    std::optional<ObservedWorkspaceSource> source = ReadWorkspaceSource();
    if (!source) {
      return std::nullopt;
    }
    sources.push_back(std::move(*source));
  }

  const std::optional<uint32_t> fact_count =
      ReadLength(api::kMaxWorkspaceFacts);
  if (!fact_count) {
    return std::nullopt;
  }
  std::vector<ObservedWorkspaceFact> facts;
  facts.reserve(*fact_count);
  for (uint32_t index = 0u; index < *fact_count; ++index) {
    std::optional<ObservedWorkspaceFact> fact = ReadWorkspaceFact();
    if (!fact) {
      return std::nullopt;
    }
    facts.push_back(std::move(*fact));
  }

  const std::optional<bool> saved = ReadBool();
  std::optional<std::string> display_name =
      ReadString(api::kMaxWorkspaceDisplayNameBytes);
  const std::optional<bool> deletion_preview_present = ReadBool();
  if (!saved || !display_name || !deletion_preview_present) {
    return std::nullopt;
  }
  std::optional<ObservedWorkspaceDeletionPreview> deletion_preview;
  if (*deletion_preview_present) {
    deletion_preview = ReadDeletionPreview();
    if (!deletion_preview) {
      return std::nullopt;
    }
  }

  return ObservedWorkspaceStatus{std::move(*workspace_id),
                                 *revision,
                                 std::move(*goal),
                                 *phase,
                                 *last_updated_epoch_ms,
                                 *template_id,
                                 std::move(sources),
                                 std::move(facts),
                                 *saved,
                                 std::move(*display_name),
                                 std::move(deletion_preview)};
}

std::optional<ObservedWorkspaceSource>
CoreStatusWireReader::ReadWorkspaceSource() {
  std::optional<std::string> source_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> title = ReadString(api::kMaxWorkspaceTitleBytes);
  std::optional<std::string> host = ReadString(api::kMaxSourceHostBytes);
  const std::optional<uint64_t> read_at_epoch_ms = ReadU64();
  const std::optional<uint32_t> fact_count = ReadU32();
  const std::optional<bool> excluded = ReadBool();
  if (!source_id || !title || !host || !read_at_epoch_ms || !fact_count ||
      !excluded) {
    return std::nullopt;
  }
  return ObservedWorkspaceSource{std::move(*source_id), std::move(*title),
                                 std::move(*host),      *read_at_epoch_ms,
                                 *fact_count,           *excluded};
}

std::optional<ObservedWorkspaceFact> CoreStatusWireReader::ReadWorkspaceFact() {
  std::optional<std::string> fact_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> field = ReadString(api::kMaxFactFieldBytes);
  std::optional<std::string> value = ReadString(api::kMaxFactValueBytes);
  const std::optional<api::WorkspaceFactKind> kind = ReadClosedEnum(
      api::WorkspaceFactKind::kFromPage, api::WorkspaceFactKind::kUserEntered);
  const std::optional<uint32_t> source_count = ReadLength(api::kMaxFactSources);
  if (!fact_id || !field || !value || !kind || !source_count) {
    return std::nullopt;
  }
  std::vector<std::string> sources;
  sources.reserve(*source_count);
  for (uint32_t index = 0u; index < *source_count; ++index) {
    std::optional<std::string> source = ReadString(api::kMaxIdentifierBytes);
    if (!source) {
      return std::nullopt;
    }
    sources.push_back(std::move(*source));
  }

  const std::optional<bool> correction_present = ReadBool();
  if (!correction_present) {
    return std::nullopt;
  }
  std::optional<std::string> correction;
  if (*correction_present) {
    correction = ReadString(api::kMaxFactValueBytes);
    if (!correction) {
      return std::nullopt;
    }
  }
  const std::optional<bool> has_conflict = ReadBool();
  const std::optional<bool> needs_new_source = ReadBool();
  if (!has_conflict || !needs_new_source) {
    return std::nullopt;
  }
  return ObservedWorkspaceFact{std::move(*fact_id), std::move(*field),
                               std::move(*value),   *kind,
                               std::move(sources),  std::move(correction),
                               *has_conflict,       *needs_new_source};
}

std::optional<ObservedWorkspaceDeletionPreview>
CoreStatusWireReader::ReadDeletionPreview() {
  const std::optional<uint32_t> sources = ReadU32();
  const std::optional<uint32_t> facts = ReadU32();
  const std::optional<uint32_t> artifact_metadata = ReadU32();
  const std::optional<uint32_t> derived_indexes = ReadU32();
  std::optional<std::string> confirmation_token =
      ReadString(api::kMaxWorkspaceConfirmationTokenBytes);
  if (!sources || !facts || !artifact_metadata || !derived_indexes ||
      !confirmation_token) {
    return std::nullopt;
  }
  return ObservedWorkspaceDeletionPreview{*sources, *facts, *artifact_metadata,
                                          *derived_indexes,
                                          std::move(*confirmation_token)};
}

std::optional<ObservedWorkspaceExport>
CoreStatusWireReader::ReadWorkspaceExport() {
  std::optional<std::string> request_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> workspace_id =
      ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<api::WorkspaceExportFormat> format = ReadClosedEnum(
      api::WorkspaceExportFormat::kMarkdown, api::WorkspaceExportFormat::kCsv);
  std::optional<std::string> content = ReadString(api::kMaxExportContentBytes);
  if (!request_id || !workspace_id || !revision || !format || !content) {
    return std::nullopt;
  }
  return ObservedWorkspaceExport{std::move(*request_id),
                                 std::move(*workspace_id), *revision, *format,
                                 std::move(*content)};
}

}  // namespace taffy::test::internal
