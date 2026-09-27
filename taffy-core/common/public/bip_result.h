// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_RESULT_H_
#define TAFFY_PUBLIC_BIP_RESULT_H_

#include <stdint.h>

// Result taxonomies (protocol section 11.7,
// taffy-core/contracts/bip/schema/action.schema.json and snapshot.schema.json).
//
// Result codes are security significant. Two rules apply to every consumer, in
// C++, in Rust and in the UI layer:
//
//   * An unknown code fails closed. There is no "treat it as success"
//     fallback and no numeric range that means "probably fine".
//   * User-facing text is generated from trusted local templates keyed by the
//     code. A renderer-authored string never reaches a user.
//
// Member order matches the contract exactly. The helper predicates below,
// not the numeric values, are what code is allowed to reason with.

namespace taffy {

enum class ActionResultCode : uint8_t {
  // The one and only success. Reached only after the postcondition verifier
  // corroborated the declared effect against browser-owned state or a fresh
  // observation. Renderer acknowledgement alone never produces it
  // (protocol section 11.6).
  kVerified = 0,

  kDeniedByPolicy = 1,
  kApprovalRequired = 2,
  kApprovalDenied = 3,
  kActorLeaseMissing = 4,
  kCapabilityExpired = 5,
  kTabGone = 6,
  kFrameGone = 7,
  kDocumentInactive = 8,
  kStalePageEpoch = 9,
  kStaleGraph = 10,
  kNodeGone = 11,
  kOriginChanged = 12,
  kRoleOrActionChanged = 13,
  kNotVisible = 14,
  kOccluded = 15,
  kNotEnabled = 16,
  kNotEditable = 17,
  kSensitiveField = 18,
  kDestinationChanged = 19,
  kUnsupported = 20,
  kBudgetExceeded = 21,
  kDispatchFailed = 22,
  kNavigationStarted = 23,
  kPostconditionTimeout = 24,
  kPostconditionFailed = 25,
  kCancelledByUser = 26,
  kCancelledByNavigation = 27,
  kRendererCrashed = 28,

  // The side effect may or may not have happened and the browser cannot prove
  // which. Never retried automatically when repetition could cause an external
  // effect (domain model section 12.1).
  kOutcomeUnknown = 29,

  kInternalError = 30,

  // Appended, never inserted: the code is persisted in an action attempt, so
  // renumbering would change the meaning of records already written.
  kEgressNotAuthorized = 31,
  kDestinationClassRestricted = 32,
  kUntrustedContentOrigin = 33,
  kPreparedEffectChanged = 34,
  kCommitWithoutPrepare = 35,
  // A document the browser knows has moved, and that nobody has observed
  // since. Split from kStaleGraph at 0.7, which had been answering this and
  // "your revision floor is behind" with one code — two situations whose next
  // moves differ, and the conflation cost a real investigation its first day.
  kGraphMovedDuringPreflight = 36,
  // A journalled form action could not spend the exact browser-owned value it
  // named. This is not a policy decision: the person must supply a fresh
  // value before the action can be proposed again.
  kValueReferenceUnknown = 37,
};

enum class ObservationResultCode : uint8_t {
  kOk = 0,
  kUnsupported = 1,
  kIncomplete = 2,
  kConflicted = 3,
  kStalePageEpoch = 4,
  kDocumentInactive = 5,
  kBudgetExceeded = 6,
  kDeadlineExceeded = 7,
  kCancelled = 8,
  kResourcePressure = 9,
  kInternalError = 10,
};

// Total closed-enum admission for values read from durable storage or another
// byte-oriented boundary. Keep this exhaustive: a hole or a newly appended
// code must be an explicit review, never accepted because it falls inside a
// numeric range.
constexpr bool IsKnownActionResultCode(ActionResultCode code) {
  switch (code) {
    case ActionResultCode::kVerified:
    case ActionResultCode::kDeniedByPolicy:
    case ActionResultCode::kApprovalRequired:
    case ActionResultCode::kApprovalDenied:
    case ActionResultCode::kActorLeaseMissing:
    case ActionResultCode::kCapabilityExpired:
    case ActionResultCode::kTabGone:
    case ActionResultCode::kFrameGone:
    case ActionResultCode::kDocumentInactive:
    case ActionResultCode::kStalePageEpoch:
    case ActionResultCode::kStaleGraph:
    case ActionResultCode::kNodeGone:
    case ActionResultCode::kOriginChanged:
    case ActionResultCode::kRoleOrActionChanged:
    case ActionResultCode::kNotVisible:
    case ActionResultCode::kOccluded:
    case ActionResultCode::kNotEnabled:
    case ActionResultCode::kNotEditable:
    case ActionResultCode::kSensitiveField:
    case ActionResultCode::kDestinationChanged:
    case ActionResultCode::kUnsupported:
    case ActionResultCode::kBudgetExceeded:
    case ActionResultCode::kDispatchFailed:
    case ActionResultCode::kNavigationStarted:
    case ActionResultCode::kPostconditionTimeout:
    case ActionResultCode::kPostconditionFailed:
    case ActionResultCode::kCancelledByUser:
    case ActionResultCode::kCancelledByNavigation:
    case ActionResultCode::kRendererCrashed:
    case ActionResultCode::kOutcomeUnknown:
    case ActionResultCode::kInternalError:
    case ActionResultCode::kEgressNotAuthorized:
    case ActionResultCode::kDestinationClassRestricted:
    case ActionResultCode::kUntrustedContentOrigin:
    case ActionResultCode::kPreparedEffectChanged:
    case ActionResultCode::kCommitWithoutPrepare:
    case ActionResultCode::kGraphMovedDuringPreflight:
    case ActionResultCode::kValueReferenceUnknown:
      return true;
  }
  return false;
}

// True only for kVerified. Written as a function so that a reader of the
// dispatch path can see there is exactly one success code, and so that adding
// a code cannot accidentally widen success.
constexpr bool IsActionSuccess(ActionResultCode code) {
  return code == ActionResultCode::kVerified;
}

// True when the code says the caller's handle is dead and a fresh observation
// is the only legal next step. On any of these the core service must not retry the
// old handle, resolve by selector, text, ordinal or coordinates, broaden
// scope, or reuse the old approval (protocol section 12).
constexpr bool RequiresFreshObservation(ActionResultCode code) {
  switch (code) {
    case ActionResultCode::kStalePageEpoch:
    case ActionResultCode::kStaleGraph:
    // The whole content of this code is that the document moved and nobody has
    // looked since, so a fresh observation is not merely the legal next step,
    // it is the only one. Note that this function has a default arm, which is
    // why the membership has to be added by hand: a new code that belongs here
    // and is left out answers "no" and compiles.
    case ActionResultCode::kGraphMovedDuringPreflight:
    case ActionResultCode::kNodeGone:
    case ActionResultCode::kOriginChanged:
    case ActionResultCode::kRoleOrActionChanged:
    case ActionResultCode::kDestinationChanged:
    case ActionResultCode::kCancelledByNavigation:
      return true;
    default:
      return false;
  }
}

// True when the browser could not establish whether the effect happened.
// Recorded alongside the terminal result so that no consumer has to re-derive
// it and get it wrong.
constexpr bool IsAmbiguousOutcome(ActionResultCode code) {
  return code == ActionResultCode::kOutcomeUnknown ||
         code == ActionResultCode::kRendererCrashed ||
         code == ActionResultCode::kPostconditionTimeout;
}

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_RESULT_H_
