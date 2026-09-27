// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_ASSET_VALIDATION_H_
#define TAFFY_BROWSER_CORE_ASSET_VALIDATION_H_

#include <stddef.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// Whether a delivery effect the core proposed is one the browser may carry
// out. Structure only: the path is checked here for shape, and again by
// `AssetUrl` against the pinned origin before anything is sent.
bool IsValidCoreAssetDelivery(
    const core_service::mojom::AssetDeliveryEffect& effect,
    size_t max_identifier_bytes);

// Whether the report an adapter produced answers the effect it was given.
// The identity must be the one asked about, because a report for a different
// asset would advance the wrong row's state machine.
bool IsValidCoreAssetDeliveryResult(
    const core_service::mojom::AssetDeliveryEffectResult& result,
    const core_service::mojom::AssetDeliveryEffect& effect);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_ASSET_VALIDATION_H_
