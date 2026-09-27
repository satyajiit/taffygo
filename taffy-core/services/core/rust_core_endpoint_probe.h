// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_ENDPOINT_PROBE_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_ENDPOINT_PROBE_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_provider_ffi.rs.h"

namespace taffy::core_service_internal {

// One probe of an address a person typed, rebuilt into the typed envelope the
// browser's prober carries out (decision 0096).
//
// Total, like the listing fetch's projection and unlike the key probe's, and
// for the same reason: this effect rides the batch that also carries the
// published state of the flight it claimed. A refusal that cleared the batch
// would turn an accepted probe into an invalid command while the flight stayed
// claimed, and the person would watch a sheet that never stops testing. Every
// field is either copied or a closed enumeration the Rust side wrote from the
// contract's own type, so there is nothing here to disagree with.
core_service::mojom::EffectEnvelopePtr ToMojoEndpointProbeEffect(
    const core_bridge::BridgeEndpointProbeEffect &effect);

// What the browser's prober found, flattened for the bridge. Absent when the
// result is not an endpoint-probe result or does not carry the body its kind
// names; a mismatch is refused rather than repaired.
//
// Claimed by kind rather than by identity, because this kind belongs to one
// plane and to no other. The key probe's terminal cannot be confused with it:
// that one is a MODEL_REQUEST result, and this one names itself.
std::optional<core_bridge::BridgeEndpointProbeResult>
ToBridgeEndpointProbeResult(const core_service::mojom::EffectResult &result);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_ENDPOINT_PROBE_H_
