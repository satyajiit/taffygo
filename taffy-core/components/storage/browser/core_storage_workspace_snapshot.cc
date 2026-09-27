// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"
#include "url/gurl.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

constexpr std::string_view kSnapshotMagic = "TAFFYWS1";
constexpr uint32_t kLegacySnapshotSchemaVersion = 1u;
constexpr uint32_t kDisplayNameSnapshotSchemaVersion = 2u;
constexpr uint32_t kSnapshotSchemaVersion = 3u;
constexpr uint32_t kSourceLocatorSnapshotSchemaVersion = 4u;
constexpr uint32_t kMediaProvenanceSnapshotSchemaVersion = 5u;
constexpr uint32_t kMaxWorkspaceSources = 64u;
constexpr uint32_t kMaxWorkspaceFacts = 256u;
constexpr uint32_t kMaxFactSources = 16u;
constexpr uint32_t kMaxGoalBytes = 8192u;
constexpr uint32_t kMaxDisplayNameBytes = 256u;
constexpr uint32_t kMaxTitleBytes = 1024u;
constexpr uint32_t kMaxHostBytes = 253u;
constexpr uint32_t kMaxSourceLocatorBytes = 4096u;
constexpr uint32_t kMaxFieldBytes = 256u;
constexpr uint32_t kMaxMediaLocatorBytes = 512u;
constexpr uint32_t kMaxConfidencePpm = 1'000'000u;

bool IsCanonicalRecordId(std::string_view value) {
  return value.size() == 32u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

class SnapshotReader {
 public:
  explicit SnapshotReader(base::span<const uint8_t> bytes) : bytes_(bytes) {}

  bool ReadU8(uint8_t* value) {
    if (remaining() < 1u) {
      return false;
    }
    *value = bytes_[offset_++];
    return true;
  }

  bool ReadU32(uint32_t* value) {
    if (remaining() < 4u) {
      return false;
    }
    *value = static_cast<uint32_t>(bytes_[offset_]) |
             (static_cast<uint32_t>(bytes_[offset_ + 1u]) << 8u) |
             (static_cast<uint32_t>(bytes_[offset_ + 2u]) << 16u) |
             (static_cast<uint32_t>(bytes_[offset_ + 3u]) << 24u);
    offset_ += 4u;
    return true;
  }

  bool ReadU64(uint64_t* value) {
    if (remaining() < 8u) {
      return false;
    }
    *value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
      *value |= static_cast<uint64_t>(bytes_[offset_ + index]) << (index * 8u);
    }
    offset_ += 8u;
    return true;
  }

  bool ReadString(uint32_t maximum, bool allow_empty, std::string_view* value) {
    uint32_t length = 0u;
    if (!ReadU32(&length) || length > maximum ||
        (!allow_empty && length == 0u) || remaining() < length) {
      return false;
    }
    *value = base::as_string_view(bytes_.subspan(offset_, length));
    offset_ += length;
    return base::IsStringUTF8(*value);
  }

  bool AtEnd() const { return offset_ == bytes_.size(); }

  bool ReadMagic() {
    if (remaining() < kSnapshotMagic.size()) {
      return false;
    }
    const auto magic = bytes_.subspan(offset_, kSnapshotMagic.size());
    offset_ += kSnapshotMagic.size();
    return std::equal(magic.begin(), magic.end(), kSnapshotMagic.begin());
  }

 private:
  size_t remaining() const { return bytes_.size() - offset_; }

  const base::raw_span<const uint8_t> bytes_;
  size_t offset_ = 0u;
};

bool ReadBoolean(SnapshotReader* reader) {
  uint8_t value = 0u;
  return reader->ReadU8(&value) && value <= 1u;
}

bool ReadCanonicalId(SnapshotReader* reader, std::string_view* value) {
  return reader->ReadString(32u, false, value) && IsCanonicalRecordId(*value);
}

bool IsValidSourceHost(std::string_view host) {
  return !host.empty() && host.size() <= kMaxHostBytes &&
         base::IsStringASCII(host) &&
         host.find_first_of("/?#@") == std::string_view::npos;
}

