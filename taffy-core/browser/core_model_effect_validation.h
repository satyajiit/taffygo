// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_MODEL_EFFECT_VALIDATION_H_
#define TAFFY_BROWSER_CORE_MODEL_EFFECT_VALIDATION_H_

#include <stddef.h>

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// The register decision 0096 section 1 puts in the browser: the exact address
// a person saved for one provider, or nothing when that provider has none.
// `taffy/browser/model/custom_provider_endpoint_store.h` is what answers it.
using RegisteredEndpointLookup =
    base::RepeatingCallback<std::optional<std::string>(
        const std::string& provider_id)>;

// Whether `value` is the one canonical spelling of an HTTPS origin. This is
// the common address gate for model calls and authenticated provider-listing
// fetches: the sandbox may choose an origin, while only compiled browser code
// may choose a path beneath it.
bool IsCanonicalHttpsProviderOrigin(const std::string& value);

// Whether a model request the sandboxed core proposed is one this browser may
// send. It lives in its own file because two of its rules are about a string
// the core chose and the browser must not trust: where the request goes, and
// what travels with it.
//
// The effect is refused whole rather than repaired. A repaired one would be a
// request nobody proposed, sent to a provider on a person's credential, and
// the record of what happened would name the plan rather than the request.
//
// **This is the form a caller that is about to send must ask.** A request
// claiming a person's own address is accepted only when `registered_endpoint`
// answers with that exact string for that request's provider; a null lookup
// therefore refuses every such request, because the register is the only
// authority that can accept one and a caller that cannot reach it has no
// second way to decide.
bool IsValidCoreModelRequest(
    const core_service::mojom::ModelRequestEffect& effect,
    size_t max_identifier_bytes,
    size_t max_effect_bytes,
    const RegisteredEndpointLookup& registered_endpoint);

// The same rules, asked by a caller that holds no register.
//
// It answers everything the sender's form answers except which of a person's
// addresses is theirs: a request claiming one is bounded here and settled
// where it is sent. That is the shape the managed route already has — this
// file accepts any https origin for a managed request and the broker refuses
// any that is not the compiled worker origin — and it is why an upstream
// envelope check does not need the profile's preference file in reach.
//
// Nothing sends on this answer alone.
bool IsValidCoreModelRequest(
    const core_service::mojom::ModelRequestEffect& effect,
    size_t max_identifier_bytes,
    size_t max_effect_bytes);

// Whether an endpoint probe the core proposed is one this browser may perform
// (decision 0096 section 5).
//
// It lives beside the model request rather than with the generic effect rules
// because it is the same question asked of the same kind of string: an address
// a person typed, which this process is about to make a request to. What it
// does *not* ask is the register question — a probe is asked before a provider
// exists, so there is no row for it to be recognized against, and the address
// policy has already been applied where the command was accepted.
//
// The response ceiling is the core's own request and is checked against the
// contract's limit rather than replaced by a browser default, so a core asking
// for more than a listing may be is refused rather than quietly clamped.
bool IsValidCustomEndpointProbe(
    const core_service::mojom::CustomEndpointProbeEffect& effect);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_MODEL_EFFECT_VALIDATION_H_
