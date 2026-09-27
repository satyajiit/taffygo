// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_network_cost.h"

namespace taffy {

core_service::mojom::AssetNetworkCost MapAssetNetworkCost(
    net::NetworkChangeNotifier::ConnectionType type,
    net::NetworkChangeNotifier::ConnectionCost cost) {
  if (type == net::NetworkChangeNotifier::CONNECTION_NONE) {
    return core_service::mojom::AssetNetworkCost::kOffline;
  }
  switch (cost) {
    case net::NetworkChangeNotifier::CONNECTION_COST_UNMETERED:
      return core_service::mojom::AssetNetworkCost::kUnmetered;
    case net::NetworkChangeNotifier::CONNECTION_COST_METERED:
    case net::NetworkChangeNotifier::CONNECTION_COST_UNKNOWN:
    case net::NetworkChangeNotifier::CONNECTION_COST_LAST:
      break;
  }
  // Online, but not known to be unmetered. Report it as metered rather than
  // guess: the plane transfers either way, and the word reaches a surface.
  return core_service::mojom::AssetNetworkCost::kMetered;
}

}  // namespace taffy