bool ReadCanonicalLocator(SnapshotReader* reader,
                          std::string_view expected_host) {
  uint8_t present = 0u;
  std::string_view locator;
  if (!reader->ReadU8(&present) || present > 1u ||
      (present == 1u &&
       !reader->ReadString(kMaxSourceLocatorBytes, false, &locator))) {
    return false;
  }
  if (present == 0u) {
    return true;
  }
  const GURL url(locator);
  const bool has_forbidden_ascii =
      std::any_of(locator.begin(), locator.end(), [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return byte <= 0x20u || byte == 0x7fu;
      });
  return base::IsStringASCII(locator) && !has_forbidden_ascii &&
         locator.find_first_of("?#\\") == std::string_view::npos &&
         url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password() && !url.has_query() && !url.has_ref() &&
         url.HostNoBracketsPiece() == expected_host;
}

bool ReadMediaProvenance(SnapshotReader* reader, uint8_t workspace_fact_kind) {
  uint8_t present = 0u;
  if (!reader->ReadU8(&present) || present > 1u) {
    return false;
  }
  if (present == 0u) {
    return true;
  }
  uint8_t media_kind = 0u;
  uint8_t media_fact_kind = 0u;
  uint8_t evidence_kind = 0u;
  std::string_view locator;
  uint32_t source_start = 0u;
  uint32_t source_end = 0u;
  uint32_t page = 0u;
  uint64_t timestamp_start = 0u;
  uint64_t timestamp_end = 0u;
  uint32_t row = 0u;
  uint32_t confidence = 0u;
  if (workspace_fact_kind != 0u || !reader->ReadU8(&media_kind) ||
      media_kind > 2u || !reader->ReadU8(&media_fact_kind) ||
      media_fact_kind > 5u || !reader->ReadU8(&evidence_kind) ||
      evidence_kind > 5u ||
      !reader->ReadString(kMaxMediaLocatorBytes, false, &locator) ||
      !reader->ReadU32(&source_start) || !reader->ReadU32(&source_end) ||
      !reader->ReadU32(&page) || !reader->ReadU64(&timestamp_start) ||
      !reader->ReadU64(&timestamp_end) || !reader->ReadU32(&row) ||
      !reader->ReadU32(&confidence) || !ReadBoolean(reader) ||
      source_start > source_end || timestamp_start > timestamp_end ||
      confidence > kMaxConfidencePpm) {
    return false;
  }
  if (locator != base::TrimWhitespaceASCII(locator, base::TRIM_ALL) ||
      std::any_of(locator.begin(), locator.end(), [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return byte < 0x20u || byte == 0x7fu;
      })) {
    return false;
  }
  const bool has_page = page > 0u;
  const bool has_time = timestamp_end > 0u;
  const bool has_row = row > 0u;
  if (media_kind == 0u) {
    return !has_page && !has_time && !has_row && media_fact_kind == 5u &&
           (evidence_kind == 0u || evidence_kind == 1u) &&
           locator.starts_with("image:");
  }
  if (media_kind == 1u) {
    return (media_fact_kind == 2u && has_time && !has_page && !has_row &&
            evidence_kind == 2u && locator.starts_with("video:track/")) ||
           (media_fact_kind == 5u && !has_page && !has_time && !has_row &&
            evidence_kind == 0u && locator.starts_with("video:"));
  }
  return (media_fact_kind == 3u && has_page && !has_time && !has_row &&
          evidence_kind == 3u && locator.starts_with("pdf:page/")) ||
         (media_fact_kind == 4u && has_page && !has_time && has_row &&
          evidence_kind == 5u && locator.starts_with("pdf:page/")) ||
         (media_fact_kind == 5u && !has_time && !has_row &&
          evidence_kind == 3u && locator.starts_with("pdf:"));
}

