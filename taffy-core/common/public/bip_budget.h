// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_BUDGET_H_
#define TAFFY_PUBLIC_BIP_BUDGET_H_

#include <stdint.h>

#include <vector>

#include "taffy/common/public/bip_identity.h"

// Observation budgets and the policy grant they are clamped against
// (protocol section 7.1, taffy-core/contracts/bip/schema/protocol-info.schema.json and
// snapshot.schema.json).
//
// Two independent narrowings apply to every request, in this order:
//
//   1. the process-safe ceiling, which protects renderer and browser health
//      and is identical for every task; then
//   2. the policy grant already issued to the task, which is what the user and
//      the policy engine agreed to.
//
// Both are pure narrowings. Every axis of the result is at most the
// corresponding axis of every input, and no field of a request can raise a
// ceiling or widen a grant. The broker checks that property on every call and
// the unit tests assert it as a postcondition, because "a request can never
// widen the granted policy" is a security property, not a performance guard.
//
// Neither enumeration below is ordered by breadth or by strictness: both match
// the contract's member order exactly, and the ranking functions are where the
// ordering actually lives. Relying on enum values for a security comparison
// would break the first time the contract appends a member.

namespace taffy {

enum class ObservationScope : uint8_t {
  kViewport = 0,
  kInteractive = 1,
  kSelection = 2,
  kSection = 3,
  kDocument = 4,
};

enum class AdapterKind : uint8_t {
  kDom = 0,
  kAccessibility = 1,
  kForms = 2,
  kMetadata = 3,
  kBrowser = 4,
  kDocumentViewer = 5,
  kMedia = 6,
  kSite = 7,
  kVision = 8,
  // Appended by the minor contract step recorded in
  // taffy-core/contracts/bip/schema/bip.version.json. The values above keep the numbers
  // they had, which is the whole point of appending: this enumeration is
  // persisted in observation records, and renumbering it would change what a
  // stored record says.
  kSelection = 9,
  kLayout = 10,
};

enum class Sensitivity : uint8_t {
  kNotSensitive = 0,
  kPersonal = 1,
  kAccount = 2,
  kPayment = 3,
  kIdentity = 4,
  kHealth = 5,
  kFinancial = 6,
  kLegal = 7,
  kPrivateCommunication = 8,
  kAdministration = 9,
  kCredential = 10,
  kUnknownSensitive = 11,
  kOneTimeCode = 12,
  kChallengeResponse = 13,
};

// How much of a page a scope can reach, from narrowest to widest. Clamping a
// requested scope against a granted scope is a minimum of these ranks.
constexpr uint8_t ObservationScopeBreadth(ObservationScope scope) {
  switch (scope) {
    case ObservationScope::kSelection:
      return 1;
    case ObservationScope::kViewport:
      return 2;
    case ObservationScope::kInteractive:
      return 3;
    case ObservationScope::kSection:
      return 4;
    case ObservationScope::kDocument:
      return 5;
  }
  // Unreachable for a valid value. Returning the widest rank would be the
  // dangerous default, so an invalid value ranks as the narrowest possible
  // and therefore grants nothing.
  return 0;
}

// How strictly a class must be handled, least to most. kUnknownSensitive
// deliberately ranks above every named class: a value nobody could classify is
// handled at least as strictly as its destination policy requires, and page
// instructions can never lower it (protocol section 9.2).
constexpr uint8_t SensitivityStrictness(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::kNotSensitive:
      return 0;
    case Sensitivity::kPersonal:
      return 1;
    case Sensitivity::kAccount:
      return 2;
    case Sensitivity::kPayment:
      return 3;
    case Sensitivity::kIdentity:
      return 4;
    case Sensitivity::kHealth:
      return 5;
    case Sensitivity::kFinancial:
      return 6;
    case Sensitivity::kLegal:
      return 7;
    case Sensitivity::kPrivateCommunication:
      return 8;
    case Sensitivity::kAdministration:
      return 9;
    case Sensitivity::kCredential:
      return 10;
    case Sensitivity::kOneTimeCode:
    case Sensitivity::kChallengeResponse:
      return 10;
    case Sensitivity::kUnknownSensitive:
      return 11;
  }
  // An unrecognized class is the strictest thing there is.
  return 255;
}

// Client-requested extraction bounds. Zero means "unset", and an unset field
// is filled from the ceiling rather than treated as unlimited.
struct ObservationBudget {
  uint32_t max_nodes = 0;
  uint32_t max_text_bytes = 0;
  uint32_t max_total_bytes = 0;
  uint32_t max_depth = 0;
  uint32_t max_frames = 0;
  uint32_t deadline_ms = 0;

  friend bool operator==(const ObservationBudget&,
                         const ObservationBudget&) = default;
};

// The process-safe ceiling. Identical for every task and every origin; it
// exists to protect renderer and browser health, not to express policy. Its
// fields mirror ProtocolLimits in the contract so that an endpoint's reported
// limits and the process ceiling can be compared without a translation step.
//
// The values are owned by exactly one place: budget_clamp.cc in
// //taffy/browser. They are provisional until the device-floor
// measurements ratify them, which is [Open (OD-031)]. Nothing else in the
// tree, in any document and in any comment, restates them.
struct ProcessBudgetLimits {
  uint32_t max_message_bytes = 0;
  uint32_t max_nodes = 0;
  uint32_t max_text_bytes = 0;
  uint32_t max_total_bytes = 0;
  uint32_t max_depth = 0;
  uint32_t max_frames = 0;
  uint32_t max_delta_queue_depth = 0;
  uint32_t max_snapshot_deadline_ms = 0;
  uint32_t min_delta_interval_ms = 0;
};

// What the policy engine already granted this task. The task engine proposes; this
// is the ceiling the proposal is measured against.
struct ObservationPolicyGrant {
  ObservationBudget budget;
  ObservationScope max_scope = ObservationScope::kViewport;
  // The document this grant was decided against: the origin that must still be
  // committed in the root frame when the request arrives.
  //
  // It is deliberately not `allowed_origins` below. That list is the
  // cross-origin CHILD-frame allowlist, and its documented empty case means
  // "the root frame's own origin only" — a sentence about whichever document
  // the root happens to be showing, which is not a binding at all. So a grant
  // decided against the site a person asked for admitted an observation of
  // whatever site a redirect substituted for it, and the two fields reading
  // alike is most of why nobody saw it. Naming the root separately is what
  // lets the browser say no.
  //
  // An invalid value admits nothing. A grant that cannot name the document it
  // is about is not a grant that covers every document, and the failure has to
  // land on whoever left it unset rather than on the person being observed.
  Origin document_origin;
  // Empty means "the root frame's own origin only", never "no restriction".
  std::vector<Origin> allowed_origins;
  bool may_include_child_frames = false;
  // The strictest class this task may observe at all. Anything stricter is
  // omitted by the renderer and counted in the redaction summary.
  Sensitivity max_sensitivity = Sensitivity::kNotSensitive;
  // Adapters the task may use. An adapter outside this list is never
  // requested, even when the endpoint supports it.
  std::vector<AdapterKind> allowed_adapters;
};

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_BUDGET_H_
