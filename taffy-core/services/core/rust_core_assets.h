// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_ASSETS_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_ASSETS_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_assets_ffi.rs.h"

namespace taffy::core_service_internal {

// The three delivery-plane commands, flattened for the bridge. Nothing here
// decides anything; a command whose body does not match its kind is refused
// rather than repaired, and the plane never sees it.
std::optional<core_bridge::BridgeAssetCommand>
ToBridgeAssetCommand(const core_service::mojom::CoreServiceCommand &command);

// The report an adapter produced, flattened for the bridge. Absent when the
// result is not a delivery result or does not carry the body its kind names.
std::optional<core_bridge::BridgeAssetReport>
ToBridgeAssetReport(const core_service::mojom::EffectResult &result);

// One delivery effect the plane wants carried out.
core_service::mojom::EffectEnvelopePtr
ToMojoAssetEffect(const core_bridge::BridgeAssetEffect &effect);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_ASSETS_H_
