// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_SKILL_RECORDS_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_SKILL_RECORDS_H_

#include <optional>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "taffy/services/core/service_bridge_bootstrap_ffi.rs.h"

namespace taffy::core_service_internal {

// Shared by normal bootstrap and isolated backup-stage admission. Bounds and
// closed enumeration values are checked before allocating the second payload
// copy. The portable catalogue alone decodes and validates the definitions.
std::optional<rust::Vec<core_bridge::BridgeSkillRecord>> ToBridgeSkillRecords(
    const std::vector<core_service::mojom::SkillRecordPtr>& records);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_SKILL_RECORDS_H_
