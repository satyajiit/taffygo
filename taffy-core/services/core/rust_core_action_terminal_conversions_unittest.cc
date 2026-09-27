// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "taffy/common/public/bip_result.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

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

bridge::BridgeTaskEffect ReconcileBridgeBinding() {
  bridge::BridgeTaskEffect effect{};
  effect.operation.operation_id = "reconcile-operation";
  effect.operation.service_generation = 1u;
  effect.operation.task_revision = 4u;
  effect.operation.deadline_monotonic_ms = 1000u;
  effect.operation.idempotency_key = "reconcile-key";
  effect.effect_id = "reconcile-effect";
  effect.task_id = "task-1";
  effect.kind =
      static_cast<uint8_t>(mojom::TaskReducerEffectKind::kReconcileAction);
  effect.action_id = "action-1";
  effect.dispatch_id = "dispatch-1";
  effect.recovery_rule =
      static_cast<uint8_t>(mojom::TaskRecoveryRule::kReconcileFirst);
  effect.action_operation =
      static_cast<uint8_t>(mojom::TaskActionOperationKind::kDomClick);
  return effect;
}

mojom::TaskEffectBindingPtr ReconcileBinding() {
  std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(ReconcileBridgeBinding());
  EXPECT_TRUE(projected);
  return projected ? std::move(*projected) : nullptr;
}

TEST(RustCoreActionTerminalConversionTest,
     SucceededClickTerminalCarriesNoObservation) {
  const auto effect =
      DispatchBinding(mojom::PolicyActionClass::kSyntheticClick);
  const auto completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_observation);
}

TEST(RustCoreActionTerminalConversionTest,
     SucceededNavigationTerminalCarriesNoObservation) {
  const auto effect = DispatchBinding(mojom::PolicyActionClass::kOpenLink);
  const auto completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_observation);
}

