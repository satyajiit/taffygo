// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_POLICY_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_POLICY_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "taffy/services/core/service_bridge_policy_ffi.rs.h"

namespace taffy::core_service_internal {

std::optional<core_bridge::BridgePolicyRequest> ToBridgePolicyRequest(
    const core_service::mojom::PolicyEvaluationRequest &request);
core_service::mojom::PolicyEvaluationResultPtr
ToMojoPolicyResult(core_bridge::BridgePolicyResult result);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_POLICY_H_
