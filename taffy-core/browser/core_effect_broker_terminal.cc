// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_broker_terminal.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

// The element count of every digest the core-service contract declares as
// `array<uint8, 32>`. Stated once here because mojo's generated C++ carries a
// fixed-size array as a plain `std::vector`, so the count is not something a
// caller can read back off the type it is filling.
constexpr size_t kAssetDigestBytes = 32u;

} // namespace

// `observed_digest` is `array<uint8, 32>` and is filled here for the reason a
// fixed-size array is unlike every other field on this record: mojo checks the
// element count while it serialises the *outgoing* message, so a report that
// left it default-constructed was not a report the core rejected - it was a
// `LOG(FATAL)` in the browser process, on the sending side, with the whole
// application going down with it. Nothing was transferred, so the honest
// digest of nothing is thirty-two zero bytes, and the outcome beside it is
// what says the artifact never arrived.
mojom::ToolTerminalStatus ToolTerminalStatusFor(mojom::EffectStatus status) {
  switch (status) {
  case mojom::EffectStatus::kCompleted:
    return mojom::ToolTerminalStatus::kCompleted;
  case mojom::EffectStatus::kCancelled:
    return mojom::ToolTerminalStatus::kCancelled;
  case mojom::EffectStatus::kDeadlineExceeded:
    return mojom::ToolTerminalStatus::kDeadlineExceeded;
  case mojom::EffectStatus::kResourceLimit:
    return mojom::ToolTerminalStatus::kResourceLimit;
  case mojom::EffectStatus::kUnavailable:
    return mojom::ToolTerminalStatus::kRuntimeCrashed;
  case mojom::EffectStatus::kDenied:
    return mojom::ToolTerminalStatus::kUnsupported;
  case mojom::EffectStatus::kOutcomeUnknown:
    return mojom::ToolTerminalStatus::kOutcomeUnknown;
  case mojom::EffectStatus::kInvalidResult:
    return mojom::ToolTerminalStatus::kInvalidInput;
  }
  return mojom::ToolTerminalStatus::kInvalidInput;
}

mojom::EffectStatus EffectStatusFor(mojom::ToolTerminalStatus status) {
  switch (status) {
  case mojom::ToolTerminalStatus::kCompleted:
    return mojom::EffectStatus::kCompleted;
  case mojom::ToolTerminalStatus::kCancelled:
    return mojom::EffectStatus::kCancelled;
  case mojom::ToolTerminalStatus::kDeadlineExceeded:
    return mojom::EffectStatus::kDeadlineExceeded;
  case mojom::ToolTerminalStatus::kResourceLimit:
    return mojom::EffectStatus::kResourceLimit;
  case mojom::ToolTerminalStatus::kRuntimeCrashed:
    return mojom::EffectStatus::kUnavailable;
  case mojom::ToolTerminalStatus::kInvalidInput:
    return mojom::EffectStatus::kInvalidResult;
  case mojom::ToolTerminalStatus::kUnsupported:
    return mojom::EffectStatus::kDenied;
  case mojom::ToolTerminalStatus::kOutcomeUnknown:
    return mojom::EffectStatus::kOutcomeUnknown;
  case mojom::ToolTerminalStatus::kModelArtifactMissing:
  case mojom::ToolTerminalStatus::kModelArtifactIncompatible:
  case mojom::ToolTerminalStatus::kLocalRuntimeUnavailable:
    return mojom::EffectStatus::kUnavailable;
  }
  return mojom::EffectStatus::kInvalidResult;
}

void PopulateAssetDeliveryTerminal(const mojom::EffectEnvelope &effect,
                                   mojom::EffectResult *result) {
  result->asset_delivery = mojom::AssetDeliveryEffectResult::New();
  if (!effect.asset_delivery) {
    return;
  }
  const mojom::AssetDeliveryEffect &request = *effect.asset_delivery;
  result->asset_delivery->operation_kind = request.operation_kind;
  if (request.fetch) {
    result->asset_delivery->transfer = mojom::AssetTransferReport::New();
    result->asset_delivery->transfer->asset_id = request.fetch->asset_id;
    result->asset_delivery->transfer->asset_revision =
        request.fetch->asset_revision;
    result->asset_delivery->transfer->outcome =
        mojom::AssetTransferOutcome::kInterrupted;
    result->asset_delivery->transfer->written_bytes = request.fetch->offset_bytes;
    result->asset_delivery->transfer->observed_bytes =
        request.fetch->offset_bytes;
    result->asset_delivery->transfer->observed_digest.assign(kAssetDigestBytes,
                                                             0u);
    return;
  }
  if (request.remove) {
    result->asset_delivery->removal = mojom::AssetRemovalReport::New();
    result->asset_delivery->removal->asset_id = request.remove->asset_id;
    result->asset_delivery->removal->asset_revision =
        request.remove->asset_revision;
  }
}

void PopulateProviderPlaneTerminal(const mojom::EffectEnvelope &effect,
                                   mojom::EffectResult *result) {
  if (effect.kind == mojom::EffectKind::kFetchProviderListing) {
    // Unavailable with an empty body, which is what the core reads as "the
    // browser could not ask". A disposition is the verdict carrier here
    // exactly as it is for the catalog fetch, so an absent listing is a
    // stated outcome rather than a missing body.
    result->provider_listing = mojom::ProviderListingFetchResult::New(
        effect.provider_listing_fetch
            ? effect.provider_listing_fetch->provider_id
            : std::string(),
        mojom::CatalogFetchDisposition::kUnavailable, std::vector<uint8_t>());
    return;
  }
  if (effect.kind == mojom::EffectKind::kProbeCustomEndpoint) {
    // Unreached, with nothing behind it. `reached` is the verdict carrier here
    // — the same role the listing's disposition plays — so a probe the broker
    // had to terminate says the address was never asked rather than arriving
    // with no body at all, which the core would read as a protocol error.
    //
    // No proved base and no models, because nothing was proved and nothing was
    // listed. Absent is the honest answer for both, and the count stays zero.
    result->custom_endpoint_probe = mojom::CustomEndpointProbeResult::New(
        effect.custom_endpoint_probe ? effect.custom_endpoint_probe->provider_id
                                     : std::string(),
        /*reached=*/false, mojom::DetectedServerPtr(), /*model_count=*/0u,
        std::vector<mojom::CustomModelSpecPtr>(),
        std::optional<std::string>());
    return;
  }
  // Delivered is false: a completion the broker had to terminate reached no
  // surface, and the core must be able to tell that from one that did.
  result->composer_completion = mojom::ComposerCompletionEffectResult::New(
      effect.composer_completion ? effect.composer_completion->request_id
                                 : std::string(),
      /*delivered=*/false);
}

} // namespace taffy