TEST(RustCoreActionTerminalConversionTest,
     DiscoverySearchAndNavigateRequireAnExactCountedLanding) {
  for (auto operation : {mojom::TaskActionOperationKind::kSearch,
                         mojom::TaskActionOperationKind::kNavigate}) {
    SCOPED_TRACE(static_cast<int>(operation));
    auto effect = DispatchBinding(mojom::PolicyActionClass::kOpenLink);
    effect->action->executable->operation_kind = operation;
    effect->action->executable->tab_id = "tab-1";
    effect->action->dispatch_id = "dispatch-1";
    effect->action->document = mojom::TaskFrozenDocument::New();
    effect->action->document->opaque_origin_id = "opaque-blank";
    auto completion = DispatchCompletion(
        *effect, mojom::TaskEffectCompletionStatus::kSucceeded);
    EXPECT_FALSE(
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
    completion->effect_result = mojom::EffectResult::New();
    auto& result = *completion->effect_result;
    result.operation = effect->operation.Clone();
    result.effect_id = effect->effect_id;
    result.status = mojom::EffectStatus::kCompleted;
    result.kind = mojom::EffectKind::kBrowserAction;
    result.browser_action = mojom::BrowserActionEffectResult::New();
    result.browser_action->outcome = mojom::BrowserActionOutcome::kCompleted;
    result.browser_action->dispatch_id = "dispatch-1";
    EXPECT_FALSE(
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
    result.browser_action->discovered_source = mojom::TaskConsentSource::New(
        "source-1", "tab-1", "https://official.test",
        "https://official.test/download-document");
    const auto terminal =
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
    ASSERT_TRUE(terminal);
    EXPECT_TRUE(terminal->has_discovered_source);
    EXPECT_EQ(std::string(terminal->discovered_source_id), "source-1");
    EXPECT_TRUE(terminal->has_discovered_source_canonical_locator);
    EXPECT_EQ(std::string(terminal->discovered_source_canonical_locator),
              "https://official.test/download-document");
    result.browser_action->discovered_source->canonical_locator = "";
    EXPECT_FALSE(
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
    result.browser_action->discovered_source->canonical_locator =
        std::string(mojom::kMaxSourceLocatorBytes + 1u, 'x');
    EXPECT_FALSE(
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
    result.browser_action->discovered_source->canonical_locator.reset();
    const auto no_locator =
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
    ASSERT_TRUE(no_locator);
    EXPECT_FALSE(no_locator->has_discovered_source_canonical_locator);
    EXPECT_TRUE(no_locator->discovered_source_canonical_locator.empty());
    result.browser_action->discovered_source->tab_id = "different-tab";
    EXPECT_FALSE(
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  }
}

TEST(RustCoreActionTerminalConversionTest,
     SucceededClickTerminalRefusesAnAttachedResult) {
  const auto effect =
      DispatchBinding(mojom::PolicyActionClass::kSyntheticClick);
  auto completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->effect_result = PageObservationResult(*effect);

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

TEST(RustCoreActionTerminalConversionTest,
     SucceededObserveTerminalStillRequiresAnObservation) {
  const auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  const auto completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

TEST(RustCoreActionTerminalConversionTest,
     FailedObservationTerminalsPreserveStatusWithoutSnapshot) {
  const auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  for (const auto status : {
           mojom::TaskEffectCompletionStatus::kRefused,
           mojom::TaskEffectCompletionStatus::kUnavailable,
           mojom::TaskEffectCompletionStatus::kCancelled,
           mojom::TaskEffectCompletionStatus::kOutcomeUnknown,
       }) {
    const auto completion = DispatchCompletion(*effect, status);
    const auto terminal =
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
    ASSERT_TRUE(terminal);
    EXPECT_EQ(terminal->status, static_cast<uint8_t>(status));
    EXPECT_FALSE(terminal->has_observation);
  }
}

TEST(RustCoreActionTerminalConversionTest,
     FailedObservationTerminalsRejectAttachedSnapshot) {
  const auto effect = DispatchBinding(mojom::PolicyActionClass::kObservePage);
  for (const auto status : {
           mojom::TaskEffectCompletionStatus::kRefused,
           mojom::TaskEffectCompletionStatus::kUnavailable,
           mojom::TaskEffectCompletionStatus::kCancelled,
           mojom::TaskEffectCompletionStatus::kOutcomeUnknown,
       }) {
    auto completion = DispatchCompletion(*effect, status);
    completion->effect_result = PageObservationResult(*effect);
    completion->effect_result->observation =
        mojom::ObservationEffectResult::New();
    completion->effect_result->observation->status =
        mojom::BipObservationStatus::kResourcePressure;
    EXPECT_FALSE(
        core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
  }
}

TEST(RustCoreActionTerminalConversionTest,
     ReconciliationProjectsExactDispatchAndOperation) {
  const mojom::TaskEffectBindingPtr effect = ReconcileBinding();

  ASSERT_TRUE(effect);
  ASSERT_TRUE(effect->reconcile);
  EXPECT_EQ("action-1", effect->reconcile->action_id);
  EXPECT_EQ("dispatch-1", effect->reconcile->dispatch_id);
  EXPECT_EQ(mojom::TaskRecoveryRule::kReconcileFirst, effect->reconcile->rule);
  EXPECT_EQ(mojom::TaskActionOperationKind::kDomClick,
            effect->reconcile->operation);
}

TEST(RustCoreActionTerminalConversionTest,
     ExactReconciliationResultCrossesAsClosedCode) {
  const mojom::TaskEffectBindingPtr effect = ReconcileBinding();
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->reconciled_action_result = mojom::TaskReconciledActionResult::New(
      static_cast<uint32_t>(ActionResultCode::kDeniedByPolicy));

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_reconciled_action_result);
  EXPECT_EQ(static_cast<uint32_t>(ActionResultCode::kDeniedByPolicy),
            terminal->reconciled_action_result_code);
  EXPECT_EQ(std::string_view("action-1"),
            std::string_view(terminal->action_id));
}

TEST(RustCoreActionTerminalConversionTest,
     UnresolvedReconciliationCarriesNoInventedResult) {
  const mojom::TaskEffectBindingPtr effect = ReconcileBinding();
  ASSERT_TRUE(effect);
  const mojom::TaskEffectCompletionPtr completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kOutcomeUnknown);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_reconciled_action_result);
  EXPECT_EQ(0u, terminal->reconciled_action_result_code);
}

TEST(RustCoreActionTerminalConversionTest,
     ReconciliationRefusesUnknownOrStatusMismatchedCodes) {
  const mojom::TaskEffectBindingPtr effect = ReconcileBinding();
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr unknown = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  unknown->reconciled_action_result =
      mojom::TaskReconciledActionResult::New(999u);
  EXPECT_FALSE(core_service_internal::ToBridgeTaskTerminal(*effect, *unknown));

  mojom::TaskEffectCompletionPtr mismatched = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kOutcomeUnknown);
  mismatched->reconciled_action_result = mojom::TaskReconciledActionResult::New(
      static_cast<uint32_t>(ActionResultCode::kVerified));
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *mismatched));
}

TEST(RustCoreActionTerminalConversionTest,
     SucceededReconciliationCannotOmitItsExactResult) {
  const mojom::TaskEffectBindingPtr effect = ReconcileBinding();
  ASSERT_TRUE(effect);
  const mojom::TaskEffectCompletionPtr completion = DispatchCompletion(
      *effect, mojom::TaskEffectCompletionStatus::kSucceeded);

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion));
}

}  // namespace
}  // namespace taffy
