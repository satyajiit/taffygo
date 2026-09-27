// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_PROJECTION_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_PROJECTION_H_

#include <optional>

#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy::core_service_internal {

core_service::mojom::OperationEnvelopePtr ToMojoOperation(
    const core_bridge::BridgeOperation& input);

std::optional<core_service::mojom::TaskPolicyEffectPtr> ToMojoPolicyEffect(
    const core_bridge::BridgeTaskEffect& input);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_PROJECTION_H_
