// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

class FixedEntropy final : public CoreApiEntropySource {
public:
  std::string NewOpaqueId(std::string_view domain) override {
    return std::string(domain) + "-fixed";
  }

  std::array<uint8_t, 32> NewTaskSeed() override {
    std::array<uint8_t, 32> seed{};
    seed.fill(0x5a);
    return seed;
  }
};

CoreApiCommandFactory NewFactory() {
  return CoreApiCommandFactory("profile-fixed",
                               std::make_unique<FixedEntropy>());
}

TEST(CoreApiAssetCommandTest, ARequestProjectsToTheDeliveryCommandUnchanged) {
  CoreApiCommandFactory factory = NewFactory();
  const auto projected =
      factory.BuildRequestAsset("python-stdlib", "3.14.1-1", 7u, 1000u);
  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->core_api_command->kind, api::CoreCommandKind::kRequestAsset);
  ASSERT_TRUE(projected->core_api_command->request_asset);
  EXPECT_EQ(projected->core_api_command->request_asset->asset_id,
            "python-stdlib");
  ASSERT_TRUE(projected->core_service_command->request_asset);
  EXPECT_EQ(projected->core_service_command->kind,
            service::CoreServiceCommandKind::kRequestAsset);
  EXPECT_EQ(projected->core_service_command->request_asset->asset_revision,
            "3.14.1-1");
  EXPECT_EQ(projected->core_service_command->operation->service_generation, 7u);
}

TEST(CoreApiAssetCommandTest, ARemovalCarriesTheRevisionItWasAskedFor) {
  CoreApiCommandFactory factory = NewFactory();
  const auto projected =
      factory.BuildRemoveAsset("python-packages", "2026-08-1", 7u, 1000u);
  ASSERT_TRUE(projected);
  ASSERT_TRUE(projected->core_service_command->remove_asset);
  EXPECT_EQ(projected->core_service_command->kind,
            service::CoreServiceCommandKind::kRemoveAsset);
  EXPECT_EQ(projected->core_service_command->remove_asset->asset_id,
            "python-packages");
  EXPECT_EQ(projected->core_service_command->remove_asset->asset_revision,
            "2026-08-1");
}

// A policy command names the Core Service kind whose name differs from the
// Core API one. The two contracts named the same intent differently, and the
// place that has to know is here.
TEST(CoreApiAssetCommandTest, APolicyCommandCrossesToTheNameTheServiceUses) {
  CoreApiCommandFactory factory = NewFactory();
  const auto projected = factory.BuildSetAssetPolicy(
      api::AssetNetworkCostView::kUnmetered, true, 7u, 1000u);
  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->core_api_command->kind,
            api::CoreCommandKind::kSetAssetPolicy);
  EXPECT_EQ(projected->core_service_command->kind,
            service::CoreServiceCommandKind::kSetAssetDeliveryPolicy);
  ASSERT_TRUE(projected->core_service_command->set_asset_delivery_policy);
  EXPECT_EQ(projected->core_service_command->set_asset_delivery_policy->network_cost,
            service::AssetNetworkCost::kUnmetered);
  EXPECT_TRUE(
      projected->core_service_command->set_asset_delivery_policy->metered_permitted);
}

TEST(CoreApiAssetCommandTest, AnEmptyIdentityNeverBecomesACommand) {
  CoreApiCommandFactory factory = NewFactory();
  EXPECT_FALSE(factory.BuildRequestAsset("", "3.14.1-1", 7u, 1000u));
  EXPECT_FALSE(factory.BuildRequestAsset("python-stdlib", "", 7u, 1000u));
  EXPECT_FALSE(factory.BuildRemoveAsset("", "", 7u, 1000u));
}

TEST(CoreApiAssetCommandTest, AnIdentityPastTheContractBoundIsRefusedHere) {
  CoreApiCommandFactory factory = NewFactory();
  const std::string oversized(api::kMaxIdentifierBytes + 1u, 'a');
  EXPECT_FALSE(factory.BuildRequestAsset(oversized, "3.14.1-1", 7u, 1000u));
  EXPECT_FALSE(factory.BuildRemoveAsset("python-stdlib", oversized, 7u, 1000u));
}

} // namespace
} // namespace taffy
