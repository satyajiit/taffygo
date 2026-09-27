// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <string>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

bridge::BridgeTaskEffect DiscoveryNavigatePolicy() {
  bridge::BridgeTaskEffect effect{};
  effect.operation.operation_id = "discovery-policy-operation";
  effect.operation.service_generation = 7u;
  effect.operation.task_revision = 11u;
  effect.operation.deadline_monotonic_ms = 10'000u;
  effect.operation.idempotency_key = "discovery-policy-key";
  effect.effect_id = "discovery-policy-effect";
  effect.task_id = "task-1";
  effect.kind = static_cast<uint8_t>(mojom::TaskReducerEffectKind::kAskPolicy);
  effect.action_id = "navigate-official";
  effect.action_idempotency_key = "navigate-official-key";
  effect.proposal_digest = std::string(64u, 'a');
  effect.action_class =
      static_cast<uint8_t>(mojom::PolicyActionClass::kOpenLink);
  effect.action_operation =
      static_cast<uint8_t>(mojom::TaskActionOperationKind::kNavigate);
  effect.tool_name = "browser.navigate";
  effect.tab_id = "tab-1";
  effect.has_destination_address = true;
  effect.destination_address = "https://official.test/download-document";
  effect.context_risk =
      static_cast<uint8_t>(mojom::PolicyRiskClass::kReversibleDisclosure);
  effect.control_mode =
      static_cast<uint8_t>(mojom::TaskControlMode::kAssistant);
  effect.data_classes = {
      static_cast<uint8_t>(mojom::BipSensitivity::kNotSensitive)};
  effect.policy_version = 3u;
  effect.has_policy_discovery = true;
  effect.policy_discovery_tab_id = "tab-1";
  effect.policy_discovery_browser_session_id = "session-1";
  effect.policy_discovery_remaining_new_source_cap = 1u;
  // The utility preserves these canonical Rust bytes; the browser verifies
  // their operation, exact tab/address and false new-tab flag before policy.
  for (char byte : std::string_view("taffy.action-intent.v1")) {
    effect.canonical_intent.push_back(static_cast<uint8_t>(byte));
  }
  auto field = [&](uint8_t tag, std::string_view value) {
    effect.canonical_intent.push_back(tag);
    for (size_t index = 0u; index < 8u; ++index) {
      effect.canonical_intent.push_back(
          static_cast<uint8_t>(value.size() >> (index * 8u)));
    }
    for (char byte : value) {
      effect.canonical_intent.push_back(static_cast<uint8_t>(byte));
    }
  };
  constexpr char kFalseAndNavigate = '\0';
  field(0u, std::string_view(&kFalseAndNavigate, 1u));
  field(1u, "tab-1");
  field(2u, "https://official.test/download-document");
  field(3u, std::string_view(&kFalseAndNavigate, 1u));
  return effect;
}

TEST(RustCoreDiscoveryPolicyConversionTest,
     TypedNavigatePreservesExactBoundedDiscoveryFact) {
  const auto input = DiscoveryNavigatePolicy();
  const auto binding = core_service_internal::ToMojoTaskEffect(input);
  ASSERT_TRUE(binding);
  ASSERT_TRUE((*binding)->policy);
  const auto& effect = *(*binding)->policy;
  EXPECT_EQ(effect.operation_kind, mojom::TaskActionOperationKind::kNavigate);
  EXPECT_EQ(effect.action_class, mojom::PolicyActionClass::kOpenLink);
  EXPECT_EQ(effect.destination_address,
            "https://official.test/download-document");
  EXPECT_FALSE(effect.transient_search_query);
  ASSERT_TRUE(effect.discovery);
  EXPECT_EQ(effect.discovery->discovery_tab_id, "tab-1");
  EXPECT_EQ(effect.discovery->browser_session_id, "session-1");
  EXPECT_EQ(effect.discovery->remaining_new_source_cap, 1u);
  ASSERT_EQ(effect.canonical_intent.size(), input.canonical_intent.size());
  for (size_t index = 0u; index < input.canonical_intent.size(); ++index) {
    EXPECT_EQ(effect.canonical_intent[index], input.canonical_intent[index]);
  }
}

TEST(RustCoreDiscoveryPolicyConversionTest,
     DiscoveryRejectsUnrelatedOperationsClassesAndIncompleteAuthority) {
  for (int mutation = 0; mutation < 10; ++mutation) {
    SCOPED_TRACE(mutation);
    auto input = DiscoveryNavigatePolicy();
    switch (mutation) {
      case 0:
        input.action_operation =
            static_cast<uint8_t>(mojom::TaskActionOperationKind::kTabsOpen);
        break;
      case 1:
        input.action_class =
            static_cast<uint8_t>(mojom::PolicyActionClass::kCreateTaskTab);
        break;
      case 2:
        input.policy_discovery_tab_id = "other-tab";
        break;
      case 3:
        input.policy_discovery_remaining_new_source_cap = 0u;
        break;
      case 4:
        input.policy_discovery_remaining_new_source_cap =
            mojom::kMaxNewSourceCap + 1u;
        break;
      case 5:
        input.policy_discovery_browser_session_id = "";
        break;
      case 6:
        input.principal =
            static_cast<uint8_t>(mojom::PolicyPrincipalKind::kSkill);
        input.principal_id = "skill-version-1";
        break;
      case 7:
        input.has_node_id = true;
        input.node_id = "node-1";
        break;
      case 8:
        input.control_mode =
            static_cast<uint8_t>(mojom::TaskControlMode::kShared);
        break;
      case 9:
        input.has_destination_address = false;
        input.destination_address = "";
        break;
    }
    EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(input));
  }
}

}  // namespace
}  // namespace taffy
