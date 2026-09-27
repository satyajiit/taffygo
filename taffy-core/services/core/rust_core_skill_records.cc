// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_skill_records.h"

#include <limits>
#include <utility>

#include "base/strings/string_util.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
// The header declares this seam over the forward declaration alone; reading a
// SkillRecord's fields and its bound constants needs the definition.
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace wire = core_service::wire;

bool IsBounded(const mojom::SkillRecordPtr& record) {
  return record && !record->skill_id.empty() &&
         record->skill_id.size() <= mojom::kMaxSkillIdBytes &&
         base::IsStringUTF8(record->skill_id) && !record->origin.empty() &&
         record->origin.size() <= mojom::kMaxNormalizedOriginBytes &&
         base::IsStringUTF8(record->origin) &&
         wire::SkillProvenanceFromWire(
             static_cast<uint32_t>(record->provenance)) &&
         wire::SkillStatusFromWire(static_cast<uint32_t>(record->status)) &&
         record->active_version > 0u &&
         record->active_version <= mojom::kMaxSkillVersionsPerSkill &&
         !record->definition.empty() &&
         record->definition.size() <= mojom::kMaxSkillDefinitionBytes &&
         record->step_count > 0u &&
         record->step_count <= mojom::kMaxSkillSteps &&
         record->installed_at_utc_ms <=
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) &&
         record->updated_at_utc_ms <=
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

}  // namespace

std::optional<rust::Vec<core_bridge::BridgeSkillRecord>> ToBridgeSkillRecords(
    const std::vector<mojom::SkillRecordPtr>& records) {
  if (records.size() > mojom::kMaxSkillsPerProfile) {
    return std::nullopt;
  }
  // Refuse a malformed later row before copying any earlier definition.
  for (const auto& record : records) {
    if (!IsBounded(record)) {
      return std::nullopt;
    }
  }
  rust::Vec<core_bridge::BridgeSkillRecord> projected;
  projected.reserve(records.size());
  for (const auto& record : records) {
    core_bridge::BridgeSkillRecord restored;
    restored.skill_id = record->skill_id;
    restored.origin = record->origin;
    restored.provenance = static_cast<uint8_t>(record->provenance);
    restored.status = static_cast<uint8_t>(record->status);
    restored.active_version = record->active_version;
    restored.definition.reserve(record->definition.size());
    for (const uint8_t byte : record->definition) {
      restored.definition.push_back(byte);
    }
    restored.step_count = record->step_count;
    restored.installed_at_utc_ms = record->installed_at_utc_ms;
    restored.updated_at_utc_ms = record->updated_at_utc_ms;
    projected.push_back(std::move(restored));
  }
  return projected;
}

}  // namespace taffy::core_service_internal
