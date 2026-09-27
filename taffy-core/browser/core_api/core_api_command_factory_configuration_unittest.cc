// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class ConfigurationEntropy final : public CoreApiEntropySource {
 public:
  std::string NewOpaqueId(std::string_view domain) override {
    return std::string(domain) + "-fixed";
  }

  std::array<uint8_t, 32> NewTaskSeed() override { return {}; }
};

TEST(CoreApiCommandFactoryConfigurationTest,
     WholeRecordCrossesBothContracts) {
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<ConfigurationEntropy>());
  const std::optional<ProjectedCoreCommand> projected =
      factory.BuildSetAssistantConfiguration(
          7u,
          {core_api::mojom::AssistantAbilityView::kForm,
           core_api::mojom::AssistantAbilityView::kDownloads},
          core_api::mojom::PersonalityPresetView::kQuickShopper, 2u, 0u, 1u,
          9u, 100u);

  ASSERT_TRUE(projected);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kSetAssistantConfiguration,
            projected->core_api_command->kind);
  ASSERT_TRUE(projected->core_service_command->set_assistant_configuration);
  EXPECT_EQ(
      core_service::mojom::CoreServiceCommandKind::kSetAssistantConfiguration,
      projected->core_service_command->kind);
  EXPECT_EQ(7u, projected->core_service_command
                    ->set_assistant_configuration->expected_revision);
  EXPECT_EQ(
      (std::vector<core_service::mojom::AssistantAbility>{
          core_service::mojom::AssistantAbility::kForm,
          core_service::mojom::AssistantAbility::kDownloads}),
      projected->core_service_command->set_assistant_configuration
          ->disabled_abilities);
}

TEST(CoreApiCommandFactoryConfigurationTest,
     UnsortedOrOutOfRangeValuesAreRefused) {
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<ConfigurationEntropy>());
  EXPECT_FALSE(factory.BuildSetAssistantConfiguration(
      0u,
      {core_api::mojom::AssistantAbilityView::kDownloads,
       core_api::mojom::AssistantAbilityView::kForm},
      core_api::mojom::PersonalityPresetView::kCarefulResearcher, 0u, 1u, 0u,
      9u, 100u));
  EXPECT_FALSE(factory.BuildSetAssistantConfiguration(
      0u, {}, core_api::mojom::PersonalityPresetView::kCarefulResearcher, 3u,
      1u, 0u, 9u, 100u));
}

}  // namespace
}  // namespace taffy
