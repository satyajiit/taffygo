// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/delta_backpressure_policy.h"

#include <algorithm>

#include "base/check.h"

namespace taffy {

namespace {

// Appends `delta_class` to `plan` if it is sheddable at all. The optional
// return of SheddableDeltaClass::From is what makes the protected classes
// unrepresentable here; CHECK-ing it would be wrong, because the whole point
// is that a caller cannot get one, so a nullopt is a caller asking for
// something that does not exist rather than a broken invariant.
void AppendIfSheddable(DeltaShedPlan* plan, DeltaClass delta_class) {
  if (std::optional<SheddableDeltaClass> sheddable =
          SheddableDeltaClass::From(delta_class)) {
    plan->classes.push_back(*sheddable);
  }
}

uint32_t PercentOf(uint32_t value, uint32_t maximum) {
  if (maximum == 0) {
    // No room was granted, so any queued work is already over. Reporting the
    // top of the ladder is the fail-closed answer and avoids a division that
    // has no meaning.
    return 100;
  }
  const uint64_t scaled = static_cast<uint64_t>(value) * 100u / maximum;
  return scaled > 100u ? 100u : static_cast<uint32_t>(scaled);
}

}  // namespace

// static
uint32_t DeltaBackpressurePolicy::PressurePercent(const Pressure& pressure) {
  return std::max(
      PercentOf(pressure.queue_depth, pressure.budget.max_queue_depth),
      PercentOf(pressure.queued_bytes, pressure.budget.max_queued_bytes));
}

// static
DeltaShedPlan DeltaBackpressurePolicy::OptionalShedPlan(bool carries_text,
                                                        bool carries_layout) {
  DeltaShedPlan plan;
  // Drop order, cheapest first. Text before layout because a layout change is
  // what a visibility precondition is read from, and a subscriber that has
  // lost layout has lost more than one that has lost text.
  if (carries_text) {
    AppendIfSheddable(&plan, DeltaClass::kText);
  }
  if (carries_layout) {
    AppendIfSheddable(&plan, DeltaClass::kLayout);
  }
  plan.expected_relief = static_cast<uint32_t>(plan.classes.size());
  return plan;
}

// static
DeltaShedPlan DeltaBackpressurePolicy::StructuralShedPlan() {
  DeltaShedPlan plan;
  AppendIfSheddable(&plan, DeltaClass::kAttribute);
  AppendIfSheddable(&plan, DeltaClass::kEdge);
  AppendIfSheddable(&plan, DeltaClass::kNodeAdded);
  plan.expected_relief = static_cast<uint32_t>(plan.classes.size());
  return plan;
}

// static
std::optional<ObservationScope> DeltaBackpressurePolicy::NarrowerScope(
    ObservationScope scope) {
  // Narrowing walks the breadth ranking rather than the enum, because the
  // enum's order is the contract's member order and reading a breadth
  // comparison out of it would break the first time a member is appended.
  ObservationScope best = scope;
  bool found = false;
  for (ObservationScope candidate :
       {ObservationScope::kSelection, ObservationScope::kViewport,
        ObservationScope::kInteractive, ObservationScope::kSection,
        ObservationScope::kDocument}) {
    if (ObservationScopeBreadth(candidate) >= ObservationScopeBreadth(scope)) {
      continue;
    }
    if (!found ||
        ObservationScopeBreadth(candidate) > ObservationScopeBreadth(best)) {
      best = candidate;
      found = true;
    }
  }
  return found ? std::optional<ObservationScope>(best) : std::nullopt;
}

// static
DeltaBackpressurePolicy::Decision DeltaBackpressurePolicy::Evaluate(
    const Pressure& pressure,
    const DeltaPressureThresholds& thresholds) {
  Decision decision;

  // A projection that is already dead skips the ladder. There is nothing left
  // to protect by shedding, and pausing a stream whose projection cannot be
  // applied would delay the resnapshot that is the only way back.
  if (pressure.projection_already_dead) {
    decision.notify = true;
    decision.action = BackpressureAction::kResnapshotRequested;
    decision.resnapshot_required = true;
    decision.next_state = SubscriptionState::kAwaitingResnapshot;
    return decision;
  }

  const uint32_t percent = PressurePercent(pressure);

  // Rung 4: the queue is full. Whatever else was true, something has been lost
  // that nobody can name, so the projection is abandoned rather than repaired.
  if (percent >= thresholds.resnapshot_percent) {
    decision.notify = true;
    decision.action = BackpressureAction::kResnapshotRequested;
    // Everything sheddable is listed, in drop order, so the notice says
    // precisely what stopped being delivered. The protected classes are absent
    // from both plans by construction.
    decision.shed = OptionalShedPlan(pressure.carries_text_deltas,
                                     pressure.carries_layout_deltas);
    for (const SheddableDeltaClass& structural : StructuralShedPlan().classes) {
      decision.shed.classes.push_back(structural);
    }
    decision.shed.expected_relief =
        static_cast<uint32_t>(decision.shed.classes.size());
    decision.resnapshot_required = true;
    decision.next_state = SubscriptionState::kAwaitingResnapshot;
    return decision;
  }

  // Rung 3: suspend delivery. Handles stay valid and the projection stays
  // applicable; it simply stops advancing until the subscriber catches up.
  if (percent >= thresholds.pause_percent) {
    decision.notify = true;
    decision.action = BackpressureAction::kSubscriptionPaused;
    decision.shed = OptionalShedPlan(pressure.carries_text_deltas,
                                     pressure.carries_layout_deltas);
    decision.next_state = SubscriptionState::kPaused;
    return decision;
  }

  // Rung 2: narrow what is observed. Preferred over pausing because a narrower
  // stream still carries the lifecycle and removal signals that keep a
  // subscriber's handles honest.
  if (percent >= thresholds.reduce_scope_percent &&
      !pressure.scope_already_reduced) {
    if (std::optional<ObservationScope> narrower =
            NarrowerScope(pressure.scope)) {
      decision.notify = true;
      decision.action = BackpressureAction::kScopeReduced;
      decision.reduced_scope = narrower;
      decision.shed = OptionalShedPlan(pressure.carries_text_deltas,
                                       pressure.carries_layout_deltas);
      decision.next_state = SubscriptionState::kActive;
      return decision;
    }
  }

  // Rung 1: give up the optional signals. This is the only rung that can be
  // taken while the projection stays fully usable, which is why it is first.
  if (percent >= thresholds.shed_optional_percent) {
    DeltaShedPlan plan = OptionalShedPlan(pressure.carries_text_deltas,
                                          pressure.carries_layout_deltas);
    if (!plan.is_empty()) {
      decision.notify = true;
      decision.action = BackpressureAction::kCoalesced;
      decision.shed = std::move(plan);
      decision.next_state = SubscriptionState::kActive;
      return decision;
    }
    // Nothing optional left to give up and the queue is still filling. Say so
    // rather than staying silent: the next sample will reach a higher rung,
    // and a subscriber that knows it is close to the pause threshold can slow
    // down first.
    decision.notify = true;
    decision.action = BackpressureAction::kCoalesced;
    decision.next_state = SubscriptionState::kActive;
    return decision;
  }

  // Below every threshold. The only thing still worth reporting is a loss the
  // renderer already took, because that loss is real whatever this process's
  // own queue looks like.
  if (pressure.renderer_reported_loss) {
    decision.notify = true;
    decision.action = BackpressureAction::kCoalesced;
    decision.next_state = SubscriptionState::kActive;
    return decision;
  }
  return decision;
}

}  // namespace taffy
