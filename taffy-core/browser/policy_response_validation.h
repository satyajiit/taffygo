// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_POLICY_RESPONSE_VALIDATION_H_
#define TAFFY_BROWSER_POLICY_RESPONSE_VALIDATION_H_

#include <optional>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Why one task-context policy request's scope was refused before the core saw
// it, or nullopt when the scope is the exact shape its operation calls for.
//
// A refusal names itself. Every shape refusal used to answer the caller one
// bare `false`, which `EvaluatePolicy` reported as `kInvalidRequest` and the
// core recorded as `Unsupported` — one word for eight different reasons, on a
// path where the only diagnosis left was decoding the journal.
std::optional<core_service::mojom::TaskActionResultCode>
TaskScopeRefusalForOperation(
    const core_service::mojom::PolicyEvaluationRequest& request);

// A discovery ask is the sole policy shape whose source is opaque. It is
// admitted only as an assistant Search or typed HTTPS Navigate from the exact
// browser-session blank to one exact destination tuple.
bool TaskDiscoveryPolicyRequestHasExactShape(
    const core_service::mojom::PolicyEvaluationRequest& request,
    std::string_view browser_session_id);

// Verifies that Rust returned exactly the authority facts the browser
// originated. The capability identity and compiled policy version are the only
// new grant facts; every other binding must be derivable from the request.
bool PolicyGrantMatchesRequest(
    const core_service::mojom::MintedCapabilityGrant& grant,
    const core_service::mojom::PolicyEvaluationRequest& request);

// Direct reads additionally require one fixed, taskless observation effect.
// No service-returned field may redirect, widen, prolong, or re-identify it.
bool DirectObservationEffectMatchesRequest(
    const core_service::mojom::EffectEnvelope& effect,
    const core_service::mojom::MintedCapabilityGrant& grant,
    const core_service::mojom::PolicyEvaluationRequest& request);

}  // namespace taffy

#endif  // TAFFY_BROWSER_POLICY_RESPONSE_VALIDATION_H_
