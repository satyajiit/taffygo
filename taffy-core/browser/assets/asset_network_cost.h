// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_ASSET_NETWORK_COST_H_
#define TAFFY_BROWSER_ASSETS_ASSET_NETWORK_COST_H_

#include "net/base/network_change_notifier.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// The delivery plane's three costs, from what Chromium can see of the link.
//
// The plane fetches on any live connection — offline is the only wait
// (asset-plane `DevicePolicy::may_transfer`) — so the cost decides nothing
// today; it is carried for the wire and for a surface to name. It still has
// to be honest: unknown is not unmetered, and no connection is offline even
// if a stale cost is still sitting on the notifier.
core_service::mojom::AssetNetworkCost MapAssetNetworkCost(
    net::NetworkChangeNotifier::ConnectionType type,
    net::NetworkChangeNotifier::ConnectionCost cost);

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_ASSET_NETWORK_COST_H_
