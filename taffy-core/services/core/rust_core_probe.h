// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_PROBE_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_PROBE_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_probe_ffi.rs.h"

namespace taffy::core_service_internal {

// One key-probe model effect the ordered core composed (decision 0083),
// rebuilt from the bridge's flat record into the typed envelope the effect
// broker dispatches. Null is a refusal, not a repair: a record whose
// enumerations decode to no member, whose sizes exceed the contract, or that
// names the managed wire — a probe of the product's own service would spend
// the person's plan to test the product — must not reach the network as
// something nobody composed. The rebuilt request is probe-only by
// construction: `probe` is set, the task binding is empty, and the static
// header list is empty, because the bridge record has no field a header
// could travel in.
core_service::mojom::EffectEnvelopePtr ToMojoProbeEffect(
    const core_bridge::BridgeProbeEffect &effect);

// The terminal of one probe dispatch, flattened for the bridge. Absent when
// the result is not a model result — every other kind has its own delivery
// leg, and this one carries exactly the two facts the Rust classifier reads:
// the coarse effect status, and the provider's own HTTP status (zero when
// the provider was never reached, including a result that carries no model
// payload at all).
std::optional<core_bridge::BridgeProbeCompletion>
ToBridgeProbeCompletion(const core_service::mojom::EffectResult &result);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_PROBE_H_
