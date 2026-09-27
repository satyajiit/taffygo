// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_SUBSCRIPTION_H_
#define TAFFY_PUBLIC_BIP_SUBSCRIPTION_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"

// Delta subscription lifecycle and backpressure
// (protocol section 10, taffy-core/contracts/bip/schema/delta.schema.json).
//
// A subscription is a standing request, so it needs two things a one-shot
// observation does not: a state that can be paused and resumed, and a way to
// say honestly that the stream could not keep up. Both are here.
//
// The backpressure contract is that loss is never silent. The broker may
// reduce observation scope, pause a subscription, or force a resnapshot before
// queue growth affects renderer or interface health — and whichever it does,
// the subscriber is told what was given up and whether its projection is still
// usable. A stream that quietly thinned itself out would leave a subscriber
// acting on a projection it believed was current, which is the failure this
// whole section exists to prevent.

namespace taffy {

// What the broker did to protect renderer and interface health. Members and
// order match the contract's BackpressureAction.
enum class BackpressureAction : uint8_t {
  // Mutations were folded together. No information was lost that the
  // subscriber could have acted on; the delta simply covers more at once.
  kCoalesced = 0,
  // Observation scope was narrowed. The subscriber keeps a valid projection of
  // a smaller region.
  kScopeReduced = 1,
  // The stream stopped delivering until the subscriber catches up. Handles
  // stay valid; the projection stops advancing.
  kSubscriptionPaused = 2,
  // The projection is dead and a bounded fresh snapshot is the only way back.
  kResnapshotRequested = 3,
  // The subscription is over. A new one must be created.
  kSubscriptionStopped = 4,
};

// Whether the action leaves a projection the subscriber may keep applying
// deltas to. Written once here rather than re-derived at each consumer.
constexpr bool BackpressureKeepsProjection(BackpressureAction action) {
  switch (action) {
    case BackpressureAction::kCoalesced:
    case BackpressureAction::kScopeReduced:
    case BackpressureAction::kSubscriptionPaused:
      return true;
    case BackpressureAction::kResnapshotRequested:
    case BackpressureAction::kSubscriptionStopped:
      return false;
  }
  // An unrecognized action invalidates the projection. Fail closed.
  return false;
}

// The observable state of one subscription, as the core service sees it.
enum class SubscriptionState : uint8_t {
  // Deltas are being delivered and the projection is current.
  kActive = 0,
  // Delivery is suspended. The projection is still valid; nothing advances it.
  kPaused = 1,
  // The projection is dead. Only a fresh snapshot re-establishes one, and
  // deltas that arrive meanwhile are discarded rather than queued.
  kAwaitingResnapshot = 2,
  // Terminal. The identifier is never reused.
  kStopped = 3,
};

// Client-requested delta stream bounds. Zero means "unset", and an unset field
// is filled from the process ceiling rather than treated as unlimited — the
// same rule ObservationBudget follows, for the same reason.
struct DeltaBudget {
  // How many undelivered deltas may be held for this subscription before the
  // broker sheds. Charged per subscription, not per tab: one noisy page must
  // not be able to starve another tab's stream.
  uint32_t max_queue_depth = 0;
  // Total encoded bytes those deltas may occupy.
  uint32_t max_queued_bytes = 0;
  // The renderer's mutation coalescing window. Raised, never lowered, to the
  // endpoint's minimum delta interval: a client asking for updates faster than
  // the endpoint will produce them does not get them faster.
  uint32_t coalescing_window_ms = 0;
  // The largest single delta this subscription accepts. A delta above it is
  // over budget, which forces a resnapshot rather than a partial application.
  uint32_t max_delta_bytes = 0;

