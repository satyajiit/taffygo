// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_SKILL_MATCH_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_SKILL_MATCH_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "taffy/services/core/service_bridge_skill_match_ffi.rs.h"

namespace taffy::core_service_internal {

std::optional<core_bridge::BridgeSiteSkillMatchCommand>
ToBridgeSiteSkillMatchCommand(
    core_service::mojom::SiteSkillMatchCommandPtr command);
core_service::mojom::SiteSkillMatchResultPtr ToMojoSiteSkillMatchResult(
    core_bridge::BridgeSiteSkillMatchResult result);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_SKILL_MATCH_H_
