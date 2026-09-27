// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_RESPONSE_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_RESPONSE_H_

#include <optional>

#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy::core_service_internal {

// Turns one bridge answer into the Mojo batch the service publishes.
//
// This is projection and nothing else: every effect the core proposed becomes
// an envelope, every state becomes a publication, and a record that does not
// project clears the batch rather than shipping half of it — a caller that
// received three of four effects would be a caller acting on a decision the
// core did not take.
//
// It lives beside `RustCore` rather than inside it because the two are
// different jobs. `rust_core.cc` decides which bridge entry point a message
// belongs to; this decides what the answer looks like in Mojo, and it grows
// with the contract while the routing grows with the entry points.
CoreResponseBatch ToResponseBatch(core_bridge::BridgeResponse response);

std::optional<core_bridge::BridgeModelStreamChunk> ToBridgeModelStreamChunk(
    const core_service::mojom::ModelStreamChunk& chunk);
CoreModelStreamDelivery ToModelStreamDelivery(
    core_bridge::BridgeModelStreamDelivery delivery);

// One storage effect, projected. Named here as well as used above because
// bootstrap can emit task commits and run-ledger writes outside a response,
// and a second copy of this projection is a second thing that can disagree
// with the contract.
core_service::mojom::EffectEnvelopePtr
ToMojoStorageEffect(const core_bridge::BridgeStorageEffect &effect);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_RESPONSE_H_