  friend bool operator==(const DeltaBudget&, const DeltaBudget&) = default;
};

// A standing request to observe one frame's changes.
//
// The optional-signal flags are the visible half of the drop order: a
// subscriber that does not ask for text or layout deltas is asking for the
// stream that survives pressure longest, and one that does ask for them is
// told through BackpressureNotice when they are the first thing given up.
struct SubscriptionRequest {
  RequestId request_id;
  // Browser-side only, like ObservationRequest::authority_subject: the grant
  // this task-owned subscription is clamped against is found by it, and a
  // renderer has no business knowing which task is watching.
  TaskId task_id;
  TabId tab_id;
  FrameId frame_id;
  // Required, unlike an observation's. A subscription with no expected epoch
  // would silently rebind onto whatever document arrived next.
  PageEpoch expected_page_epoch;
  ObservationScope scope = ObservationScope::kViewport;
  std::vector<AdapterRequirement> adapters;
  std::vector<SemanticField> requested_fields;
  DeltaBudget budget;
  bool include_text_deltas = false;
  bool include_layout_deltas = false;
  SensitivityPolicyId sensitivity_policy_id;
};

// The single terminal result of one Subscribe call.
struct SubscriptionEnvelope {
  std::string schema_version;
  RequestId request_id;
  ObservationResultCode code = ObservationResultCode::kInternalError;
  TabId tab_id;
  FrameId frame_id;
  // Present if and only if code is kOk.
  std::optional<SubscriptionId> subscription_id;
  std::optional<PageEpoch> page_epoch;
  // The revision the projection starts from. A subscriber that has not taken a
  // snapshot at this revision cannot apply the first delta, and the broker
  // says so rather than letting it try.
  std::optional<GraphRevision> base_revision;
  // What the request was actually granted, after the process ceiling and the
  // task grant. Reported so a subscriber can see it asked for more.
  DeltaBudget granted_budget;
  ObservationScope granted_scope = ObservationScope::kViewport;
  bool text_deltas_granted = false;
  bool layout_deltas_granted = false;
  MonotonicMillis observed_at_monotonic_ms = 0;
};

// Honest notice that the stream could not keep up
// (taffy-core/contracts/bip/schema/delta.schema.json, BackpressureNotice).
//
// `dropped_classes` may name a protected class, because a renderer that
// overflowed may genuinely have lost one. It may not do so quietly: see
// BackpressureNoticeIsWellFormed below.
struct BackpressureNotice {
  std::string schema_version;
  SubscriptionId subscription_id;
  TabId tab_id;
  FrameId frame_id;
  PageEpoch page_epoch;
  EventSequence event_sequence = 0;
  BackpressureAction action = BackpressureAction::kResnapshotRequested;
  uint32_t dropped_delta_count = 0;
  // In drop order, cheapest first.
  std::vector<DeltaClass> dropped_classes;
  std::optional<uint32_t> queue_depth_hint;
  std::optional<ObservationScope> reduced_scope;
  bool resnapshot_required = false;
  MonotonicMillis observed_at_monotonic_ms = 0;
};

// The invariants the contract states as conditional requirements, in one
// checkable function. The broker asserts this on every notice it emits and on
// every notice it accepts from a renderer, so a malformed notice is a refused
// message rather than a subscriber that trusts a dead projection.
//
//   * A notice that lost a protected class always requires a resnapshot. That
//     is the whole reason those classes are protected.
//   * kScopeReduced must say what the scope was reduced to.
//   * kResnapshotRequested and kSubscriptionStopped always require one.
inline bool BackpressureNoticeIsWellFormed(const BackpressureNotice& notice) {
  for (DeltaClass dropped : notice.dropped_classes) {
    if (!IsSheddableDeltaClass(dropped) && !notice.resnapshot_required) {
      return false;
    }
  }
  if (notice.action == BackpressureAction::kScopeReduced &&
      !notice.reduced_scope.has_value()) {
    return false;
  }
  if (!BackpressureKeepsProjection(notice.action) &&
      !notice.resnapshot_required) {
    return false;
  }
  return notice.subscription_id.is_valid() && notice.tab_id.is_valid() &&
         notice.frame_id.is_valid();
}

// What the broker plans to stop delivering, in drop order.
//
// It can only name sheddable classes: SheddableDeltaClass has no public
// constructor and its factory refuses the protected ones. Shedding a node
// removal is therefore not a bug that review has to catch — it is a program
// that does not compile.
struct DeltaShedPlan {
  std::vector<SheddableDeltaClass> classes;
  // How much queue relief the plan is expected to produce, in deltas.
  uint32_t expected_relief = 0;

  bool is_empty() const { return classes.empty(); }
};

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_SUBSCRIPTION_H_
