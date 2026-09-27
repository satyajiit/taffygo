// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_network_cost.h"

#include "net/base/network_change_notifier.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using net::NetworkChangeNotifier;
using Cost = core_service::mojom::AssetNetworkCost;

TEST(AssetNetworkCostTest, NoLinkIsOfflineRegardlessOfCost) {
  EXPECT_EQ(Cost::kOffline,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_NONE,
                NetworkChangeNotifier::CONNECTION_COST_UNMETERED));
  EXPECT_EQ(Cost::kOffline,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_NONE,
                NetworkChangeNotifier::CONNECTION_COST_METERED));
}

TEST(AssetNetworkCostTest, UnmeteredWifiIsUnmetered) {
  EXPECT_EQ(Cost::kUnmetered,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_WIFI,
                NetworkChangeNotifier::CONNECTION_COST_UNMETERED));
}

TEST(AssetNetworkCostTest, UnmeteredEthernetIsUnmetered) {
  EXPECT_EQ(Cost::kUnmetered,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_ETHERNET,
                NetworkChangeNotifier::CONNECTION_COST_UNMETERED));
}

TEST(AssetNetworkCostTest, CellularIsMetered) {
  EXPECT_EQ(Cost::kMetered,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_4G,
                NetworkChangeNotifier::CONNECTION_COST_METERED));
  EXPECT_EQ(Cost::kMetered,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_5G,
                NetworkChangeNotifier::CONNECTION_COST_METERED));
}

TEST(AssetNetworkCostTest, UnknownCostIsMeteredNotUnmetered) {
  EXPECT_EQ(Cost::kMetered,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_WIFI,
                NetworkChangeNotifier::CONNECTION_COST_UNKNOWN));
  EXPECT_EQ(Cost::kMetered,
            MapAssetNetworkCost(
                NetworkChangeNotifier::CONNECTION_UNKNOWN,
                NetworkChangeNotifier::CONNECTION_COST_UNKNOWN));
}

}  // namespace
}  // namespace taffy
