// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: a dispatched action that did not verify, crossing the
// seam with the exact code it was refused with.
//
// `TaskEffectCompletionStatus` has one refusal member, so a page that moved
// under a read, a control that is no longer the control the capability was
// minted against, and a policy denial all reached the reducer as the same
// word — and its recovery table reads that word as "this will be decided the
// same way again; do not retry". A phone showed both halves: an errand handed
// the page back on the first client-side route, and again on the one press it
// was there to make. The rule these hold is that a refusal carries its own
// word and nothing else — no snapshot, no landing, no rows.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::TaskEffectBindingPtr DispatchBinding(
    mojom::PolicyActionClass action_class) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation =
      mojom::OperationEnvelope::New("operation-1", 1u, 1u, 1000u, "key-1");
  binding->effect_id = "effect-1";
  binding->task_id = "task-1";
  binding->ordinal = 0u;
  binding->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = mojom::TaskActionEffect::New();
  binding->action->action_id = "action-1";
  binding->action->executable = mojom::TaskExecutableAction::New();
  binding->action->executable->action_class = action_class;
  return binding;
}

mojom::TaskEffectCompletionPtr DispatchCompletion(
    const mojom::TaskEffectBinding& effect,
    mojom::TaskEffectCompletionStatus status) {
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = effect.operation->Clone();
  completion->effect_id = effect.effect_id;
  completion->task_id = effect.task_id;
  completion->kind = effect.kind;
  completion->status = status;
  return completion;
}

mojom::EffectResultPtr PageObservationResult(
    const mojom::TaskEffectBinding& effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation->Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kPageObservation;
  return result;
}

TEST(RustCoreActionRefusalTerminalTest,
     ARefusedReadCarriesTheCodeItWasRefusedWith) {
  // The page moved under the read, which is the commonest thing a live site
  // does. Before this, the one refusal member of `TaskEffectCompletionStatus`
  // was all that crossed, and the reducer's recovery table reads a bare policy
  // denial as "do not retry" — so the errand handed the page back instead of
  // reading it again.
  auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  completion->effect_result = PageObservationResult(*effect);
  completion->effect_result->status = mojom::EffectStatus::kDenied;
  completion->effect_result->observation =
      mojom::ObservationEffectResult::New();
  completion->effect_result->observation->status =
      mojom::BipObservationStatus::kStalePageEpoch;
  completion->effect_result->observation->graph_encoding =
      mojom::BipGraphEncoding::kNone;

  const auto terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_denial_code);
  EXPECT_EQ(terminal->denial_code,
            static_cast<uint8_t>(mojom::TaskActionResultCode::kStalePageEpoch));
  EXPECT_FALSE(terminal->has_observation);
}

TEST(RustCoreActionRefusalTerminalTest,
     AReadRefusedBeforeAnyRendererSawItStillNamesItsCode) {
  // A read the browser refused at its own gate has no observation to carry a
  // reason, and `BipObservationStatus` has no word for most of what refuses
  // one: a frame that changed, an origin that changed, a document the browser
  // knows has moved. Those are `TaskActionResultCode` members, so the refusal
  // crosses in the shape every other refused action uses.
  //
  // Before this the gate sent a bare refusal, which the bridge reads as the
  // generic policy denial — `DoNotRetry` for a page that had simply settled
  // under it. On a phone that was one of the two words that ended an errand
  // in a hand-back (decision 0207).
  auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  effect->action->dispatch_id = "dispatch-1";
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  auto refusal = mojom::EffectResult::New();
  refusal->operation = effect->operation->Clone();
  refusal->effect_id = effect->effect_id;
  refusal->status = mojom::EffectStatus::kDenied;
  refusal->kind = mojom::EffectKind::kBrowserAction;
  refusal->browser_action = mojom::BrowserActionEffectResult::New();
  refusal->browser_action->outcome = mojom::BrowserActionOutcome::kRefused;
  refusal->browser_action->dispatch_id = "dispatch-1";
  refusal->browser_action->refused_code = mojom::TaskActionRefusal::New(
      mojom::TaskActionResultCode::kGraphMovedDuringPreflight);
  completion->effect_result = std::move(refusal);

  const auto terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_denial_code);
  EXPECT_EQ(terminal->denial_code,
            static_cast<uint8_t>(
                mojom::TaskActionResultCode::kGraphMovedDuringPreflight));
  EXPECT_FALSE(terminal->has_observation);
}

