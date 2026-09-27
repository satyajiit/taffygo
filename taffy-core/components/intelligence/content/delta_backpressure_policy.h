// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_BACKPRESSURE_POLICY_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_BACKPRESSURE_POLICY_H_

#include <stdint.h>

#include <optional>

#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_subscription.h"

// What the broker does before queue growth hurts renderer or interface health
// (protocol section 10).
//
// The protocol names three broker moves and one ordering rule:
//
//   "The broker may reduce observation scope, pause a subscription, or request
//    a resnapshot before queue growth affects renderer/UI health. Optional
//    text/layout deltas are dropped before lifecycle or node-removal signals."
//
// This class is the ladder those sentences describe, and nothing else. It
// holds no state, touches no Chromium object, and decides using only the
// numbers handed to it, so every rung is provable on a host without a browser.
//
// The ordering rule is enforced twice over, and the redundancy is the point:
//
//   * DeltaShedPlan can only hold SheddableDeltaClass values, and there is no
//     way to build one of those from kNodeRemoved or kLifecycle. Shedding a
//     removal is not a mistake this code can make — it is a program that does
//     not compile.
//   * Within the sheddable classes, this class draws a second line. Text and
//     layout are optional signals the subscriber opted into, so dropping them
//     leaves a projection the subscriber may keep using. Attribute, edge and
//     node-addition changes are structural, so dropping one means the
//     projection no longer describes the page and the notice says a fresh
//     snapshot is required.
//
// Every threshold is a fraction of the subscription's own granted queue and
// lives in budget_clamp.cc with the rest of the provisional numbers, which are
// [Open (OD-031)]. Nothing here restates one.

namespace taffy {

class DeltaBackpressurePolicy {
 public:
  // What the broker can see about one subscription right now.
  struct Pressure {
    // Undelivered deltas held for this subscription, and their encoded size.
    uint32_t queue_depth = 0;
    uint32_t queued_bytes = 0;
    // The subscription's clamped budget. Both maxima are non-zero after
    // clamping; a zero maximum is treated as "no room at all" rather than as
    // "unlimited", which is the fail-closed reading.
    DeltaBudget budget;
    // What the subscription currently carries and at what scope.
    bool carries_text_deltas = false;
    bool carries_layout_deltas = false;
    ObservationScope scope = ObservationScope::kViewport;
    // True once the stream has already been narrowed to its floor, so the
    // ladder does not offer the same rung twice.
    bool scope_already_reduced = false;
    // Set when the renderer told us it had already coalesced or dropped. A
    // renderer-reported loss is reported onward even when this process is
    // under no pressure of its own.
    bool renderer_reported_loss = false;
    // Set when something already made the projection unusable — a sequence
    // gap, an over-budget delta, an adapter restart, an uninterpretable field.
    bool projection_already_dead = false;
  };

  struct Decision {
    // False when nothing needs to be said. The broker emits no notice at all
    // in that case: a stream of "everything is fine" notices would be noise
    // that a subscriber learns to ignore.
    bool notify = false;
    BackpressureAction action = BackpressureAction::kCoalesced;
    DeltaShedPlan shed;
    std::optional<ObservationScope> reduced_scope;
    bool resnapshot_required = false;
    // The state the subscription moves to. kStopped is never chosen here:
    // stopping is a lifecycle decision the manager owns, not a pressure one.
    SubscriptionState next_state = SubscriptionState::kActive;
  };

  // The single decision function. Pure and total.
  static Decision Evaluate(const Pressure& pressure,
                           const DeltaPressureThresholds& thresholds);

  // Pressure as a percentage of the granted queue, taking the worse of the
  // depth and byte axes. A subscription whose granted maximum is zero is
  // already full by definition, which is why this returns the top of the
  // ladder rather than dividing by zero.
  static uint32_t PressurePercent(const Pressure& pressure);

  // The classes that may be dropped while leaving the projection usable, in
  // drop order. Exposed so the unit test asserts the membership rather than
  // re-deriving it.
  static DeltaShedPlan OptionalShedPlan(bool carries_text,
                                        bool carries_layout);

  // Everything else that is sheddable at all, in drop order. Dropping any of
  // it forces a resnapshot.
  static DeltaShedPlan StructuralShedPlan();

  // One rank narrower, or nullopt when the scope is already at its narrowest.
  static std::optional<ObservationScope> NarrowerScope(ObservationScope scope);
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_BACKPRESSURE_POLICY_H_
