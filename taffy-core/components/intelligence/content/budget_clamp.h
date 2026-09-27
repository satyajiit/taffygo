// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BUDGET_CLAMP_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BUDGET_CLAMP_H_

#include <stdint.h>

#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_subscription.h"

// Budget and policy clamping (protocol section 7.1).
//
// "The broker clamps all client budgets to process-safe maximums. A request
// cannot increase the data policy or source scope already granted to the
// task." Both halves of that sentence are security properties, so both are
// implemented as pure functions with a checkable postcondition rather than as
// scattered min() calls in the request path.
//
// The postcondition, asserted on every clamp in debug builds and proven
// exhaustively by budget_clamp_unittest.cc: for every axis of the result,
// result is at most the request, at most the process ceiling, and at most the
// grant. There is no axis on which a request can raise a ceiling, add an
// adapter, add an origin, widen a scope, or loosen a sensitivity class.

namespace taffy {

// The process-safe ceiling. Every task and every origin gets the same one.
const ProcessBudgetLimits& GetProcessBudgetLimits();

// Narrows `requested` against the process ceiling and then against the task's
// grant. Never widens any axis.
//
// A request naming an origin, adapter or scope outside the grant loses it here
// and is told through the envelope's warning codes rather than by having the
// whole observation rejected. A caller asking for more than it may have is an
// ordinary planning outcome, not an attack, and failing the request outright
// would push callers toward asking for the minimum and then re-asking.
ObservationRequest ClampObservationRequest(const ObservationRequest& requested,
                                           const ObservationPolicyGrant& grant,
                                           const ProcessBudgetLimits& limits);

// True when every axis of `narrow` is at most the corresponding axis of
// `wide`. Used for the clamp postcondition and to refuse a widening
// replacement grant.
bool IsNarrowerOrEqual(const ObservationPolicyGrant& narrow,
                       const ObservationPolicyGrant& wide);

// Clamps one duration against the process deadline ceiling. Zero means unset
// and becomes the ceiling; anything above the ceiling becomes the ceiling.
uint32_t ClampDeadlineMs(uint32_t requested_ms,
                         const ProcessBudgetLimits& limits);

// Clamps a postcondition verifier's deadline (decision 0169).
//
// A separate ceiling from [ClampDeadlineMs] because it bounds a different
// thing. A snapshot deadline is how long a renderer may spend building an
// observation and must stay short; a postcondition deadline is how long the
// browser will wait for a site to answer a request it has already sent, and
// a site on a phone routinely takes longer than a renderer ever may. Nothing
// in the shipping path sets `Postcondition::timeout_ms`, so every verifier
// took the zero branch and every navigation this product dispatched was
// contradicted after the snapshot ceiling — two seconds. A cold Google
// results page took 2,006 milliseconds, and the errand died on its first
// move with the page it had asked for on the screen.
//
// Zero means unset and becomes the ceiling, exactly as above.
uint32_t ClampPostconditionDeadlineMs(uint32_t requested_ms);

// The fractions of a subscription's granted queue at which the broker escalates
// its response to pressure. Provisional in exactly the same way, and for
// exactly the same reason, as the process ceiling above: the ratified values
// come from measurements on the supported device floor and are [Open
// (OD-031)]. They live in this file so that every provisional numeric bound in
// the browser half has one owner.
//
// Expressed as percentages of the granted queue rather than as absolute
// depths, because the granted queue is itself clamped per subscription: a
// fraction stays meaningful when the ceiling moves, and an absolute depth
// would have to move with it.
struct DeltaPressureThresholds {
  // Below this, nothing is shed.
  uint32_t shed_optional_percent = 0;
  // Above this, observation scope narrows by one rank.
  uint32_t reduce_scope_percent = 0;
  // Above this, delivery is suspended.
  uint32_t pause_percent = 0;
  // At or above this, the projection is abandoned and a fresh snapshot is
  // required. Always 100: a full queue has already lost something.
  uint32_t resnapshot_percent = 0;
};

const DeltaPressureThresholds& GetDeltaPressureThresholds();

// Narrows a subscription request against the process ceiling and then against
// the task's grant, exactly as ClampObservationRequest does for a one-shot
// observation.
//
// Two axes behave differently from an observation's and both are deliberate:
//
//   * coalescing_window_ms is clamped UPWARD to the endpoint's minimum delta
//     interval. Every other axis narrows; this one is a floor, because a
//     client asking for updates faster than the endpoint will produce them
//     must not be told it will get them.
//   * an optional signal the grant does not allow is switched off rather than
//     rejected, so a subscriber that asked for text deltas it may not have
//     still gets the stream it may have.
SubscriptionRequest ClampSubscriptionRequest(
    const SubscriptionRequest& requested,
    const ObservationPolicyGrant& grant,
    const ProcessBudgetLimits& limits,
    const ProcessBudgetLimits& endpoint_limits);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BUDGET_CLAMP_H_
