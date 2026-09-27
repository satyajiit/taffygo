// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_ACCOUNT_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_ACCOUNT_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_account_ffi.rs.h"

namespace taffy::core_service_internal {

std::optional<core_bridge::BridgeAccountCommand>
ToBridgeAccountCommand(const core_service::mojom::CoreServiceCommand &command);

std::optional<core_bridge::BridgeAccountCompletion>
ToBridgeAccountCompletion(core_service::mojom::EffectResult *result);

std::optional<core_bridge::BridgeAccountTokenValidationRequest>
ToBridgeAccountTokenValidationRequest(
    core_service::mojom::AccountTokenValidationRequest *request);

core_service::mojom::AccountTokenValidationResultPtr
ToMojoAccountTokenValidationResult(
    core_bridge::BridgeAccountTokenValidationResult result);

core_service::mojom::EffectEnvelopePtr
ToMojoAccountEffect(const core_bridge::BridgeAccountEffect &effect);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_ACCOUNT_H_
