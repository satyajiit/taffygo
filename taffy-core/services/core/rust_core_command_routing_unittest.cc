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
#include "taffy/services/core/rust_core_provider.h"
#include "taffy/services/core/rust_core_task.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: the two command kinds whose whole implementation existed
// on both sides of this seam while the seam itself refused them. Each
// projection switches over the closed enumeration with no `default`, so a kind
// it does not carry sits in a run of case labels answering `std::nullopt` —
// and a projection answering `std::nullopt` is `kInvalidCommand` at the
// browser. That refusal compiles, links and passes every other suite, because
// the switch is still exhaustive and the fallthrough is still well formed.
//
// `taffy-core/services/core/tools/check_command_dispatch_coverage.py` is the
// static half of the same claim and catches a kind routed to a plane that
// refuses it. These cases are the other half: that the plane which now claims
// a kind actually carries its body across, field by field, rather than
// accepting the command and delivering an empty one.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 4u;
constexpr uint64_t kDeadlineMillis = 90'000u;

mojom::CoreServiceCommandPtr CommandOfKind(
    mojom::CoreServiceCommandKind kind) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "operation-1", kGeneration, 3u, kDeadlineMillis, "idempotency-1");
  command->kind = kind;
  return command;
}

mojom::CoreServiceCommandPtr SupplyFieldValues(uint32_t supplied) {
  mojom::CoreServiceCommandPtr command =
      CommandOfKind(mojom::CoreServiceCommandKind::kSupplyFieldValues);
  std::vector<std::string> fields;
  for (uint32_t index = 0u; index < supplied; ++index) {
    fields.push_back("field-" + std::to_string(index));
  }
  command->supply_field_values = mojom::SupplyFieldValuesCommand::New(
      "task-1", "field-request-1", supplied, "supply-trace",
      mojom::FieldValueAskOutcome::kAnswered, std::move(fields));
  return command;
}

mojom::CoreServiceCommandPtr SetModelPreference(
    std::optional<std::string> model_id,
    mojom::ThinkingPreferencePtr thinking) {
  mojom::CoreServiceCommandPtr command = CommandOfKind(
      mojom::CoreServiceCommandKind::kSetProviderModelPreference);
  command->set_provider_model_preference =
      mojom::SetProviderModelPreferenceCommand::New(
          "provider-1", std::move(model_id), std::move(thinking));
  return command;
}

TEST(RustCoreCommandRoutingTest, SuppliedFieldValuesCrossWithTheirCount) {
  const std::optional<core_bridge::BridgeTaskCommand> projected =
      core_service_internal::ToBridgeTaskCommand(*SupplyFieldValues(3u));

  ASSERT_TRUE(projected)
      << "the task plane refused kSupplyFieldValues, so every field-value "
         "answer is kInvalidCommand and the task waits forever";
  EXPECT_EQ(static_cast<uint8_t>(
                mojom::CoreServiceCommandKind::kSupplyFieldValues),
            projected->kind);
  EXPECT_EQ("task-1", std::string(projected->task_id));
  EXPECT_EQ("field-request-1", std::string(projected->request_id));
  // The count is the whole payload. What a person typed stays in the browser's
  // vault and is spent there (decision 0063), so a projection that carried the
  // command and left this at zero would deliver an answer of no values to a
  // request that is waiting for some.
  EXPECT_EQ(3u, projected->supplied);
  // And the field each one was minted for, in position order, which is what
  // lets the task fill them without a model turn (decision 0238).
  ASSERT_EQ(3u, projected->supplied_field_node_ids.size());
  EXPECT_EQ("field-0", std::string(projected->supplied_field_node_ids[0]));
  EXPECT_EQ("field-2", std::string(projected->supplied_field_node_ids[2]));
  EXPECT_EQ("supply-trace", std::string(projected->trace_id));
}

TEST(RustCoreCommandRoutingTest, SuppliedFieldValuesWithNoBodyAreRefused) {
  mojom::CoreServiceCommandPtr command =
      CommandOfKind(mojom::CoreServiceCommandKind::kSupplyFieldValues);

  EXPECT_FALSE(core_service_internal::ToBridgeTaskCommand(*command));
}

TEST(RustCoreCommandRoutingTest, AModelChoiceCrossesWithBothOfItsHalves) {
  auto thinking = mojom::ThinkingPreference::New();
  thinking->level = mojom::ThinkingLevel::kHigh;
  const std::optional<core_bridge::BridgeProviderCommand> projected =
      core_service_internal::ToBridgeProviderCommand(
          *SetModelPreference("model-1", std::move(thinking)));

  ASSERT_TRUE(projected)
      << "the provider plane refused kSetProviderModelPreference, so a "
         "standing model choice never reaches the core that routes with it";
  EXPECT_EQ(static_cast<uint8_t>(
                mojom::CoreServiceCommandKind::kSetProviderModelPreference),
            projected->kind);
  EXPECT_EQ("provider-1", std::string(projected->provider_id));
  EXPECT_TRUE(projected->has_model_id);
  EXPECT_EQ("model-1", std::string(projected->model_id));
  EXPECT_TRUE(projected->has_thinking_level);
  EXPECT_EQ(static_cast<uint8_t>(mojom::ThinkingLevel::kHigh),
            projected->thinking_level);
}

TEST(RustCoreCommandRoutingTest, AChoiceOfNothingCrossesAsAbsenceNotAsOff) {
  const std::optional<core_bridge::BridgeProviderCommand> projected =
      core_service_internal::ToBridgeProviderCommand(
          *SetModelPreference(std::nullopt, nullptr));

  ASSERT_TRUE(projected);
  // Absence is "Taffy decides" and `OFF` is a person asking for no thinking
  // phase. They are different answers and the enumeration has no member for
  // the first, which is why both halves travel as a pair (decision 0093
  // section 3): a projection that sent `kOff` here would file a choice this
  // person never made.
  EXPECT_FALSE(projected->has_model_id);
  EXPECT_FALSE(projected->has_thinking_level);
}

TEST(RustCoreCommandRoutingTest, AModelChoiceWithNoBodyIsRefused) {
  mojom::CoreServiceCommandPtr command = CommandOfKind(
      mojom::CoreServiceCommandKind::kSetProviderModelPreference);

  EXPECT_FALSE(core_service_internal::ToBridgeProviderCommand(*command));
}

}  // namespace
}  // namespace taffy
