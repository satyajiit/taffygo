// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/budget_clamp.h"

#include "testing/gtest/include/gtest/gtest.h"

// These tests exist to prove one sentence from protocol section 7.1: "a
// request cannot increase the data policy or source scope already granted to
// the task".
//
// Every assertion is a relationship, never a literal. The process ceiling is
// provisional pending [Open (OD-031)], and a test that hard-coded its values
// would have to be edited every time the ceiling moves — which is exactly how
// a test stops catching regressions and starts documenting them.

namespace taffy {
namespace {

ObservationPolicyGrant MakeGrant() {
  ObservationPolicyGrant grant;
  grant.max_scope = ObservationScope::kInteractive;
  grant.may_include_child_frames = false;
  grant.max_sensitivity = Sensitivity::kNotSensitive;
  grant.allowed_adapters = {AdapterKind::kDom, AdapterKind::kAccessibility};
  Origin allowed;
  allowed.kind = OriginKind::kTuple;
  allowed.serialization = "https://granted.test";
  grant.allowed_origins = {allowed};
  return grant;
}

ObservationRequest MakeGreedyRequest() {
  ObservationRequest request;
  request.scope = ObservationScope::kDocument;
  request.include_child_frames = true;
  request.adapters = {
      {AdapterKind::kDom, AdapterRequirementLevel::kRequired},
      {AdapterKind::kForms, AdapterRequirementLevel::kRequired},
      {AdapterKind::kVision, AdapterRequirementLevel::kOptional},
  };
  Origin other;
  other.kind = OriginKind::kTuple;
  other.serialization = "https://not-granted.test";
  request.allowed_origins = {other};
  // Absurd values, to prove they are clamped rather than trusted.
  request.budget.max_nodes = 4000000;
  request.budget.max_text_bytes = 4000000;
  request.budget.max_total_bytes = 4000000;
  request.budget.max_depth = 4000;
  request.budget.max_frames = 4000;
  request.budget.deadline_ms = 4000000;
  return request;
}

TEST(BudgetClampTest, RequestCannotExceedProcessCeiling) {
  const ProcessBudgetLimits& limits = GetProcessBudgetLimits();
  ObservationPolicyGrant grant = MakeGrant();
  // A grant that asks for more than the process allows is itself clamped.
  grant.budget.max_nodes = limits.max_nodes * 10;
  grant.budget.max_total_bytes = limits.max_total_bytes * 10;

  const ObservationRequest clamped =
      ClampObservationRequest(MakeGreedyRequest(), grant, limits);

  EXPECT_LE(clamped.budget.max_nodes, limits.max_nodes);
  EXPECT_LE(clamped.budget.max_text_bytes, limits.max_text_bytes);
  EXPECT_LE(clamped.budget.max_total_bytes, limits.max_total_bytes);
  EXPECT_LE(clamped.budget.max_depth, limits.max_depth);
  EXPECT_LE(clamped.budget.max_frames, limits.max_frames);
  EXPECT_LE(clamped.budget.deadline_ms, limits.max_snapshot_deadline_ms);
}

TEST(BudgetClampTest, RequestCannotWidenScope) {
  const ObservationRequest clamped = ClampObservationRequest(
      MakeGreedyRequest(), MakeGrant(), GetProcessBudgetLimits());
  EXPECT_LE(ObservationScopeBreadth(clamped.scope),
            ObservationScopeBreadth(MakeGrant().max_scope));
}

TEST(BudgetClampTest, RequestCannotAddAnUngrantedAdapter) {
  const ObservationRequest clamped = ClampObservationRequest(
      MakeGreedyRequest(), MakeGrant(), GetProcessBudgetLimits());
  for (const AdapterRequirement& adapter : clamped.adapters) {
    EXPECT_TRUE(adapter.adapter == AdapterKind::kDom ||
                adapter.adapter == AdapterKind::kAccessibility)
        << "an adapter outside the grant survived the clamp";
  }
}

TEST(BudgetClampTest, RequestCannotAddAnUngrantedOrigin) {
  const ObservationRequest clamped = ClampObservationRequest(
      MakeGreedyRequest(), MakeGrant(), GetProcessBudgetLimits());
  EXPECT_TRUE(clamped.allowed_origins.empty());
}

TEST(BudgetClampTest, RequestCannotAddChildFrames) {
  const ObservationRequest clamped = ClampObservationRequest(
      MakeGreedyRequest(), MakeGrant(), GetProcessBudgetLimits());
  EXPECT_FALSE(clamped.include_child_frames);
}

TEST(BudgetClampTest, EmptyGrantOriginListMeansRootOriginOnly) {
  ObservationPolicyGrant grant = MakeGrant();
  grant.allowed_origins.clear();

  ObservationRequest request = MakeGreedyRequest();
  Origin any;
  any.kind = OriginKind::kTuple;
  any.serialization = "https://anything.test";
  request.allowed_origins = {any};

  const ObservationRequest clamped =
      ClampObservationRequest(request, grant, GetProcessBudgetLimits());
  // An empty grant list is not "no restriction".
  EXPECT_TRUE(clamped.allowed_origins.empty());
}

TEST(BudgetClampTest, UnsetBudgetFieldsBecomeTheCeilingNotUnlimited) {
  ObservationRequest request;  // Every budget field left at zero.
  ObservationPolicyGrant grant = MakeGrant();

  const ObservationRequest clamped =
      ClampObservationRequest(request, grant, GetProcessBudgetLimits());
  EXPECT_GT(clamped.budget.max_nodes, 0u);
  EXPECT_LE(clamped.budget.max_nodes, GetProcessBudgetLimits().max_nodes);
}

TEST(BudgetClampTest, WideningReplacementGrantIsRejected) {
  const ObservationPolicyGrant narrow = MakeGrant();

  ObservationPolicyGrant wider = narrow;
  wider.max_scope = ObservationScope::kDocument;
  EXPECT_FALSE(IsNarrowerOrEqual(wider, narrow));

  ObservationPolicyGrant more_adapters = narrow;
  more_adapters.allowed_adapters.push_back(AdapterKind::kVision);
  EXPECT_FALSE(IsNarrowerOrEqual(more_adapters, narrow));

  ObservationPolicyGrant more_sensitive = narrow;
  more_sensitive.max_sensitivity = Sensitivity::kCredential;
  EXPECT_FALSE(IsNarrowerOrEqual(more_sensitive, narrow));

  ObservationPolicyGrant child_frames = narrow;
  child_frames.may_include_child_frames = true;
  EXPECT_FALSE(IsNarrowerOrEqual(child_frames, narrow));

  // Narrowing in every direction is allowed.
  ObservationPolicyGrant narrower = narrow;
  narrower.max_scope = ObservationScope::kViewport;
  narrower.allowed_adapters = {AdapterKind::kDom};
  EXPECT_TRUE(IsNarrowerOrEqual(narrower, narrow));
}

TEST(BudgetClampTest, UnknownSensitivityIsTheStrictestClass) {
  // A value nobody could classify must never sort below a named class, or a
  // grant for "personal" would silently admit it.
  for (Sensitivity sensitivity :
       {Sensitivity::kNotSensitive, Sensitivity::kPersonal,
        Sensitivity::kAccount, Sensitivity::kPayment, Sensitivity::kIdentity,
        Sensitivity::kHealth, Sensitivity::kFinancial, Sensitivity::kLegal,
        Sensitivity::kPrivateCommunication, Sensitivity::kAdministration,
        Sensitivity::kCredential, Sensitivity::kOneTimeCode,
        Sensitivity::kChallengeResponse}) {
    EXPECT_LT(SensitivityStrictness(sensitivity),
              SensitivityStrictness(Sensitivity::kUnknownSensitive));
  }
}

}  // namespace

// --- subscription clamping --------------------------------------------------
//
// A subscription narrows on every axis an observation does, plus two of its
// own. The second of those is the only axis in the whole file that widens, and
// it widens for a reason worth stating: a coalescing window shorter than the
// endpoint will honour is a promise the endpoint cannot keep.

namespace {

SubscriptionRequest AskingForEverything() {
  SubscriptionRequest request;
  request.task_id = TaskId{"task_1"};
  request.tab_id = TabId{"tab_1"};
  request.frame_id = FrameId{"frame_1"};
  request.expected_page_epoch = PageEpoch{"epoch_1"};
  request.scope = ObservationScope::kDocument;
  request.include_text_deltas = true;
  request.include_layout_deltas = true;
  request.budget.max_queue_depth = 1000000;
  request.budget.max_queued_bytes = 1000000000;
  request.budget.max_delta_bytes = 1000000000;
  request.budget.coalescing_window_ms = 1;
  request.adapters.push_back(
      AdapterRequirement{AdapterKind::kDom, AdapterRequirementLevel::kRequired});
  request.adapters.push_back(AdapterRequirement{
      AdapterKind::kVision, AdapterRequirementLevel::kOptional});
  return request;
}

ObservationPolicyGrant NarrowGrant() {
  ObservationPolicyGrant grant;
  grant.max_scope = ObservationScope::kViewport;
  grant.allowed_adapters = {AdapterKind::kDom};
  return grant;
}

}  // namespace

// Decision 0169. A postcondition waits on the web; a snapshot waits on a
// renderer. Nothing in the shipping path writes `Postcondition::timeout_ms`,
// so the zero branch is the only branch a dispatched action ever takes, and
// it used to answer the snapshot ceiling: a phone's first search was
// contradicted after two seconds with the results page on the screen.
//
// A relationship, not a literal, for the reason the file header gives.
TEST(BudgetClampTest, APostconditionWaitsLongerThanASnapshotMay) {
  const ProcessBudgetLimits& limits = GetProcessBudgetLimits();
  EXPECT_GT(ClampPostconditionDeadlineMs(0),
            ClampDeadlineMs(0, limits));
}

TEST(BudgetClampTest, AnUnsetPostconditionDeadlineIsTheCeilingNotUnlimited) {
  const uint32_t ceiling = ClampPostconditionDeadlineMs(0);
  EXPECT_GT(ceiling, 0u);
  EXPECT_EQ(ClampPostconditionDeadlineMs(ceiling + 1u), ceiling);
  EXPECT_EQ(ClampPostconditionDeadlineMs(1u), 1u);
}

TEST(BudgetClampTest, SubscriptionQueueNeverExceedsTheProcessCeiling) {
  const ProcessBudgetLimits& limits = GetProcessBudgetLimits();
  const SubscriptionRequest clamped = ClampSubscriptionRequest(
      AskingForEverything(), NarrowGrant(), limits, ProcessBudgetLimits());
  EXPECT_LE(clamped.budget.max_queue_depth, limits.max_delta_queue_depth);
  EXPECT_LE(clamped.budget.max_delta_bytes, limits.max_message_bytes);
}

TEST(BudgetClampTest, SubscriptionTakesTheStricterOfTheTwoQueueCeilings) {
  const ProcessBudgetLimits& limits = GetProcessBudgetLimits();
  ProcessBudgetLimits endpoint;
  endpoint.max_delta_queue_depth = 1;
  endpoint.min_delta_interval_ms = limits.min_delta_interval_ms;

  const SubscriptionRequest clamped = ClampSubscriptionRequest(
      AskingForEverything(), NarrowGrant(), limits, endpoint);
  // A renderer that advertised a deeper queue than this process is willing to
  // hold does not get to set the bound, and neither does the process when the
  // renderer is stricter.
  EXPECT_EQ(clamped.budget.max_queue_depth, 1u);
}

TEST(BudgetClampTest, CoalescingWindowIsAFloorNotACeiling) {
  const ProcessBudgetLimits& limits = GetProcessBudgetLimits();
  ProcessBudgetLimits endpoint;
  endpoint.min_delta_interval_ms = limits.min_delta_interval_ms * 3;

  const SubscriptionRequest clamped = ClampSubscriptionRequest(
      AskingForEverything(), NarrowGrant(), limits, endpoint);
  EXPECT_GE(clamped.budget.coalescing_window_ms,
            endpoint.min_delta_interval_ms);
  EXPECT_GE(clamped.budget.coalescing_window_ms, limits.min_delta_interval_ms);
}

TEST(BudgetClampTest, SubscriptionScopeNarrowsToTheGrant) {
  const SubscriptionRequest clamped =
      ClampSubscriptionRequest(AskingForEverything(), NarrowGrant(),
                               GetProcessBudgetLimits(), ProcessBudgetLimits());
  EXPECT_LE(ObservationScopeBreadth(clamped.scope),
            ObservationScopeBreadth(NarrowGrant().max_scope));
}

TEST(BudgetClampTest, UngrantedAdaptersAreDroppedFromASubscription) {
  const SubscriptionRequest clamped =
      ClampSubscriptionRequest(AskingForEverything(), NarrowGrant(),
                               GetProcessBudgetLimits(), ProcessBudgetLimits());
  ASSERT_EQ(clamped.adapters.size(), 1u);
  EXPECT_EQ(clamped.adapters.front().adapter, AdapterKind::kDom);
}

TEST(BudgetClampTest, OptionalSignalsAreSwitchedOffRatherThanRefused) {
  ObservationPolicyGrant grant = NarrowGrant();
  grant.allowed_adapters = {AdapterKind::kMetadata};

  const SubscriptionRequest clamped =
      ClampSubscriptionRequest(AskingForEverything(), grant,
                               GetProcessBudgetLimits(), ProcessBudgetLimits());
  // A subscriber that asked for more than it may have still gets the stream it
  // may have; the envelope reports what was granted.
  EXPECT_FALSE(clamped.include_text_deltas);
  EXPECT_FALSE(clamped.include_layout_deltas);
}

TEST(BudgetClampTest, PressureThresholdsEscalateInOrder) {
  const DeltaPressureThresholds& thresholds = GetDeltaPressureThresholds();
  // Relationships, never literals: the values are provisional and
  // [Open (OD-031)], and this test must still pass when they move.
  EXPECT_LT(thresholds.shed_optional_percent, thresholds.reduce_scope_percent);
  EXPECT_LT(thresholds.reduce_scope_percent, thresholds.pause_percent);
  EXPECT_LT(thresholds.pause_percent, thresholds.resnapshot_percent);
  EXPECT_EQ(thresholds.resnapshot_percent, 100u);
}

}  // namespace taffy
