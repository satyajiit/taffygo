// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

bridge::BridgeTaskEffect FormApprovalBinding() {
  bridge::BridgeTaskEffect effect{};
  effect.operation.operation_id = "form-approval-operation";
  effect.operation.service_generation = 7u;
  effect.operation.task_revision = 3u;
  effect.operation.deadline_monotonic_ms = 10'000u;
  effect.operation.idempotency_key = "form-approval-idempotency";
  effect.effect_id = "form-approval-effect";
  effect.task_id = "task-1";
  effect.ordinal = 1u;
  effect.kind =
      static_cast<uint8_t>(mojom::TaskReducerEffectKind::kRequestApproval);
  effect.action_id = "fill-name";
  effect.proposal_digest = std::string(64u, 'a');
  effect.action_class =
      static_cast<uint8_t>(mojom::PolicyActionClass::kFillField);
  effect.action_operation =
      static_cast<uint8_t>(mojom::TaskActionOperationKind::kFormFill);
  effect.tool_name = "browser.form.fill";
  effect.canonical_intent = {0x10u, 0x20u, 0x30u};
  effect.tab_id = "tab-1";
  effect.has_node_id = true;
  effect.node_id = "field-1";
  effect.action_input_kind =
      static_cast<uint8_t>(mojom::TaskActionInputKind::kSuppliedValue);
  effect.has_supplied_value = true;
  effect.supplied_value_request_id = "values-1";
  effect.supplied_value_index = 1u;
  return effect;
}

TEST(RustCoreFormApprovalConversionTest,
     ExactExecutableMetadataCrossesWithoutAValue) {
  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(FormApprovalBinding());

  ASSERT_TRUE(projected);
  ASSERT_TRUE(*projected);
  ASSERT_TRUE((*projected)->approval);
  EXPECT_EQ("fill-name", (*projected)->approval->action_id);
  EXPECT_EQ(std::string(64u, 'a'), (*projected)->approval->proposal_digest);
  ASSERT_TRUE((*projected)->approval->form_action);
  const mojom::TaskExecutableAction& action =
      *(*projected)->approval->form_action;
  EXPECT_EQ(mojom::PolicyActionClass::kFillField, action.action_class);
  EXPECT_EQ(mojom::TaskActionOperationKind::kFormFill,
            action.operation_kind);
  EXPECT_EQ("browser.form.fill", action.tool_name);
  EXPECT_EQ("tab-1", action.tab_id);
  EXPECT_EQ(std::optional<std::string>("field-1"), action.node_id);
  EXPECT_EQ(std::vector<uint8_t>({0x10u, 0x20u, 0x30u}),
            action.canonical_intent);
  ASSERT_TRUE(action.input);
  ASSERT_TRUE(action.input->supplied_value);
  EXPECT_EQ("values-1", action.input->supplied_value->request_id);
  EXPECT_EQ(1u, action.input->supplied_value->index);
  EXPECT_FALSE(action.destination_origin);
  EXPECT_FALSE(action.destination_address);
  EXPECT_FALSE(action.operand_handle);
}

TEST(RustCoreFormApprovalConversionTest,
     FormMetadataCannotSmuggleADestination) {
  bridge::BridgeTaskEffect effect = FormApprovalBinding();
  effect.has_destination_address = true;
  effect.destination_address = "https://other.test/submit";
  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreFormApprovalConversionTest,
     SuppliedPositionMustBeCompleteAndBounded) {
  bridge::BridgeTaskEffect effect = FormApprovalBinding();
  effect.has_supplied_value = false;
  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));

  effect = FormApprovalBinding();
  effect.supplied_value_index = mojom::kMaxTaskSuppliedValues;
  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreFormApprovalConversionTest,
     GenericApprovalDoesNotInventExecutableMetadata) {
  bridge::BridgeTaskEffect effect = FormApprovalBinding();
  effect.action_class =
      static_cast<uint8_t>(mojom::PolicyActionClass::kSyntheticClick);
  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(effect);
  ASSERT_TRUE(projected);
  ASSERT_TRUE(*projected);
  ASSERT_TRUE((*projected)->approval);
  EXPECT_FALSE((*projected)->approval->form_action);
}

}  // namespace
}  // namespace taffy
