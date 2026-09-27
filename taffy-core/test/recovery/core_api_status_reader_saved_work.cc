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

std::optional<ObservedLibraryStatus> CoreStatusWireReader::ReadLibrary() {
  const std::optional<api::LibraryAvailability> availability =
      ReadClosedEnum(api::LibraryAvailability::kAvailable,
                     api::LibraryAvailability::kUnavailable);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<uint32_t> entry_count =
      ReadLength(api::kMaxLibraryEntries);
  if (!availability || !revision || !entry_count) {
    return std::nullopt;
  }
  std::vector<ObservedLibraryEntry> entries;
  entries.reserve(*entry_count);
  for (uint32_t index = 0u; index < *entry_count; ++index) {
    std::optional<ObservedLibraryEntry> entry = ReadLibraryEntry();
    if (!entry) {
      return std::nullopt;
    }
    entries.push_back(std::move(*entry));
  }

  const std::optional<bool> search_present = ReadBool();
  if (!search_present) {
    return std::nullopt;
  }
  std::optional<ObservedLibrarySearch> search;
  if (*search_present) {
    search = ReadLibrarySearch();
    if (!search) {
      return std::nullopt;
    }
  }
  const std::optional<uint32_t> preview_count = ReadLength(api::kMaxWorkspaces);
  if (!preview_count) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *preview_count; ++index) {
    if (!SkipLibraryRefreshPreview()) {
      return std::nullopt;
    }
  }
  const std::optional<uint32_t> result_count =
      ReadLength(api::kMaxLibraryRefreshResults);
  if (!result_count) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *result_count; ++index) {
    if (!SkipLibraryRefreshResult()) {
      return std::nullopt;
    }
  }
  return ObservedLibraryStatus{*availability, *revision, std::move(entries),
                               std::move(search)};
}

std::optional<ObservedLibraryEntry> CoreStatusWireReader::ReadLibraryEntry() {
  std::optional<std::string> entry_id = ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> revision = ReadU64();
  std::optional<std::string> collection_id =
      ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> collection_name =
      ReadString(api::kMaxWorkspaceDisplayNameBytes);
  std::optional<std::string> workspace_id =
      ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> workspace_revision = ReadU64();
  std::optional<std::string> fact_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> field = ReadString(api::kMaxFactFieldBytes);
  std::optional<std::string> value = ReadString(api::kMaxFactValueBytes);
  const std::optional<bool> correction_present = ReadBool();
  if (!entry_id || !revision || !collection_id || !collection_name ||
      !workspace_id || !workspace_revision || !fact_id || !field || !value ||
      !correction_present) {
    return std::nullopt;
  }
  std::optional<std::string> correction;
  if (*correction_present) {
    correction = ReadString(api::kMaxFactValueBytes);
    if (!correction) {
      return std::nullopt;
    }
  }
  const std::optional<api::WorkspaceFactKind> kind = ReadClosedEnum(
      api::WorkspaceFactKind::kFromPage, api::WorkspaceFactKind::kUserEntered);
  const std::optional<uint32_t> source_count =
      ReadLength(api::kMaxLibrarySources);
  if (!kind || !source_count) {
    return std::nullopt;
  }
  std::vector<ObservedLibrarySource> sources;
  sources.reserve(*source_count);
  for (uint32_t index = 0u; index < *source_count; ++index) {
    std::optional<ObservedLibrarySource> source = ReadLibrarySource();
    if (!source) {
      return std::nullopt;
    }
    sources.push_back(std::move(*source));
  }
  const std::optional<uint64_t> captured = ReadU64();
  const std::optional<uint64_t> checked = ReadU64();
  const std::optional<bool> conflict = ReadBool();
  if (!captured || !checked || !conflict) {
    return std::nullopt;
  }
  return ObservedLibraryEntry{std::move(*entry_id),
                              *revision,
                              std::move(*collection_id),
                              std::move(*collection_name),
                              std::move(*workspace_id),
                              *workspace_revision,
                              std::move(*fact_id),
                              std::move(*field),
                              std::move(*value),
                              std::move(correction),
                              *kind,
                              std::move(sources),
                              *captured,
                              *checked,
                              *conflict};
}

