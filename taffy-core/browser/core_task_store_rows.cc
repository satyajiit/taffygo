// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_store_rows.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

// Unicode category Cc, the set the sandbox's own row rule refuses.
bool IsControl(char16_t character) {
  return character < 0x20u || (character >= 0x7fu && character <= 0x9fu);
}

// A tab or a line break between two words is a gap the person saw, so it
// becomes a space for the collapse that follows to fold; any other control
// is nothing they saw and is dropped.
bool IsWhitespaceControl(char16_t character) {
  return character >= 0x09u && character <= 0x0du;
}

std::u16string SpaceOrDropControls(const std::u16string& value) {
  std::u16string out;
  out.reserve(value.size());
  for (const char16_t character : value) {
    if (IsWhitespaceControl(character)) {
      out.push_back(u' ');
    } else if (!IsControl(character)) {
      out.push_back(character);
    }
  }
  return out;
}

// Collapses and trims whitespace with controls settled, bounds the bytes at
// a character boundary and trims again, since a cut can expose a space.
std::optional<std::string> BoundedTitle(const std::u16string& title) {
  const std::u16string collapsed = base::CollapseWhitespace(
      SpaceOrDropControls(title), /*trim_sequences_with_line_breaks=*/false);
  std::string utf8;
  if (!base::UTF16ToUTF8(collapsed.data(), collapsed.size(), &utf8)) {
    return std::nullopt;
  }
  std::string bounded(
      base::TruncateUTF8ToByteSize(utf8, mojom::kMaxTaskStoreRowFieldBytes));
  return std::string(
      base::TrimWhitespaceASCII(bounded, base::TrimPositions::TRIM_ALL));
}

}  // namespace

mojom::TaskStoreRowPtr ProjectTaskStoreRow(const TaskStoreEntry& entry) {
  if (!entry.url.is_valid() || !entry.url.SchemeIsHTTPOrHTTPS() ||
      entry.url.host().empty() ||
      entry.url.host().size() > mojom::kMaxTaskStoreRowFieldBytes) {
    return nullptr;
  }
  std::string path(entry.url.path());
  if (path.empty()) {
    path = "/";
  }
  if (path.size() > mojom::kMaxTaskStoreRowFieldBytes ||
      path.find('?') != std::string::npos ||
      path.find('#') != std::string::npos ||
      std::any_of(path.begin(), path.end(), [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return byte < 0x20u || byte == 0x7fu || byte == ' ';
      })) {
    return nullptr;
  }
  const std::optional<std::string> title = BoundedTitle(entry.title);
  if (!title) {
    return nullptr;
  }
  const int64_t when = entry.when.InMillisecondsSinceUnixEpoch();
  auto row = mojom::TaskStoreRow::New();
  row->title = *title;
  row->host = entry.url.host();
  row->path = path;
  row->when_utc_ms = when < 0 ? 0u : static_cast<uint64_t>(when);
  return row;
}

mojom::TaskStoreActionResultPtr BuildTaskStoreResult(
    mojom::TaskActionOperationKind operation,
    const std::vector<TaskStoreEntry>& entries,
    uint32_t limit) {
  auto result = mojom::TaskStoreActionResult::New();
  result->operation_kind = operation;
  const size_t cap = std::min<size_t>(limit, mojom::kMaxTaskStoreResults);
  uint32_t omitted = 0u;
  for (const TaskStoreEntry& entry : entries) {
    if (result->rows.size() >= cap) {
      ++omitted;
      continue;
    }
    mojom::TaskStoreRowPtr row = ProjectTaskStoreRow(entry);
    if (!row) {
      ++omitted;
      continue;
    }
    result->rows.push_back(std::move(row));
  }
  result->omitted = omitted;
  return result;
}

}  // namespace taffy
