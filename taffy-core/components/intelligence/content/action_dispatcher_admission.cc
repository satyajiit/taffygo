// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Steps 4 and 6 of the section 12 sequence: whether the request may be
// admitted at all, and whether every declared precondition holds against what
// the renderer resolved. Both are decisions over values; neither performs an
// effect, which is what lets the whole sequence be exercised as a table.

#include <string>

#include "base/time/time.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "taffy/components/intelligence/content/action_precondition_evaluator.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"

namespace taffy {

// --- step 4 -----------------------------------------------------------------

std::optional<ActionResultCode> ActionDispatcher::AdmitOrRefuse(
    PendingAction& pending,
    const ContentDigest& authorized,
    const ContentDigest& computed,
    base::TimeTicks now) {
  // An action that declares no expected effect, or one whose declared effects
  // carry no observable claim, cannot be verified — and an unverifiable action
  // is not an authorized action (protocol section 11.6).
  if (!AllPostconditionsAreVerifiable(pending.postconditions())) {
    return ActionResultCode::kDeniedByPolicy;
  }
  // A browser flow can only be corroborated by the layer that owns downloads,
  // file choosers and permission prompts. With no such source installed, the
  // honest answer is that this build cannot verify the claim.
  for (const Postcondition& postcondition : pending.postconditions()) {
    if (postcondition.kind == PostconditionKind::kBrowserFlowStarted &&
        !browser_effects_) {
      return ActionResultCode::kUnsupported;
    }
  }
  const MonotonicMillis deadline =
      pending.envelope ? pending.envelope->absolute_deadline_monotonic_ms
                       : pending.command->absolute_deadline_monotonic_ms;
  if (deadline == 0 || FromMonotonicMs(deadline) <= now) {
    return ActionResultCode::kDeniedByPolicy;
  }

  // Step 4: consume the matching capability in the browser process before any
  // renderer command exists. Admit marks it in flight, so no later path can
  // spend it again.
  CapabilityAdmission admission = CapabilityAdmission::kMalformed;
  if (pending.admit_from_task_grant && pending.envelope) {
    admission = capabilities_->AdmitTaskAction(*pending.envelope, *leases_,
                                               pending.tab_id(), now);
  } else if (pending.admit_from_task_grant && pending.command) {
    admission = capabilities_->AdmitTaskBrowserCommand(
        *pending.command, *leases_, pending.tab_id(), now);
  } else {
    admission = capabilities_->Admit(pending.capability(), authorized, computed,
                                     *leases_, pending.tab_id(), now);
  }
  if (admission != CapabilityAdmission::kAdmitted) {
    return AdmissionToResultCode(admission);
  }
  pending.capability_admitted = true;
  return std::nullopt;
}

std::optional<ActionResultCode> ActionDispatcher::RevalidateAdmittedAuthority(
    const PendingAction& pending,
    base::TimeTicks now) const {
  if (!pending.capability_admitted ||
      !capabilities_->IsSpent(pending.capability().capability_reference) ||
      !leases_->IsValidFor(pending.capability().actor_lease_id,
                           pending.tab_id(), now)) {
    return ActionResultCode::kCancelledByUser;
  }
  const MonotonicMillis deadline =
      pending.envelope ? pending.envelope->absolute_deadline_monotonic_ms
                       : pending.command->absolute_deadline_monotonic_ms;
  return deadline != 0 && FromMonotonicMs(deadline) > now
             ? std::nullopt
             : std::make_optional(ActionResultCode::kDeniedByPolicy);
}

// --- step 6 -----------------------------------------------------------------

std::optional<ActionPreconditionFailure>
ActionDispatcher::EvaluatePreconditions(
    const AuthorizedActionEnvelope& envelope,
    const ResolvedNodeFacts& facts) const {
  // Admission checked the lease before consuming the capability. Read it once
  // more beside the fresh node facts: direct user input revokes it, and a
  // declared NoUserInteractionSinceLease condition must never rely on the
  // earlier answer merely because node resolution was asynchronous.
  const bool lease_still_valid = leases_->IsValidFor(
      envelope.capability.actor_lease_id, envelope.target_handle.tab_id,
      base::TimeTicks::Now());
  return EvaluateBrowserActionPreconditions(envelope, facts, lease_still_valid);
}

}  // namespace taffy