std::optional<ObservedLibrarySource> CoreStatusWireReader::ReadLibrarySource() {
  std::optional<std::string> source_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> title = ReadString(api::kMaxWorkspaceTitleBytes);
  std::optional<std::string> host = ReadString(api::kMaxSourceHostBytes);
  const std::optional<uint64_t> observed = ReadU64();
  if (!source_id || !title || !host || !observed) {
    return std::nullopt;
  }
  return ObservedLibrarySource{std::move(*source_id), std::move(*title),
                               std::move(*host), *observed};
}

std::optional<ObservedLibrarySearch> CoreStatusWireReader::ReadLibrarySearch() {
  std::optional<std::string> request_id = ReadString(api::kMaxIdentifierBytes);
  std::optional<std::string> query = ReadString(api::kMaxLibraryQueryBytes);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<uint32_t> hit_count =
      ReadLength(api::kMaxLibrarySearchResults);
  if (!request_id || !query || !revision || !hit_count) {
    return std::nullopt;
  }
  std::vector<ObservedLibrarySearchHit> hits;
  hits.reserve(*hit_count);
  for (uint32_t index = 0u; index < *hit_count; ++index) {
    std::optional<std::string> entry_id = ReadString(api::kMaxIdentifierBytes);
    const std::optional<uint64_t> age_ms = ReadU64();
    if (!entry_id || !age_ms) {
      return std::nullopt;
    }
    hits.push_back(ObservedLibrarySearchHit{std::move(*entry_id), *age_ms});
  }
  return ObservedLibrarySearch{std::move(*request_id), std::move(*query),
                               *revision, std::move(hits)};
}

bool CoreStatusWireReader::SkipLibraryRefreshPreview() {
  if (!ReadString(api::kMaxLibraryRefreshPreviewIdBytes) ||
      !ReadString(api::kMaxIdentifierBytes) || !ReadU64() || !ReadU64() ||
      !ReadClosedEnum(api::TaskProviderRoute::kNotConfigured,
                      api::TaskProviderRoute::kNoModelRequired) ||
      !ReadU32() || !ReadU32()) {
    return false;
  }
  const std::optional<uint32_t> count =
      ReadLength(api::kMaxLibraryRefreshSources);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!ReadString(api::kMaxIdentifierBytes) ||
        !ReadString(api::kMaxWorkspaceTitleBytes) ||
        !ReadString(api::kMaxSourceHostBytes)) {
      return false;
    }
  }
  return true;
}

bool CoreStatusWireReader::SkipLibraryRefreshResult() {
  if (!ReadString(api::kMaxLibraryRefreshPreviewIdBytes) ||
      !ReadString(api::kMaxIdentifierBytes)) {
    return false;
  }
  const std::optional<uint32_t> count =
      ReadLength(api::kMaxLibraryRefreshSources);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!ReadString(api::kMaxIdentifierBytes) ||
        !ReadClosedEnum(api::LibraryRefreshDisposition::kUnchanged,
                        api::LibraryRefreshDisposition::kMissing)) {
      return false;
    }
  }
  return true;
}

std::optional<ObservedLibraryExport> CoreStatusWireReader::ReadLibraryExport() {
  std::optional<std::string> request_id = ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<bool> collection_present = ReadBool();
  if (!request_id || !revision || !collection_present) {
    return std::nullopt;
  }
  std::optional<std::string> collection_id;
  if (*collection_present) {
    collection_id = ReadString(api::kMaxIdentifierBytes);
    if (!collection_id) {
      return std::nullopt;
    }
  }
  const std::optional<api::WorkspaceExportFormat> format = ReadClosedEnum(
      api::WorkspaceExportFormat::kMarkdown, api::WorkspaceExportFormat::kCsv);
  std::optional<std::string> content = ReadString(api::kMaxExportContentBytes);
  if (!format || !content) {
    return std::nullopt;
  }
  return ObservedLibraryExport{std::move(*request_id), *revision,
                               std::move(collection_id), *format,
                               std::move(*content)};
}

}  // namespace taffy::test::internal
