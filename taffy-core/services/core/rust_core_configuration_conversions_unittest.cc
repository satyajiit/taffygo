// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_command_conversions.h"
#include "taffy/services/core/rust_core_response.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bridge::BridgeStorageEffect AssistantConfigurationEffect() {
  bridge::BridgeStorageEffect effect{};
  effect.operation.operation_id = "assistant-configuration-operation-1";
  effect.operation.service_generation = 1u;
  effect.operation.deadline_monotonic_ms = 10'000u;
  effect.operation.idempotency_key = "assistant-configuration-key-1";
  effect.effect_id = "assistant-configuration-effect-1";
  effect.operation_kind = 8u;  // SET_ASSISTANT_CONFIGURATION
  effect.expected_revision = 2u;
  effect.resulting_revision = 3u;
  effect.configuration_disabled_abilities = {6u, 7u};  // FORM, DOWNLOADS
  effect.configuration_preset = 1u;  // QUICK_SHOPPER
  effect.configuration_pace = 2u;
  effect.configuration_length = 0u;
  effect.configuration_check_in = 1u;
  return effect;
}

TEST(RustCoreConfigurationConversionTest,
     StorageEffectProjectsAsWholeCompareAndSet) {
  mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoStorageEffect(
          AssistantConfigurationEffect());

  ASSERT_TRUE(projected);
  ASSERT_TRUE(projected->storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kSetAssistantConfiguration,
            projected->storage_commit->operation_kind);
  EXPECT_EQ(2u, projected->storage_commit->expected_revision);
  EXPECT_EQ(3u, projected->storage_commit->resulting_revision);
  ASSERT_TRUE(projected->storage_commit->assistant_configuration);
  EXPECT_EQ(
      (std::vector<mojom::AssistantAbility>{mojom::AssistantAbility::kForm,
                                            mojom::AssistantAbility::kDownloads}),
      projected->storage_commit->assistant_configuration
          ->disabled_abilities);
  EXPECT_EQ(mojom::PersonalityPreset::kQuickShopper,
            projected->storage_commit->assistant_configuration->preset);
}

TEST(RustCoreConfigurationConversionTest, UnknownAbilityIsRefused) {
  bridge::BridgeStorageEffect effect = AssistantConfigurationEffect();
  effect.configuration_disabled_abilities = {16u};
  EXPECT_FALSE(core_service_internal::ToMojoStorageEffect(effect));
}

TEST(RustCoreConfigurationConversionTest, UnknownPresetIsRefused) {
  bridge::BridgeStorageEffect effect = AssistantConfigurationEffect();
  effect.configuration_preset = 3u;
  EXPECT_FALSE(core_service_internal::ToMojoStorageEffect(effect));
}

TEST(RustCoreConfigurationConversionTest,
     BootstrapCarriesDurableConfigurationWithoutDefaults) {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = 4u;
  bootstrap->generation_capability_entropy.assign(32u, 0x5au);
  bootstrap->assistant_configuration = mojom::AssistantConfiguration::New(
      9u,
      std::vector<mojom::AssistantAbility>{mojom::AssistantAbility::kForm},
      mojom::PersonalityPreset::kTripPlanner, 1u, 2u, 1u);

  std::optional<bridge::BridgeBootstrap> projected =
      core_service_internal::ToBridgeBootstrap(*bootstrap);
  ASSERT_TRUE(projected);
  EXPECT_TRUE(projected->has_assistant_configuration);
  EXPECT_EQ(9u, projected->assistant_configuration_revision);
  EXPECT_EQ((std::vector<uint8_t>{6u}),
            std::vector<uint8_t>(
                projected->assistant_configuration_disabled_abilities.begin(),
                projected->assistant_configuration_disabled_abilities.end()));
  EXPECT_EQ(2u, projected->assistant_configuration_preset);
}

TEST(RustCoreConfigurationConversionTest,
     CommandCarriesWholeConfigurationAndOperation) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "operation-configuration-1", 3u, 0u, 10'000u,
      "configuration-idempotency-1");
  command->kind = mojom::CoreServiceCommandKind::kSetAssistantConfiguration;
  command->set_assistant_configuration =
      mojom::SetAssistantConfigurationCommand::New(
          2u,
          std::vector<mojom::AssistantAbility>{
              mojom::AssistantAbility::kDownloads},
          mojom::PersonalityPreset::kQuickShopper, 2u, 0u, 1u);

  std::optional<bridge::BridgeAssistantConfigurationCommand> projected =
      core_service_internal::ToBridgeAssistantConfiguration(*command);
  ASSERT_TRUE(projected);
  EXPECT_EQ("operation-configuration-1",
            std::string(projected->operation.operation_id));
  EXPECT_EQ(2u, projected->expected_revision);
  ASSERT_EQ(1u, projected->disabled_abilities.size());
  EXPECT_EQ(7u, projected->disabled_abilities.front());
  EXPECT_EQ(1u, projected->preset);
}

}  // namespace
}  // namespace taffy
