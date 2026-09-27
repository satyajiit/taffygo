// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_FRAME_INCLUSION_POLICY_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_FRAME_INCLUSION_POLICY_H_

#include <stdint.h>

#include <vector>

#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"

// Which frames of a tab may enter one observation (protocol section 8.1).
//
// The sentence this file implements: "Extracting a cross-origin frame requires
// source scope and policy eligibility; a hidden third-party frame is not
// included merely because it exists."
//
// The decision is deliberately *positive*. A frame is included because
// something granted it, never because nothing excluded it. That inverts the
// obvious implementation, and the inversion is the security property: an
// allowlist that is empty grants nothing, whereas a denylist that is empty
// grants everything, and the empty case is what a new task, a fresh grant, or
// a bug in the policy plumbing produces.
//
// It follows that this policy needs no visibility signal to keep a hidden
// third-party tracking frame out of a research task. That frame's origin is
// not in the grant, so it is excluded before anyone asks whether it is
// visible. A renderer-reported visibility hint is accepted, but only in the
// excluding direction — a renderer may narrow what it hands over and may never
// widen it, exactly as the obscured signal works on the action path.
//
// `[Open (OD-045)]` Which cross-origin child-frame content may enter a research
// task at all is unresolved. This file takes the conservative position — the
// root frame's own origin, plus origins the grant names explicitly, and
// nothing else — and it is the single place that changes when the decision
// lands.
//
// Pure functions over value types: no Chromium object appears here, so the
// policy is provable on a host without a browser and the browser test only has
// to prove that the frame tree is assembled the way the policy expects.

namespace taffy {

// Why a frame was left out. Carried into the observation envelope so that a
// missing frame is explicable rather than mysterious, and counted in the
// redaction summary so that "the answer may be incomplete" is visible.
//
// bip-local-vocabulary: the browser distinguishes more reasons than the wire
//   does, because it is the one that made the decision. Six of these map onto
//   mojom::FrameExclusionReason on the way out and kIncluded is not an
//   exclusion at all; the mapping narrows deliberately, so a consumer of the
//   wire learns the class of reason and the browser's own audit keeps the
//   specific one.
enum class FrameExclusionReason : uint8_t {
  kIncluded = 0,
  // The document is prerendering, in the back/forward cache, pending deletion,
  // or its renderer is not live.
  kNotObservable = 1,
  // The request did not ask for child frames, or the grant did not allow them.
  kChildFramesNotRequested = 2,
  // Cross-origin to the root and its origin is not in the grant's allowlist.
  kOriginNotGranted = 3,
  // The scope this observation runs at does not reach into child frames.
  kScopeTooNarrow = 4,
  // The frame budget was already spent. Reported separately from the policy
  // reasons because it is a truncation, not a refusal, and the truncation
  // summary has to name which budget was reached.
  kFrameBudgetReached = 5,
  // The renderer said this frame is not being displayed. Honoured only in this
  // direction: it can keep a frame out, never let one in.
  kRendererReportedHidden = 6,
};

// What the browser knows about one candidate frame. Every field is a
// browser-owned fact except `renderer_reported_hidden`, which is named so that
// nobody has to look up whether it can be trusted.
struct FrameCandidate {
  FrameId frame_id;
  Origin origin;
  bool is_root = false;
  bool is_observable = false;
  bool is_cross_origin_to_root = false;
  bool renderer_reported_hidden = false;
};

// The observation's own eligibility, after clamping.
struct FrameInclusionInputs {
  Origin root_origin;
  ObservationScope scope = ObservationScope::kViewport;
  bool include_child_frames = false;
  // Origins the grant named, already intersected with the request's. Empty
  // means "the root frame's own origin only", never "no restriction".
  std::vector<Origin> allowed_origins;
  uint32_t max_frames = 0;
};

// One frame's outcome.
struct FrameInclusionDecision {
  bool included = false;
  FrameExclusionReason reason = FrameExclusionReason::kNotObservable;
};

// Decides one frame. `frames_already_included` is what the caller has admitted
// so far, which is how the frame budget is charged in the order the tree was
// walked rather than in an order that depends on a map's iteration.
FrameInclusionDecision DecideFrameInclusion(
    const FrameCandidate& candidate,
    const FrameInclusionInputs& inputs,
    uint32_t frames_already_included);

// Applies the decision to a whole frame tree in place, filling each summary's
// `included` flag, and reports what was left out.
struct FrameInclusionSummary {
  uint32_t included_count = 0;
  uint32_t policy_filtered_count = 0;
  uint32_t budget_filtered_count = 0;
  bool frame_budget_reached = false;
};

FrameInclusionSummary ApplyFrameInclusion(const FrameInclusionInputs& inputs,
                                          std::vector<FrameSummary>* frames);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_FRAME_INCLUSION_POLICY_H_
