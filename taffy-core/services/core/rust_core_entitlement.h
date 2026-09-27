// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_ENTITLEMENT_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_ENTITLEMENT_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_entitlement_ffi.rs.h"

namespace taffy::core_service_internal {

// The entitlement fetch result an adapter produced, flattened for the bridge.
// Absent when the result is not a network result for the entitlement
// operation; nothing here decides anything — a mismatch is refused rather
// than repaired. A result whose status is not COMPLETED, or that carries no
// summary, crosses with `has_summary` false: that is the transport-failure
// shape, and the ordered core keeps its last state on it.
std::optional<core_bridge::BridgeEntitlementFetchResult>
ToBridgeEntitlementFetchResult(const core_service::mojom::EffectResult &result);

// One planned entitlement fetch the ordered core wants performed.
core_service::mojom::EffectEnvelopePtr ToMojoEntitlementFetchEffect(
    const core_bridge::BridgeEntitlementFetchEffect &effect);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_ENTITLEMENT_H_
