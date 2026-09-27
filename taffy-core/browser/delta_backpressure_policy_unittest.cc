// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/delta_backpressure_policy.h"

#include "taffy/components/intelligence/content/budget_clamp.h"
#include "testing/gtest/include/gtest/gtest.h"

// The escalation ladder of protocol section 10.
//
// Every assertion here is about a relationship — this rung comes before that
// one, this class is shed before that one, this action requires a resnapshot.
// None of them names a threshold value, because the thresholds are provisional
// and [Open (OD-031)]: a test that pinned one would fail when the device-floor
// measurements land, which is exactly when it should still pass.

namespace taffy {
namespace {

DeltaBudget Budget(uint32_t depth, uint32_t bytes) {
  DeltaBudget budget;
  budget.max_queue_depth = depth;
  budget.max_queued_bytes = bytes;
  budget.max_delta_bytes = bytes;
  budget.coalescing_window_ms = 100;
  return budget;
}

// A subscription at `percent` of its queue, carrying the optional signals.
DeltaBackpressurePolicy::Pressure At(uint32_t percent) {
  DeltaBackpressurePolicy::Pressure pressure;
  pressure.budget = Budget(100, 100000);
  pressure.queue_depth = percent;
  pressure.queued_bytes = 0;
  pressure.carries_text_deltas = true;
  pressure.carries_layout_deltas = true;
  pressure.scope = ObservationScope::kDocument;
  return pressure;
}

bool Sheds(const DeltaShedPlan& plan, DeltaClass delta_class) {
  for (const SheddableDeltaClass& sheddable : plan.classes) {
    if (sheddable.value() == delta_class) {
      return true;
    }
  }
  return false;
}

TEST(DeltaBackpressurePolicyTest, QuietBelowTheFirstThreshold) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(At(0), thresholds);
  // A stream of "everything is fine" notices is noise a subscriber learns to
  // ignore, so silence is the correct output.
  EXPECT_FALSE(decision.notify);
  EXPECT_TRUE(decision.shed.is_empty());
}

TEST(DeltaBackpressurePolicyTest, RendererReportedLossIsAlwaysReported) {
  DeltaBackpressurePolicy::Pressure pressure = At(0);
  pressure.renderer_reported_loss = true;
  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(pressure, GetDeltaPressureThresholds());
  // The loss is real whatever this process's own queue looks like.
  EXPECT_TRUE(decision.notify);
}

TEST(DeltaBackpressurePolicyTest, TheFirstRungGivesUpTheOptionalSignalsOnly) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(At(thresholds.shed_optional_percent),
                                        thresholds);
  ASSERT_TRUE(decision.notify);
  EXPECT_FALSE(decision.resnapshot_required);
  EXPECT_EQ(decision.next_state, SubscriptionState::kActive);
  EXPECT_TRUE(Sheds(decision.shed, DeltaClass::kText));
  EXPECT_TRUE(Sheds(decision.shed, DeltaClass::kLayout));
  // Structural changes are still carried: giving them up would make the
  // projection wrong rather than merely thinner.
  EXPECT_FALSE(Sheds(decision.shed, DeltaClass::kNodeAdded));
}

TEST(DeltaBackpressurePolicyTest, ScopeNarrowsBeforeDeliveryIsSuspended) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  ASSERT_LT(thresholds.reduce_scope_percent, thresholds.pause_percent);

  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(At(thresholds.reduce_scope_percent),
                                        thresholds);
  ASSERT_TRUE(decision.notify);
  EXPECT_EQ(decision.action, BackpressureAction::kScopeReduced);
  ASSERT_TRUE(decision.reduced_scope.has_value());
  // Narrower, and by the breadth ranking rather than by the enum's order.
  EXPECT_LT(ObservationScopeBreadth(*decision.reduced_scope),
            ObservationScopeBreadth(ObservationScope::kDocument));
  EXPECT_FALSE(decision.resnapshot_required);
}

TEST(DeltaBackpressurePolicyTest, PausingKeepsTheProjection) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(At(thresholds.pause_percent),
                                        thresholds);
  ASSERT_TRUE(decision.notify);
  EXPECT_EQ(decision.action, BackpressureAction::kSubscriptionPaused);
  EXPECT_EQ(decision.next_state, SubscriptionState::kPaused);
  EXPECT_FALSE(decision.resnapshot_required);
  EXPECT_TRUE(BackpressureKeepsProjection(decision.action));
}

TEST(DeltaBackpressurePolicyTest, AFullQueueAbandonsTheProjection) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(At(thresholds.resnapshot_percent),
                                        thresholds);
  ASSERT_TRUE(decision.notify);
  EXPECT_EQ(decision.action, BackpressureAction::kResnapshotRequested);
  EXPECT_TRUE(decision.resnapshot_required);
  EXPECT_EQ(decision.next_state, SubscriptionState::kAwaitingResnapshot);
  EXPECT_FALSE(BackpressureKeepsProjection(decision.action));
}

TEST(DeltaBackpressurePolicyTest, NoRungEverShedsAProtectedClass) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  for (uint32_t percent = 0; percent <= 100; ++percent) {
    const DeltaBackpressurePolicy::Decision decision =
        DeltaBackpressurePolicy::Evaluate(At(percent), thresholds);
    for (const SheddableDeltaClass& sheddable : decision.shed.classes) {
      // Guaranteed by construction — SheddableDeltaClass cannot hold one — so
      // this test is really asserting that the guarantee has not been worked
      // around with a second, weaker representation.
      EXPECT_TRUE(IsSheddableDeltaClass(sheddable.value()));
      EXPECT_NE(sheddable.value(), DeltaClass::kNodeRemoved);
      EXPECT_NE(sheddable.value(), DeltaClass::kLifecycle);
    }
  }
}

TEST(DeltaBackpressurePolicyTest, ADeadProjectionSkipsStraightToResnapshot) {
  DeltaBackpressurePolicy::Pressure pressure = At(0);
  pressure.projection_already_dead = true;
  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(pressure, GetDeltaPressureThresholds());
  // Shedding protects a projection. There is nothing left to protect, and
  // pausing would only delay the resnapshot that is the way back.
  EXPECT_EQ(decision.action, BackpressureAction::kResnapshotRequested);
  EXPECT_TRUE(decision.resnapshot_required);
}

TEST(DeltaBackpressurePolicyTest, AZeroQueueBudgetIsAlreadyFull) {
  DeltaBackpressurePolicy::Pressure pressure;
  pressure.budget = Budget(0, 0);
  // Fail closed: no room granted means no room, never unlimited room.
  EXPECT_EQ(DeltaBackpressurePolicy::PressurePercent(pressure), 100u);
}

TEST(DeltaBackpressurePolicyTest, PressureTakesTheWorseOfDepthAndBytes) {
  DeltaBackpressurePolicy::Pressure pressure;
  pressure.budget = Budget(100, 100);
  pressure.queue_depth = 10;
  pressure.queued_bytes = 80;
  EXPECT_EQ(DeltaBackpressurePolicy::PressurePercent(pressure), 80u);
}

TEST(DeltaBackpressurePolicyTest, TheNarrowestScopeCannotNarrowFurther) {
  EXPECT_FALSE(
      DeltaBackpressurePolicy::NarrowerScope(ObservationScope::kSelection)
          .has_value());
  const std::optional<ObservationScope> narrower =
      DeltaBackpressurePolicy::NarrowerScope(ObservationScope::kViewport);
  ASSERT_TRUE(narrower.has_value());
  EXPECT_EQ(*narrower, ObservationScope::kSelection);
}

}  // namespace
}  // namespace taffy