bool DecodeSnapshotShape(base::span<const uint8_t> snapshot,
                         std::string_view expected_workspace_id,
                         uint64_t expected_revision,
                         WorkspaceSnapshotShape* shape) {
  SnapshotReader reader(snapshot);
  uint32_t schema_version = 0u;
  std::string_view workspace_id;
  uint64_t revision = 0u;
  std::string_view text;
  uint8_t closed_value = 0u;
  uint8_t saved = 1u;
  uint64_t timestamp = 0u;
  if (!reader.ReadMagic() || !reader.ReadU32(&schema_version) ||
      (schema_version != kLegacySnapshotSchemaVersion &&
       schema_version != kDisplayNameSnapshotSchemaVersion &&
       schema_version != kSnapshotSchemaVersion &&
       schema_version != kSourceLocatorSnapshotSchemaVersion &&
       schema_version != kMediaProvenanceSnapshotSchemaVersion) ||
      !ReadCanonicalId(&reader, &workspace_id) ||
      workspace_id != expected_workspace_id || !reader.ReadU64(&revision) ||
      revision != expected_revision ||
      !reader.ReadString(kMaxGoalBytes, false, &text) ||
      (schema_version >= kDisplayNameSnapshotSchemaVersion &&
       !reader.ReadString(kMaxDisplayNameBytes, false, &text)) ||
      !reader.ReadU8(&closed_value) || closed_value > 6u ||
      (schema_version >= kSnapshotSchemaVersion &&
       (!reader.ReadU8(&saved) || saved > 1u)) ||
      !reader.ReadU64(&timestamp) || !reader.ReadU8(&closed_value) ||
      closed_value > 3u) {
    return false;
  }

  uint32_t source_count = 0u;
  if (!reader.ReadU32(&source_count) || source_count > kMaxWorkspaceSources) {
    return false;
  }
  std::vector<std::string_view> source_ids;
  source_ids.reserve(source_count);
  for (uint32_t index = 0u; index < source_count; ++index) {
    std::string_view source_id;
    std::string_view source_host;
    if (!ReadCanonicalId(&reader, &source_id) ||
        (!source_ids.empty() && source_ids.back() >= source_id) ||
        !reader.ReadString(kMaxTitleBytes, false, &text) ||
        !reader.ReadString(kMaxHostBytes, false, &source_host) ||
        !IsValidSourceHost(source_host) ||
        (schema_version >= kSourceLocatorSnapshotSchemaVersion &&
         !ReadCanonicalLocator(&reader, source_host)) ||
        !reader.ReadU64(&timestamp) || !ReadBoolean(&reader)) {
      return false;
    }
    source_ids.push_back(source_id);
  }

  uint32_t fact_count = 0u;
  if (!reader.ReadU32(&fact_count) || fact_count > kMaxWorkspaceFacts) {
    return false;
  }
  std::string_view previous_fact_id;
  for (uint32_t index = 0u; index < fact_count; ++index) {
    std::string_view fact_id;
    uint8_t kind = 0u;
    uint32_t fact_source_count = 0u;
    if (!ReadCanonicalId(&reader, &fact_id) ||
        (!previous_fact_id.empty() && previous_fact_id >= fact_id) ||
        !reader.ReadString(kMaxFieldBytes, false, &text) ||
        !reader.ReadString(mojom::kMaxWorkspaceValueBytes, false, &text) ||
        !reader.ReadU8(&kind) || kind > 3u ||
        !reader.ReadU32(&fact_source_count) ||
        fact_source_count > kMaxFactSources ||
        (kind != 3u && fact_source_count == 0u)) {
      return false;
    }
    previous_fact_id = fact_id;
    std::string_view previous_source_id;
    for (uint32_t source_index = 0u; source_index < fact_source_count;
         ++source_index) {
      std::string_view source_id;
      if (!ReadCanonicalId(&reader, &source_id) ||
          (!previous_source_id.empty() && previous_source_id >= source_id) ||
          !std::binary_search(source_ids.begin(), source_ids.end(),
                              source_id)) {
        return false;
      }
      previous_source_id = source_id;
    }
    uint8_t has_correction = 0u;
    if (!reader.ReadU8(&has_correction) || has_correction > 1u ||
        (has_correction == 1u &&
         !reader.ReadString(mojom::kMaxWorkspaceValueBytes, false, &text)) ||
        !ReadBoolean(&reader) ||
        (schema_version >= kMediaProvenanceSnapshotSchemaVersion &&
         !ReadMediaProvenance(&reader, kind))) {
      return false;
    }
  }
  if (!reader.AtEnd() || !shape) {
    return false;
  }
  shape->workspace_id = std::string(workspace_id);
  shape->revision = revision;
  shape->source_count = source_count;
  shape->fact_count = fact_count;
  shape->saved = saved == 1u;
  return true;
}

}  // namespace

bool DecodeWorkspaceSnapshotShape(base::span<const uint8_t> snapshot,
                                  std::string_view expected_workspace_id,
                                  uint64_t expected_revision,
                                  WorkspaceSnapshotShape* shape) {
  return DecodeSnapshotShape(snapshot, expected_workspace_id, expected_revision,
                             shape);
}

}  // namespace taffy
