// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_LISTING_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_LISTING_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_listing_ffi.rs.h"

namespace taffy::core_service_internal {

// One listing fetch the ordered core wants performed (decision 0098). It
// travels on a response rather than on a plan reply, unlike the catalog's,
// because a listing becomes possible when a person's credential goes on file
// and the browser cannot see that moment.
//
// Total, like the catalog fetch's projection and unlike the probe's: every
// field is either copied or a closed enumeration the Rust side wrote from the
// contract's own type, so there is nothing here to disagree with. That matters
// more than uniformity does, because this effect rides a batch that also
// carries the provider write's published state — a refusal that cleared the
// batch would turn an accepted credential save into an invalid command over a
// listing nobody asked for. The browser answers a listing where it arrives and
// dispatches nothing on it, so a malformed one costs an answer rather than a
// request to somewhere unintended.
core_service::mojom::EffectEnvelopePtr
ToMojoProviderListingEffect(const core_bridge::BridgeListingEffect &effect);

// The listing fetch result the browser observed, flattened for the bridge.
// Absent when the result is not a listing result or does not carry the body
// its kind names; a mismatch is refused rather than repaired.
std::optional<core_bridge::BridgeListingResult>
ToBridgeProviderListingResult(const core_service::mojom::EffectResult &result);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_LISTING_H_