TEST(RustCoreActionRefusalTerminalTest, ARefusedReadMayCarryNoPage) {
  // The ledger settled this capability against a code that is not Verified,
  // so bytes arriving beside the refusal would be page content the task was
  // refused. Each of the five facts refuses the whole envelope on its own.
  auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  completion->effect_result = PageObservationResult(*effect);
  completion->effect_result->status = mojom::EffectStatus::kDenied;
  completion->effect_result->observation =
      mojom::ObservationEffectResult::New();
  completion->effect_result->observation->status =
      mojom::BipObservationStatus::kStalePageEpoch;
  mojom::ObservationEffectResult& observation =
      *completion->effect_result->observation;
  ASSERT_TRUE(core_service_internal::ToBridgeTaskTerminal(*effect, *completion));

  observation.graph_payload = {1u};
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  observation.graph_payload.clear();
  observation.node_count = 1u;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  observation.node_count = 0u;
  observation.total_bytes = 1u;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  observation.total_bytes = 0u;
  observation.graph_encoding = mojom::BipGraphEncoding::kBipContract;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  observation.graph_encoding = mojom::BipGraphEncoding::kNone;
  observation.media = mojom::MediaObservationResult::New();
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

TEST(RustCoreActionRefusalTerminalTest,
     ARefusedPressCarriesNoAttachedResult) {
  // Only a read names a refusal this way. A dispatched press whose completion
  // arrives with a result attached is a message about work this binding never
  // asked for, and it is still refused whole.
  auto effect = DispatchBinding(mojom::PolicyActionClass::kSyntheticClick);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomClick;
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  completion->effect_result = PageObservationResult(*effect);
  completion->effect_result->status = mojom::EffectStatus::kDenied;
  completion->effect_result->observation =
      mojom::ObservationEffectResult::New();

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

TEST(RustCoreActionRefusalTerminalTest,
     ARefusedReadOfAnotherCallIsStillRefused) {
  auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  completion->effect_result = PageObservationResult(*effect);
  completion->effect_result->status = mojom::EffectStatus::kDenied;
  completion->effect_result->observation =
      mojom::ObservationEffectResult::New();
  completion->effect_result->effect_id = "effect-other";

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

TEST(RustCoreActionRefusalTerminalTest,
     ARefusedPressCarriesTheCodeItWasRefusedWith) {
  // A phone refused a press `kRoleOrActionChanged` on a live portal. The one
  // refusal member of `TaskEffectCompletionStatus` was all that crossed, so
  // the recovery table read "do not retry" and the errand handed the page
  // back on the page it was there to use.
  auto effect = DispatchBinding(mojom::PolicyActionClass::kSyntheticClick);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomClick;
  effect->action->dispatch_id = "dispatch-1";
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  completion->effect_result = mojom::EffectResult::New();
  completion->effect_result->operation = effect->operation.Clone();
  completion->effect_result->effect_id = effect->effect_id;
  completion->effect_result->status = mojom::EffectStatus::kDenied;
  completion->effect_result->kind = mojom::EffectKind::kBrowserAction;
  completion->effect_result->browser_action =
      mojom::BrowserActionEffectResult::New();
  mojom::BrowserActionEffectResult& action =
      *completion->effect_result->browser_action;
  action.outcome = mojom::BrowserActionOutcome::kRefused;
  action.dispatch_id = "dispatch-1";
  action.refused_code = mojom::TaskActionRefusal::New(
      mojom::TaskActionResultCode::kRoleOrActionChanged);

  const auto terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_denial_code);
  EXPECT_EQ(
      terminal->denial_code,
      static_cast<uint8_t>(mojom::TaskActionResultCode::kRoleOrActionChanged));
  EXPECT_FALSE(terminal->has_discovered_source);

  // A refusal is a refusal and nothing else. Each of these turns it back into
  // a message about work this binding never asked for.
  action.refused_code =
      mojom::TaskActionRefusal::New(mojom::TaskActionResultCode::kVerified);
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  action.refused_code = mojom::TaskActionRefusal::New(
      mojom::TaskActionResultCode::kRoleOrActionChanged);
  action.discovered_source = mojom::TaskConsentSource::New(
      "source-1", "tab-1", "https://official.test", std::nullopt);
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  action.discovered_source.reset();
  action.outcome = mojom::BrowserActionOutcome::kCompleted;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

TEST(RustCoreActionRefusalTerminalTest,
     ARefusedActionWithNoNamedCodeIsRefusedWhole) {
  // The generic withdrawal names no node, no precondition and no gate, so
  // there is nothing to carry and the terminal facts stand alone.
  auto effect = DispatchBinding(mojom::PolicyActionClass::kSyntheticClick);
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomClick;
  auto completion =
      DispatchCompletion(*effect, mojom::TaskEffectCompletionStatus::kRefused);
  completion->effect_result = mojom::EffectResult::New();
  completion->effect_result->operation = effect->operation.Clone();
  completion->effect_result->effect_id = effect->effect_id;
  completion->effect_result->status = mojom::EffectStatus::kDenied;
  completion->effect_result->kind = mojom::EffectKind::kBrowserAction;
  completion->effect_result->browser_action =
      mojom::BrowserActionEffectResult::New();
  completion->effect_result->browser_action->outcome =
      mojom::BrowserActionOutcome::kRefused;

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));

  // Without any result at all it is the ordinary refused terminal.
  completion->effect_result.reset();
  const auto terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_denial_code);
}

}  // namespace
}  // namespace taffy
