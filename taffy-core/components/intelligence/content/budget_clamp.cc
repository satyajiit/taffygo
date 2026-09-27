// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/budget_clamp.h"

#include <algorithm>

#include "base/check_op.h"
#include "base/no_destructor.h"

namespace taffy {

namespace {

// The single definition of the process-safe ceiling.
//
// These values are provisional. The ratified numbers come from measurements on
// the supported device floor and are tracked as [Open (OD-031)]; the device
// floor itself is [Open (OD-017)]. Deliberately conservative until then: a
// ceiling that is too low costs a truncated snapshot and an honest truncation
// record, while a ceiling that is too high costs renderer health on the
// cheapest supported phone.
//
// Do not restate any of these numbers anywhere else — not in a document, not
// in a comment, not in a test expectation. The tests assert relationships
// (result is at most the ceiling), never literals.
constexpr uint32_t kMaxMessageBytes = 256 * 1024;
// Honour the Core Service observation ceiling (decision 0144). A process
// clamp below that grant would recut the page and the contract bound would
// never reach the renderer.
constexpr uint32_t kMaxNodes = 1500;
constexpr uint32_t kMaxTextBytes = 128 * 1024;
constexpr uint32_t kMaxTotalBytes = 256 * 1024;
// Matches the traversal ceiling the renderer endpoint declares in
// `//taffy/renderer/observation_limits.json`, and is not permitted to be
// stricter than it (decision 0170).
//
// It was 24 while that file said 64, so the browser recut every page to a
// depth the endpoint would never have stopped at — and depth is the one
// bound here that is a *shape* bound rather than a work bound. Nodes and
// bytes above already bound the work; cutting depth below them does not save
// anything, it just removes whichever subtree happens to be deepest. On a
// phone that subtree was the result links on a search page: the model was
// handed the search box, the tab strip and "Sign in", could not see a single
// result to follow, and typed a hostname it had invented instead.
constexpr uint32_t kMaxDepth = 64;
constexpr uint32_t kMaxFrames = 32;
constexpr uint32_t kMaxDeltaQueueDepth = 64;
constexpr uint32_t kMaxSnapshotDeadlineMs = 2000;
// How long the browser will wait for a site to answer a request it already
// sent, before the postcondition is reported unverified. Not the snapshot
// ceiling above and never derived from it: that one bounds work inside a
// renderer, this one bounds the web. Ten seconds because a page load on the
// supported device floor has to fit inside it and a dispatch whose effect
// never arrives still has to end. Provisional under the same open decision as
// every number in this block.
constexpr uint32_t kMaxPostconditionDeadlineMs = 10000;
constexpr uint32_t kMinDeltaIntervalMs = 100;

// The per-subscription delta bounds and the escalation fractions. Same
// provenance and same open decision as the block above.
constexpr uint32_t kMaxQueuedDeltaBytes = 128 * 1024;
constexpr uint32_t kMaxSingleDeltaBytes = 64 * 1024;
constexpr uint32_t kShedOptionalPercent = 50;
constexpr uint32_t kReduceScopePercent = 75;
constexpr uint32_t kPausePercent = 90;
constexpr uint32_t kResnapshotPercent = 100;

// Requested-but-unset (zero) means "give me the ceiling", not "unlimited".
uint32_t ClampField(uint32_t requested, uint32_t granted, uint32_t ceiling) {
  const uint32_t effective_request = requested == 0 ? ceiling : requested;
  const uint32_t effective_grant = granted == 0 ? ceiling : granted;
  return std::min({effective_request, effective_grant, ceiling});
}

ObservationBudget ClampBudget(const ObservationBudget& requested,
                              const ObservationBudget& granted,
                              const ProcessBudgetLimits& limits) {
  ObservationBudget out;
  out.max_nodes =
      ClampField(requested.max_nodes, granted.max_nodes, limits.max_nodes);
  out.max_text_bytes = ClampField(
      requested.max_text_bytes, granted.max_text_bytes, limits.max_text_bytes);
  out.max_total_bytes =
      ClampField(requested.max_total_bytes, granted.max_total_bytes,
                 limits.max_total_bytes);
  out.max_depth =
      ClampField(requested.max_depth, granted.max_depth, limits.max_depth);
  out.max_frames =
      ClampField(requested.max_frames, granted.max_frames, limits.max_frames);
  out.deadline_ms = ClampField(requested.deadline_ms, granted.deadline_ms,
                               limits.max_snapshot_deadline_ms);
  return out;
}

// A budget with every unset field filled from the ceiling, which is what
// "unset means give me the ceiling" resolves to. Used only to state the clamp
// postcondition against the caller's and the grant's own intent.
ObservationBudget EffectiveBudget(const ObservationBudget& budget,
                                  const ProcessBudgetLimits& limits) {
  ObservationBudget out;
  out.max_nodes = budget.max_nodes == 0 ? limits.max_nodes : budget.max_nodes;
  out.max_text_bytes = budget.max_text_bytes == 0 ? limits.max_text_bytes
                                                  : budget.max_text_bytes;
  out.max_total_bytes = budget.max_total_bytes == 0 ? limits.max_total_bytes
                                                    : budget.max_total_bytes;
  out.max_depth = budget.max_depth == 0 ? limits.max_depth : budget.max_depth;
  out.max_frames =
      budget.max_frames == 0 ? limits.max_frames : budget.max_frames;
  out.deadline_ms = budget.deadline_ms == 0 ? limits.max_snapshot_deadline_ms
                                            : budget.deadline_ms;
  return out;
}

ObservationBudget CeilingAsBudget(const ProcessBudgetLimits& limits) {
  ObservationBudget out;
  out.max_nodes = limits.max_nodes;
  out.max_text_bytes = limits.max_text_bytes;
  out.max_total_bytes = limits.max_total_bytes;
  out.max_depth = limits.max_depth;
  out.max_frames = limits.max_frames;
  out.deadline_ms = limits.max_snapshot_deadline_ms;
  return out;
}

bool BudgetIsNarrowerOrEqual(const ObservationBudget& narrow,
                             const ObservationBudget& wide) {
  return narrow.max_nodes <= wide.max_nodes &&
         narrow.max_text_bytes <= wide.max_text_bytes &&
         narrow.max_total_bytes <= wide.max_total_bytes &&
         narrow.max_depth <= wide.max_depth &&
         narrow.max_frames <= wide.max_frames &&
         narrow.deadline_ms <= wide.deadline_ms;
}

}  // namespace

const ProcessBudgetLimits& GetProcessBudgetLimits() {
  // A function-local static, not base::NoDestructor. At this pin NoDestructor
  // static_asserts that its type is NOT trivially destructible and tells you to
  // use a plain local static instead (base/no_destructor.h:91). Both budget
  // structs are aggregates of integers, so they qualify: there is no destructor
  // to avoid running, which is the only thing NoDestructor buys.
  static const ProcessBudgetLimits limits = [] {
    ProcessBudgetLimits value;
    value.max_message_bytes = kMaxMessageBytes;
    value.max_nodes = kMaxNodes;
    value.max_text_bytes = kMaxTextBytes;
    value.max_total_bytes = kMaxTotalBytes;
    value.max_depth = kMaxDepth;
    value.max_frames = kMaxFrames;
    value.max_delta_queue_depth = kMaxDeltaQueueDepth;
    value.max_snapshot_deadline_ms = kMaxSnapshotDeadlineMs;
    value.min_delta_interval_ms = kMinDeltaIntervalMs;
    return value;
  }();
  return limits;
}

const DeltaPressureThresholds& GetDeltaPressureThresholds() {
  // See GetProcessBudgetLimits above for why this is a plain local static.
  static const DeltaPressureThresholds thresholds = [] {
    DeltaPressureThresholds value;
    value.shed_optional_percent = kShedOptionalPercent;
    value.reduce_scope_percent = kReduceScopePercent;
    value.pause_percent = kPausePercent;
    value.resnapshot_percent = kResnapshotPercent;
    return value;
  }();
  return thresholds;
}

uint32_t ClampDeadlineMs(uint32_t requested_ms,
                         const ProcessBudgetLimits& limits) {
  return requested_ms == 0
             ? limits.max_snapshot_deadline_ms
             : std::min(requested_ms, limits.max_snapshot_deadline_ms);
}

uint32_t ClampPostconditionDeadlineMs(uint32_t requested_ms) {
  return requested_ms == 0
             ? kMaxPostconditionDeadlineMs
             : std::min(requested_ms, kMaxPostconditionDeadlineMs);
}

ObservationRequest ClampObservationRequest(const ObservationRequest& requested,
                                           const ObservationPolicyGrant& grant,
                                           const ProcessBudgetLimits& limits) {
  ObservationRequest out;
  out.request_id = requested.request_id;
  out.authority_subject = requested.authority_subject;
  out.tab_id = requested.tab_id;
  out.root_frame_id = requested.root_frame_id;
  out.expected_page_epoch = requested.expected_page_epoch;
  // A root is an exact narrowing, not a budget axis. Copy it byte for byte;
  // callers refuse the whole request if scope clamping would make this root
  // inapplicable rather than silently turning a form read into another read.
  out.form_root = requested.form_root;
  out.media_root = requested.media_root;
  out.sensitivity_policy_id = requested.sensitivity_policy_id;
  out.task_purpose = requested.task_purpose;
  out.requested_fields = requested.requested_fields;

  out.budget = ClampBudget(requested.budget, grant.budget, limits);

  // Scope narrows by breadth rank, not by enum value: the contract's member
  // order is its own, and reading a security comparison out of it would break
  // the first time a member is appended.
  out.scope = ObservationScopeBreadth(requested.scope) <=
                      ObservationScopeBreadth(grant.max_scope)
                  ? requested.scope
                  : grant.max_scope;

  // Adapters: intersection. An adapter the task was never granted is dropped
  // even when the endpoint supports it and even when the caller marked it
  // required; the request then fails as UNSUPPORTED downstream, which is the
  // honest outcome.
  for (const AdapterRequirement& adapter : requested.adapters) {
    if (std::ranges::contains(grant.allowed_adapters, adapter.adapter)) {
      out.adapters.push_back(adapter);
    }
  }

  // Origins: intersection. An empty grant list means "the root frame's own
  // origin only", so an empty grant clamps every requested origin away rather
  // than being read as "no restriction".
  for (const Origin& origin : requested.allowed_origins) {
    if (std::ranges::contains(grant.allowed_origins, origin)) {
      out.allowed_origins.push_back(origin);
    }
  }

  out.include_child_frames =
      requested.include_child_frames && grant.may_include_child_frames;

  // The postcondition that makes this function a security control rather than
  // a helper. If it ever fails, the clamp is broken and the request must not
  // proceed.
  DCHECK(BudgetIsNarrowerOrEqual(out.budget, CeilingAsBudget(limits)));
  DCHECK(BudgetIsNarrowerOrEqual(out.budget,
                                 EffectiveBudget(requested.budget, limits)));
  DCHECK(BudgetIsNarrowerOrEqual(out.budget,
                                 EffectiveBudget(grant.budget, limits)));
  DCHECK_LE(ObservationScopeBreadth(out.scope),
            ObservationScopeBreadth(requested.scope));
  DCHECK_LE(ObservationScopeBreadth(out.scope),
            ObservationScopeBreadth(grant.max_scope));
  DCHECK(out.form_root == requested.form_root);
  DCHECK(out.media_root == requested.media_root);
  DCHECK_LE(out.adapters.size(), requested.adapters.size());
  DCHECK_LE(out.allowed_origins.size(), requested.allowed_origins.size());
  DCHECK(!out.include_child_frames || requested.include_child_frames);
  DCHECK(!out.include_child_frames || grant.may_include_child_frames);

  return out;
}

SubscriptionRequest ClampSubscriptionRequest(
    const SubscriptionRequest& requested,
    const ObservationPolicyGrant& grant,
    const ProcessBudgetLimits& limits,
    const ProcessBudgetLimits& endpoint_limits) {
  SubscriptionRequest out;
  out.request_id = requested.request_id;
  out.tab_id = requested.tab_id;
  out.frame_id = requested.frame_id;
  out.expected_page_epoch = requested.expected_page_epoch;
  out.sensitivity_policy_id = requested.sensitivity_policy_id;
  out.requested_fields = requested.requested_fields;

  // The queue is bounded by whichever of the two processes is stricter. A
  // renderer that advertised a deeper queue than this process is willing to
  // hold does not get to set the bound.
  const uint32_t queue_ceiling =
      endpoint_limits.max_delta_queue_depth == 0
          ? limits.max_delta_queue_depth
          : std::min(endpoint_limits.max_delta_queue_depth,
                     limits.max_delta_queue_depth);
  out.budget.max_queue_depth =
      ClampField(requested.budget.max_queue_depth, 0, queue_ceiling);
  out.budget.max_queued_bytes =
      ClampField(requested.budget.max_queued_bytes, 0, kMaxQueuedDeltaBytes);
  out.budget.max_delta_bytes =
      ClampField(requested.budget.max_delta_bytes, 0,
                 std::min(kMaxSingleDeltaBytes, limits.max_message_bytes));

  // The one axis that widens. A coalescing window shorter than the endpoint
  // will honour is a promise the endpoint cannot keep, so the floor wins.
  const uint32_t interval_floor = std::max(
      endpoint_limits.min_delta_interval_ms, limits.min_delta_interval_ms);
  out.budget.coalescing_window_ms =
      std::max(requested.budget.coalescing_window_ms, interval_floor);

  out.scope = ObservationScopeBreadth(requested.scope) <=
                      ObservationScopeBreadth(grant.max_scope)
                  ? requested.scope
                  : grant.max_scope;

  for (const AdapterRequirement& adapter : requested.adapters) {
    if (std::ranges::contains(grant.allowed_adapters, adapter.adapter)) {
      out.adapters.push_back(adapter);
    }
  }

  // Text and layout deltas carry page text and geometry, so they need the
  // adapters that produce them to be granted. Switched off rather than
  // refused: a subscriber that asked for more than it may have still gets the
  // stream it may have, and the envelope reports what was granted.
  out.include_text_deltas =
      requested.include_text_deltas &&
      std::ranges::contains(grant.allowed_adapters, AdapterKind::kDom);
  out.include_layout_deltas =
      requested.include_layout_deltas &&
      std::ranges::contains(grant.allowed_adapters, AdapterKind::kDom);

  DCHECK_LE(out.budget.max_queue_depth, limits.max_delta_queue_depth);
  DCHECK_LE(out.budget.max_delta_bytes, limits.max_message_bytes);
  DCHECK_GE(out.budget.coalescing_window_ms, limits.min_delta_interval_ms);
  DCHECK_LE(ObservationScopeBreadth(out.scope),
            ObservationScopeBreadth(requested.scope));
  DCHECK_LE(ObservationScopeBreadth(out.scope),
            ObservationScopeBreadth(grant.max_scope));
  DCHECK_LE(out.adapters.size(), requested.adapters.size());
  DCHECK(!out.include_text_deltas || requested.include_text_deltas);
  DCHECK(!out.include_layout_deltas || requested.include_layout_deltas);
  return out;
}

bool IsNarrowerOrEqual(const ObservationPolicyGrant& narrow,
                       const ObservationPolicyGrant& wide) {
  // Two grants about different documents are not orderings of one another.
  // Neither admits a subset of what the other does, because they are about
  // different pages, and answering "narrower" would be this function telling a
  // caller it may substitute one for the other.
  if (!(narrow.document_origin == wide.document_origin)) {
    return false;
  }
  if (!BudgetIsNarrowerOrEqual(narrow.budget, wide.budget)) {
    return false;
  }
  if (ObservationScopeBreadth(narrow.max_scope) >
      ObservationScopeBreadth(wide.max_scope)) {
    return false;
  }
  if (SensitivityStrictness(narrow.max_sensitivity) >
      SensitivityStrictness(wide.max_sensitivity)) {
    return false;
  }
  if (narrow.may_include_child_frames && !wide.may_include_child_frames) {
    return false;
  }
  for (AdapterKind adapter : narrow.allowed_adapters) {
    if (!std::ranges::contains(wide.allowed_adapters, adapter)) {
      return false;
    }
  }
  for (const Origin& origin : narrow.allowed_origins) {
    if (!std::ranges::contains(wide.allowed_origins, origin)) {
      return false;
    }
  }
  return true;
}

}  // namespace taffy
