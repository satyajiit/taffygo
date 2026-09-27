// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/frame_inclusion_policy.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// Protocol section 8.1, and the conservative reading of [Open (OD-045)].
//
// The property every one of these tests is really about: a frame is included
// because something granted it, never because nothing excluded it. That is
// what makes the empty case — a new task, a fresh grant, a bug in the policy
// plumbing — safe.

namespace taffy {
namespace {

Origin Tuple(const char* serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = serialization;
  return origin;
}

FrameCandidate Child(const char* origin, bool cross_origin) {
  FrameCandidate candidate;
  candidate.frame_id = FrameId{"frame_child"};
  candidate.origin = Tuple(origin);
  candidate.is_root = false;
  candidate.is_observable = true;
  candidate.is_cross_origin_to_root = cross_origin;
  return candidate;
}

FrameInclusionInputs Inputs(bool include_child_frames) {
  FrameInclusionInputs inputs;
  inputs.root_origin = Tuple("https://primary.taffy.test");
  inputs.scope = ObservationScope::kDocument;
  inputs.include_child_frames = include_child_frames;
  inputs.max_frames = 8;
  return inputs;
}

TEST(FrameInclusionPolicyTest, AnEmptyAllowlistGrantsNothingCrossOrigin) {
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://partner.taffy.test", /*cross_origin=*/true),
      Inputs(/*include_child_frames=*/true), 0);
  EXPECT_FALSE(decision.included);
  EXPECT_EQ(decision.reason, FrameExclusionReason::kOriginNotGranted);
}

TEST(FrameInclusionPolicyTest, ASameOriginChildRidesOnTheRootsGrant) {
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://primary.taffy.test", /*cross_origin=*/false),
      Inputs(/*include_child_frames=*/true), 0);
  EXPECT_TRUE(decision.included);
}

TEST(FrameInclusionPolicyTest, ANamedCrossOriginChildIsIncluded) {
  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/true);
  inputs.allowed_origins.push_back(Tuple("https://partner.taffy.test"));
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://partner.taffy.test", /*cross_origin=*/true), inputs, 0);
  EXPECT_TRUE(decision.included);
}

TEST(FrameInclusionPolicyTest, AHiddenThirdPartyFrameIsNotIncludedByDefault) {
  // The corpus fixture this mirrors carries a zero-size embed-origin frame
  // beside a legitimate partner frame. It is excluded before anyone asks
  // whether it is visible, because its origin was never granted.
  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/true);
  inputs.allowed_origins.push_back(Tuple("https://partner.taffy.test"));
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://embed.taffy.test", /*cross_origin=*/true), inputs, 0);
  EXPECT_FALSE(decision.included);
  EXPECT_EQ(decision.reason, FrameExclusionReason::kOriginNotGranted);
}

TEST(FrameInclusionPolicyTest, ChildFramesNeedToBeAskedFor) {
  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/false);
  inputs.allowed_origins.push_back(Tuple("https://partner.taffy.test"));
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://partner.taffy.test", /*cross_origin=*/true), inputs, 0);
  EXPECT_FALSE(decision.included);
  EXPECT_EQ(decision.reason, FrameExclusionReason::kChildFramesNotRequested);
}

TEST(FrameInclusionPolicyTest, ASelectionScopeNeverSpansAFrameBoundary) {
  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/true);
  inputs.scope = ObservationScope::kSelection;
  inputs.allowed_origins.push_back(Tuple("https://partner.taffy.test"));
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://partner.taffy.test", /*cross_origin=*/true), inputs, 0);
  EXPECT_FALSE(decision.included);
  EXPECT_EQ(decision.reason, FrameExclusionReason::kScopeTooNarrow);
}

TEST(FrameInclusionPolicyTest, TheRendererHintOnlySubtracts) {
  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/true);
  inputs.allowed_origins.push_back(Tuple("https://partner.taffy.test"));

  FrameCandidate candidate =
      Child("https://partner.taffy.test", /*cross_origin=*/true);
  candidate.renderer_reported_hidden = true;
  EXPECT_FALSE(DecideFrameInclusion(candidate, inputs, 0).included);

  // And it cannot let anything in: an ungranted origin stays out whatever the
  // renderer says about it.
  FrameCandidate ungranted =
      Child("https://hostile.taffy.test", /*cross_origin=*/true);
  ungranted.renderer_reported_hidden = false;
  EXPECT_FALSE(DecideFrameInclusion(ungranted, inputs, 0).included);
}

TEST(FrameInclusionPolicyTest, AnUnobservableFrameIsNeverACandidate) {
  FrameCandidate candidate =
      Child("https://primary.taffy.test", /*cross_origin=*/false);
  candidate.is_observable = false;
  const FrameInclusionDecision decision = DecideFrameInclusion(
      candidate, Inputs(/*include_child_frames=*/true), 0);
  EXPECT_FALSE(decision.included);
  EXPECT_EQ(decision.reason, FrameExclusionReason::kNotObservable);
}

TEST(FrameInclusionPolicyTest, TheRootIsNeverSubjectToTheChildPolicy) {
  FrameCandidate root;
  root.frame_id = FrameId{"frame_root"};
  root.origin = Tuple("https://primary.taffy.test");
  root.is_root = true;
  root.is_observable = true;
  const FrameInclusionDecision decision =
      DecideFrameInclusion(root, Inputs(/*include_child_frames=*/false), 0);
  EXPECT_TRUE(decision.included);
}

TEST(FrameInclusionPolicyTest, TheFrameBudgetIsATruncationNotARefusal) {
  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/true);
  inputs.max_frames = 1;
  const FrameInclusionDecision decision = DecideFrameInclusion(
      Child("https://primary.taffy.test", /*cross_origin=*/false), inputs,
      /*frames_already_included=*/1);
  EXPECT_FALSE(decision.included);
  EXPECT_EQ(decision.reason, FrameExclusionReason::kFrameBudgetReached);
}

TEST(FrameInclusionPolicyTest, ApplyingToATreeSeparatesPolicyFromBudget) {
  std::vector<FrameSummary> frames(4);
  frames[0].frame_id = FrameId{"f0"};
  frames[0].is_main_frame = true;
  frames[0].origin = Tuple("https://primary.taffy.test");
  frames[0].included = true;

  frames[1].frame_id = FrameId{"f1"};
  frames[1].origin = Tuple("https://primary.taffy.test");
  frames[1].included = true;

  frames[2].frame_id = FrameId{"f2"};
  frames[2].origin = Tuple("https://hostile.taffy.test");
  frames[2].included = true;

  // Not observable, so never a candidate and never counted as filtered.
  frames[3].frame_id = FrameId{"f3"};
  frames[3].origin = Tuple("https://primary.taffy.test");
  frames[3].included = false;

  FrameInclusionInputs inputs = Inputs(/*include_child_frames=*/true);
  const FrameInclusionSummary summary = ApplyFrameInclusion(inputs, &frames);

  EXPECT_EQ(summary.included_count, 2u);
  EXPECT_EQ(summary.policy_filtered_count, 1u);
  EXPECT_EQ(summary.budget_filtered_count, 0u);
  EXPECT_TRUE(frames[0].included);
  EXPECT_TRUE(frames[1].included);
  EXPECT_FALSE(frames[2].included);
  EXPECT_FALSE(frames[3].included);
}

}  // namespace
}  // namespace taffy
