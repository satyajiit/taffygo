// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/core_api_status_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"

namespace taffy::test::internal {
namespace {

constexpr std::array<uint8_t, 8> kCoreStatusMagic = {'T', 'A', 'F', 'F',
                                                     'Y', 'S', 'T', 'A'};

}  // namespace

CoreStatusWireReader::CoreStatusWireReader(base::span<const uint8_t> bytes)
    : bytes_(bytes) {}

std::optional<ObservedCoreStatusPayload> CoreStatusWireReader::Read(
    uint32_t schema_version) {
  if (bytes_.size() > api::kMaxEventPayloadBytes ||
      !ReadMagic(kCoreStatusMagic) || ReadU32() != schema_version) {
    return std::nullopt;
  }

  const std::optional<api::CoreAvailability> availability = ReadClosedEnum(
      api::CoreAvailability::kStarting, api::CoreAvailability::kCircuitOpen);
  const std::optional<uint64_t> generation = ReadU64();
  const std::optional<uint32_t> task_count = ReadLength(api::kMaxActiveTasks);
  if (!availability || !generation || !task_count) {
    return std::nullopt;
  }

  ObservedCoreStatusPayload payload;
  payload.availability = *availability;
  payload.generation = *generation;
  payload.tasks.reserve(*task_count);
  for (uint32_t index = 0u; index < *task_count; ++index) {
    std::optional<ObservedTaskStatus> task = ReadTask();
    if (!task) {
      return std::nullopt;
    }
    payload.tasks.push_back(std::move(*task));
  }

  if (!SkipOptionalAuth()) {
    return std::nullopt;
  }
  const std::optional<uint32_t> workspace_count =
      ReadLength(api::kMaxWorkspaces);
  if (!workspace_count) {
    return std::nullopt;
  }
  payload.workspaces.reserve(*workspace_count);
  for (uint32_t index = 0u; index < *workspace_count; ++index) {
    std::optional<ObservedWorkspaceStatus> workspace = ReadWorkspace();
    if (!workspace) {
      return std::nullopt;
    }
    payload.workspaces.push_back(std::move(*workspace));
  }

  const std::optional<bool> export_present = ReadBool();
  if (!export_present) {
    return std::nullopt;
  }
  if (*export_present) {
    payload.workspace_export = ReadWorkspaceExport();
    if (!payload.workspace_export) {
      return std::nullopt;
    }
  }
  if (!SkipOptionalAssetDelivery() || !SkipProviderRoster() ||
      !SkipProviderProbes() || !SkipProviderModels() ||
      !SkipAssistantConfiguration()) {
    return std::nullopt;
  }
  std::optional<ObservedLibraryStatus> library = ReadLibrary();
  if (!library) {
    return std::nullopt;
  }
  payload.library = std::move(*library);

  const std::optional<bool> library_export_present = ReadBool();
  if (!library_export_present) {
    return std::nullopt;
  }
  if (*library_export_present) {
    payload.library_export = ReadLibraryExport();
    if (!payload.library_export) {
      return std::nullopt;
    }
  }
  std::optional<ObservedMemoryStatus> memory = ReadMemory();
  if (!memory) {
    return std::nullopt;
  }
  payload.memory = std::move(*memory);
  if (!SkipSavedData()) {
    return std::nullopt;
  }
  const auto skills = ReadLength(api::kMaxSiteSkills);
  if (!skills) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *skills; ++index) {
    auto skill = ReadSiteSkill();
    if (!skill) {
      return std::nullopt;
    }
    payload.site_skills.push_back(std::move(*skill));
  }
  if (!ReadProjectionTail(&payload.site_skills_complete) || Remaining() != 0u) {
    return std::nullopt;
  }
  return payload;
}

bool CoreStatusWireReader::ReadMagic(base::span<const uint8_t> magic) {
  if (Remaining() < magic.size() ||
      !std::equal(magic.begin(), magic.end(), bytes_.begin() + offset_)) {
    return false;
  }
  offset_ += magic.size();
  return true;
}

std::optional<bool> CoreStatusWireReader::ReadBool() {
  if (Remaining() < 1u) {
    return std::nullopt;
  }
  const uint8_t value = bytes_[offset_++];
  if (value > 1u) {
    return std::nullopt;
  }
  return value == 1u;
}

std::optional<uint32_t> CoreStatusWireReader::ReadU32() {
  if (Remaining() < sizeof(uint32_t)) {
    return std::nullopt;
  }
  uint32_t value = 0u;
  for (size_t shift = 0u; shift < sizeof(uint32_t); ++shift) {
    value |= static_cast<uint32_t>(bytes_[offset_++]) << (shift * 8u);
  }
  return value;
}

std::optional<uint32_t> CoreStatusWireReader::ReadBoundedU32(uint64_t limit) {
  const std::optional<uint32_t> value = ReadU32();
  return value && *value <= limit ? value : std::nullopt;
}

std::optional<uint64_t> CoreStatusWireReader::ReadU64() {
  if (Remaining() < sizeof(uint64_t)) {
    return std::nullopt;
  }
  uint64_t value = 0u;
  for (size_t shift = 0u; shift < sizeof(uint64_t); ++shift) {
    value |= static_cast<uint64_t>(bytes_[offset_++]) << (shift * 8u);
  }
  return value;
}

std::optional<uint32_t> CoreStatusWireReader::ReadLength(uint64_t limit) {
  const std::optional<uint32_t> value = ReadU32();
  return value && *value <= limit ? value : std::nullopt;
}

std::optional<std::string> CoreStatusWireReader::ReadString(uint64_t limit) {
  const std::optional<uint32_t> length = ReadU32();
  if (!length || *length > limit || Remaining() < *length) {
    return std::nullopt;
  }
  std::string value(base::as_string_view(bytes_.subspan(offset_, *length)));
  offset_ += *length;
  return base::IsStringUTF8(value)
             ? std::optional<std::string>(std::move(value))
             : std::nullopt;
}

bool CoreStatusWireReader::SkipOptionalString(uint64_t limit) {
  const std::optional<bool> present = ReadBool();
  return present && (!*present || ReadString(limit).has_value());
}

bool CoreStatusWireReader::SkipString(uint64_t limit) {
  const auto length = ReadU32();
  if (!length || *length > limit || Remaining() < *length ||
      !base::IsStringUTF8(base::as_string_view(bytes_.subspan(offset_, *length)))) {
    return false;
  }
  offset_ += *length;
  return true;
}

size_t CoreStatusWireReader::Remaining() const {
  return bytes_.size() - offset_;
}

}  // namespace taffy::test::internal
