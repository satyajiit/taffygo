// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_STATE_PERMISSIONS_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_STATE_PERMISSIONS_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy::core_service_internal {

// The platform permission a bridge state names, or nothing for a value this
// build does not know: an unknown permission is refused, never guessed.
std::optional<core_service::mojom::PlatformPermission> PermissionFromWire(
    uint8_t value);

// The browser effect that asks the person for one pending permission. Its
// identities are derived from the permission's own durable facts, so the
// same pending row projects to the same effect on every publication and the
// effect journal can refuse a repeat.
core_service::mojom::EffectEnvelopePtr ToPermissionEffect(
    const core_bridge::BridgePendingPermission& in,
    core_service::mojom::PlatformPermission permission);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_STATE_PERMISSIONS_H_
