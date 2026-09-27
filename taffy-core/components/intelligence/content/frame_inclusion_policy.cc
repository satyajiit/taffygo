// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>

#include "taffy/components/intelligence/content/frame_inclusion_policy.h"

#include "base/check.h"

namespace taffy {

namespace {

// Whether the observation's scope reaches into a child frame at all.
//
// A selection is a range inside one document, so it never spans a frame
// boundary. Everything wider can, subject to the rest of the policy. Written
// against the breadth ranking rather than the enum, because the enum's order
// is the contract's member order.
bool ScopeReachesChildFrames(ObservationScope scope) {
  return ObservationScopeBreadth(scope) >
         ObservationScopeBreadth(ObservationScope::kSelection);
}

}  // namespace

FrameInclusionDecision DecideFrameInclusion(
    const FrameCandidate& candidate,
    const FrameInclusionInputs& inputs,
    uint32_t frames_already_included) {
  FrameInclusionDecision decision;

  // Lifecycle first, for everything including the root. A document that is
  // prerendering, cached, or has no live renderer is not observable, and no
  // amount of policy makes it so (protocol section 13).
  if (!candidate.is_observable) {
    decision.reason = FrameExclusionReason::kNotObservable;
    return decision;
  }

  // The root is charged against the frame budget like any other frame, but it
  // is never subject to the child-frame policy: an observation that excluded
  // its own root would have nothing to describe.
  if (candidate.is_root) {
    if (inputs.max_frames != 0 && frames_already_included >= inputs.max_frames) {
      decision.reason = FrameExclusionReason::kFrameBudgetReached;
      return decision;
    }
    decision.included = true;
    decision.reason = FrameExclusionReason::kIncluded;
    return decision;
  }

  if (!inputs.include_child_frames) {
    decision.reason = FrameExclusionReason::kChildFramesNotRequested;
    return decision;
  }
  if (!ScopeReachesChildFrames(inputs.scope)) {
    decision.reason = FrameExclusionReason::kScopeTooNarrow;
    return decision;
  }

  // The positive check. A same-origin child rides on the root's own grant; a
  // cross-origin child needs its origin named. An empty allowlist therefore
  // admits same-origin children and nothing else, which is the conservative
  // reading of [Open (OD-045)].
  if (candidate.is_cross_origin_to_root &&
      !std::ranges::contains(inputs.allowed_origins, candidate.origin)) {
    decision.reason = FrameExclusionReason::kOriginNotGranted;
    return decision;
  }

  // The renderer's hint, honoured only here — after the frame has already
  // passed every browser-owned check, so it can only subtract.
  if (candidate.renderer_reported_hidden) {
    decision.reason = FrameExclusionReason::kRendererReportedHidden;
    return decision;
  }

  if (inputs.max_frames != 0 && frames_already_included >= inputs.max_frames) {
    decision.reason = FrameExclusionReason::kFrameBudgetReached;
    return decision;
  }

  decision.included = true;
  decision.reason = FrameExclusionReason::kIncluded;
  return decision;
}

FrameInclusionSummary ApplyFrameInclusion(const FrameInclusionInputs& inputs,
                                          std::vector<FrameSummary>* frames) {
  CHECK(frames);
  FrameInclusionSummary summary;

  for (FrameSummary& frame : *frames) {
    FrameCandidate candidate;
    candidate.frame_id = frame.frame_id;
    candidate.origin = frame.origin;
    candidate.is_root = frame.is_main_frame;
    // The tree builder already decided observability from Chromium's own
    // lifecycle state; this policy consumes that answer rather than forming a
    // second opinion about it.
    candidate.is_observable = frame.included;
    candidate.is_cross_origin_to_root =
        !frame.is_main_frame && !(frame.origin == inputs.root_origin);

    const FrameInclusionDecision decision =
        DecideFrameInclusion(candidate, inputs, summary.included_count);
    frame.included = decision.included;
    if (decision.included) {
      ++summary.included_count;
      continue;
    }
    switch (decision.reason) {
      case FrameExclusionReason::kFrameBudgetReached:
        ++summary.budget_filtered_count;
        summary.frame_budget_reached = true;
        break;
      case FrameExclusionReason::kChildFramesNotRequested:
      case FrameExclusionReason::kOriginNotGranted:
      case FrameExclusionReason::kScopeTooNarrow:
      case FrameExclusionReason::kRendererReportedHidden:
        ++summary.policy_filtered_count;
        break;
      case FrameExclusionReason::kNotObservable:
      case FrameExclusionReason::kIncluded:
        // Not a policy filter. A frame that is not observable was never a
        // candidate, and counting it as filtered would overstate how much the
        // policy removed.
        break;
    }
  }
  return summary;
}

}  // namespace taffy
