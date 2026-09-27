// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_listing.h"

#include <stdint.h>

#include <string>

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

mojom::EffectEnvelopePtr
ToMojoProviderListingEffect(const bridge::BridgeListingEffect &in) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = std::string(in.operation.operation_id);
  effect->operation->service_generation = in.operation.service_generation;
  effect->operation->task_revision = in.operation.task_revision;
  effect->operation->deadline_monotonic_ms =
      in.operation.deadline_monotonic_ms;
  effect->operation->idempotency_key =
      std::string(in.operation.idempotency_key);
  effect->effect_id = std::string(in.effect_id);
  effect->kind = mojom::EffectKind::kFetchProviderListing;
  // Read-only and safe to repeat: a listing fetched twice describes the same
  // account's models twice.
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->provider_listing_fetch = mojom::ProviderListingFetchEffect::New();
  effect->provider_listing_fetch->provider_id = std::string(in.provider_id);
  effect->provider_listing_fetch->endpoint = std::string(in.endpoint);
  effect->provider_listing_fetch->wire_api =
      static_cast<mojom::ProviderWireApi>(in.wire_api);
  // Absence and emptiness are different answers here as everywhere else: an
  // endpoint that needs no key is not an endpoint whose key is the empty
  // string, and the flag is what keeps the two apart across a bridge with no
  // optional of its own.
  if (in.has_credential_handle) {
    effect->provider_listing_fetch->credential_handle =
        std::string(in.credential_handle);
  }
  effect->provider_listing_fetch->max_response_bytes = in.max_response_bytes;
  return effect;
}

std::optional<bridge::BridgeListingResult>
ToBridgeProviderListingResult(const mojom::EffectResult &result) {
  if (!result.operation ||
      result.kind != mojom::EffectKind::kFetchProviderListing ||
      !result.provider_listing) {
    return std::nullopt;
  }
  const mojom::ProviderListingFetchResult &listing = *result.provider_listing;
  bridge::BridgeListingResult out;
  out.operation.operation_id = result.operation->operation_id;
  out.operation.service_generation = result.operation->service_generation;
  out.operation.task_revision = result.operation->task_revision;
  out.operation.deadline_monotonic_ms =
      result.operation->deadline_monotonic_ms;
  out.operation.idempotency_key = result.operation->idempotency_key;
  out.effect_id = result.effect_id;
  out.provider_id = listing.provider_id;
  out.disposition = static_cast<uint8_t>(listing.disposition);
  out.body.reserve(listing.body.size());
  for (uint8_t byte : listing.body) {
    out.body.push_back(byte);
  }
  return out;
}

} // namespace taffy::core_service_internal
