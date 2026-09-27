// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/rust_core_skill_match.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

mojom::SiteSkillMatchResultPtr RustCore::MatchSiteSkills(
    mojom::SiteSkillMatchCommandPtr command,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !command || !command->operation) {
    return nullptr;
  }
  const std::string expected_operation_id = command->operation->operation_id;
  const uint64_t expected_generation = command->operation->service_generation;
  const std::string expected_idempotency_key =
      command->operation->idempotency_key;
  std::optional<bridge::BridgeSiteSkillMatchCommand> projected =
      core_service_internal::ToBridgeSiteSkillMatchCommand(std::move(command));
  if (!projected) {
    return nullptr;
  }
  mojom::SiteSkillMatchResultPtr result =
      core_service_internal::ToMojoSiteSkillMatchResult(bridge::MatchSiteSkills(
          *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  if (!result || !result->operation ||
      result->operation->operation_id != expected_operation_id ||
      result->operation->service_generation != expected_generation ||
      result->operation->idempotency_key != expected_idempotency_key) {
    return nullptr;
  }
  return result;
}

}  // namespace taffy
