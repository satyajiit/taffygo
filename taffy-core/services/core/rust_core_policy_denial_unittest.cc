// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_policy.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: the closed code a denied policy evaluation carries, in
// both directions across the utility's seam. Outbound, the Rust engine's
// refusal becomes the Mojo result the browser records and hands back.
// Inbound, the browser's answer to an ASK_POLICY effect — its own refusal or
// the engine's — becomes the terminal the bridge settles the action with. A
// denial is a decision about the proposal, so it travels only on a denied
// answer and never ends the core.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

constexpr uint8_t kDenied = 2u;                // PolicyEvaluationStatus
constexpr uint8_t kEgressNotAuthorized = 31u;  // TaskActionResultCode

bridge::BridgePolicyResult DeniedResult() {
  bridge::BridgePolicyResult result{};
  result.operation_id = "operation-1";
  result.status = kDenied;
  result.has_denial_code = true;
  result.denial_code = kEgressNotAuthorized;
  return result;
}

mojom::TaskEffectBindingPtr PolicyBinding() {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation =
      mojom::OperationEnvelope::New("operation-1", 1u, 1u, 1000u, "key-1");
  effect->effect_id = "effect-1";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = mojom::TaskReducerEffectKind::kAskPolicy;
  effect->policy = mojom::TaskPolicyEffect::New();
  effect->policy->action_id = "action-1";
  return effect;
}

mojom::PolicyEvaluationResultPtr Answer(mojom::PolicyEvaluationStatus status) {
  auto result = mojom::PolicyEvaluationResult::New();
  result->operation_id = "operation-1";
  result->status = status;
  return result;
}

TEST(RustCorePolicyDenialTest, ADeniedEngineAnswerCarriesItsCodeOutward) {
  mojom::PolicyEvaluationResultPtr projected =
      core_service_internal::ToMojoPolicyResult(DeniedResult());

  ASSERT_TRUE(projected);
  EXPECT_EQ(mojom::PolicyEvaluationStatus::kDenied, projected->status);
  EXPECT_FALSE(projected->minted_grant);
  ASSERT_TRUE(projected->denial);
  EXPECT_EQ(mojom::TaskActionResultCode::kEgressNotAuthorized,
            projected->denial->code);
}

TEST(RustCorePolicyDenialTest, AnUnknownDenialCodeIsRefusedNotCoerced) {
  bridge::BridgePolicyResult result = DeniedResult();
  result.denial_code = 200u;
  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyDenialTest, ADenialWithoutACodeProjectsNoDenial) {
  bridge::BridgePolicyResult result = DeniedResult();
  result.has_denial_code = false;
  mojom::PolicyEvaluationResultPtr projected =
      core_service_internal::ToMojoPolicyResult(std::move(result));
  ASSERT_TRUE(projected);
  EXPECT_FALSE(projected->denial);
}

TEST(RustCorePolicyDenialTest, TheBrowsersDeniedAnswerReachesTheBridgeAsACode) {
  mojom::PolicyEvaluationResultPtr answer =
      Answer(mojom::PolicyEvaluationStatus::kDenied);
  answer->denial = mojom::PolicyDenial::New(
      mojom::TaskActionResultCode::kEgressNotAuthorized);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskPolicyTerminal(*PolicyBinding(),
                                                        *answer);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(kDenied, terminal->status);
  EXPECT_EQ("action-1", std::string(terminal->action_id));
  EXPECT_TRUE(terminal->has_denial_code);
  EXPECT_EQ(kEgressNotAuthorized, terminal->denial_code);
}

TEST(RustCorePolicyDenialTest, ADeniedAnswerWithoutACodeIsStillADenial) {
  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskPolicyTerminal(
          *PolicyBinding(), *Answer(mojom::PolicyEvaluationStatus::kDenied));
  ASSERT_TRUE(terminal);
  EXPECT_EQ(kDenied, terminal->status);
  EXPECT_FALSE(terminal->has_denial_code);
  EXPECT_EQ(0u, terminal->denial_code);
}

TEST(RustCorePolicyDenialTest, ACodeOnAnAnswerThatIsNotDeniedIsRefused) {
  mojom::PolicyEvaluationResultPtr answer =
      Answer(mojom::PolicyEvaluationStatus::kApprovalRequired);
  answer->denial =
      mojom::PolicyDenial::New(mojom::TaskActionResultCode::kDeniedByPolicy);
  EXPECT_FALSE(core_service_internal::ToBridgeTaskPolicyTerminal(
      *PolicyBinding(), *answer));
}

TEST(RustCorePolicyDenialTest, AnInvalidRequestAnswerStillCrossesAsATerminal) {
  // The bridge settles the action for this status too; what matters here is
  // that the utility hands it over rather than answering an empty batch.
  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskPolicyTerminal(
          *PolicyBinding(),
          *Answer(mojom::PolicyEvaluationStatus::kInvalidRequest));
  ASSERT_TRUE(terminal);
  EXPECT_EQ(3u, terminal->status);
  EXPECT_FALSE(terminal->has_denial_code);
}

}  // namespace
}  // namespace taffy
