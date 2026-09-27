// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_PROVIDER_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_PROVIDER_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_provider_ffi.rs.h"

namespace taffy::core_service_internal {

// The five provider-plane commands, flattened for the bridge. Nothing here
// decides anything: a command whose body does not match its kind is refused
// rather than repaired, and the plane never sees it.
//
// `credential_handle` is carried across as an opaque string and is never key
// material — the contract has no field that could hold one (decision 0049), so
// there is nothing here to zero on a refusal path the way the account seam's
// transient material must be.
std::optional<core_bridge::BridgeProviderCommand>
ToBridgeProviderCommand(const core_service::mojom::CoreServiceCommand &command);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_PROVIDER_H_
