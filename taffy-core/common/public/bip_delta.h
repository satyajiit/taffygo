// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_DELTA_H_
#define TAFFY_PUBLIC_BIP_DELTA_H_

#include <stdint.h>

#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"

// Incremental page updates as the isolated Rust core receives them
// (protocol section 10, taffy-core/contracts/bip/schema/delta.schema.json).
//
// One sentence from the protocol shapes every type in this header: "Delta
// streams are an optimization. Correctness can always fall back to a bounded
// fresh snapshot." So nothing here is allowed to be approximately right. A
// delta either applies exactly — same epoch, same from-revision, next
// sequence — or the subscriber's projection is thrown away and a fresh
// snapshot is taken. There is no third answer, and DeltaApplicability below
// is the only function permitted to decide which of the two it is.
//
// The second rule is the drop order, and it is expressed in the type system
// rather than in a comment. Optional text and layout changes are dropped
// before lifecycle or node-removal signals, because a subscriber that loses a
// text change is merely out of date, while a subscriber that loses a removal
// is holding a handle to something that no longer exists. SheddableDeltaClass
// makes the second case unrepresentable: an interface that sheds work takes
// that type, and there is no way to build one from kNodeRemoved or
// kLifecycle.

namespace taffy {

// A class of change carried by a delta. Members and order match the
// contract's DeltaCategory exactly; the ordering below is the contract's, not
// the drop order, and no code may read one as the other.
enum class DeltaClass : uint8_t {
  kText = 0,
  kLayout = 1,
  kAttribute = 2,
  kEdge = 3,
  kNodeAdded = 4,
  kNodeRemoved = 5,
  kLifecycle = 6,
};

// True when the class may be dropped to relieve pressure. False for the two
// classes whose loss would leave a subscriber acting on a node that is gone.
//
// Written as an exhaustive switch so that a class appended to the contract is
// a compile error here rather than a silently sheddable signal.
constexpr bool IsSheddableDeltaClass(DeltaClass delta_class) {
  switch (delta_class) {
    case DeltaClass::kText:
    case DeltaClass::kLayout:
    case DeltaClass::kAttribute:
    case DeltaClass::kEdge:
    case DeltaClass::kNodeAdded:
      return true;
    case DeltaClass::kNodeRemoved:
    case DeltaClass::kLifecycle:
      return false;
  }
  // An unrecognized value is not sheddable. Fail closed: the safe mistake is
  // keeping a signal that could have been dropped, never dropping one that
  // could not.
  return false;
}

// The order in which sheddable classes are given up, cheapest first. Lower
// rank is dropped earlier. Ranks exist only for sheddable classes, which is
// why this function is private to SheddableDeltaClass below rather than
// taking a bare DeltaClass: a rank for kNodeRemoved would be a rank somebody
// could act on.
namespace internal {

constexpr uint8_t SheddableDeltaClassRank(DeltaClass delta_class) {
  switch (delta_class) {
    case DeltaClass::kText:
      return 0;
    case DeltaClass::kLayout:
      return 1;
    case DeltaClass::kAttribute:
      return 2;
    case DeltaClass::kEdge:
      return 3;
    case DeltaClass::kNodeAdded:
      return 4;
    case DeltaClass::kNodeRemoved:
    case DeltaClass::kLifecycle:
      break;
  }
  // Unreachable through SheddableDeltaClass, which refuses these values at
  // construction. Returning the highest rank means that if the refusal were
  // ever bypassed the class would still be the last thing dropped.
  return 255;
}

}  // namespace internal

// A delta class that is allowed to be dropped, and nothing else.
//
// The constructor is private and the only factory returns nullopt for
// kNodeRemoved and kLifecycle, so a shedding interface that takes this type
// cannot be handed a signal it must not drop. That turns "optional text and
// layout deltas are dropped before lifecycle or node-removal signals" from a
// rule a reviewer has to notice into a rule the compiler enforces.
class SheddableDeltaClass {
 public:
  static constexpr std::optional<SheddableDeltaClass> From(
      DeltaClass delta_class) {
    if (!IsSheddableDeltaClass(delta_class)) {
      return std::nullopt;
    }
    return SheddableDeltaClass(delta_class);
  }

  constexpr DeltaClass value() const { return value_; }

  // Lower is dropped first.
  constexpr uint8_t drop_rank() const {
    return internal::SheddableDeltaClassRank(value_);
  }

  friend constexpr bool operator==(const SheddableDeltaClass&,
                                   const SheddableDeltaClass&) = default;

  // Orders by drop rank, so a container of these sheds in the right order
  // without every call site restating the policy.
  friend constexpr auto operator<=>(const SheddableDeltaClass& lhs,
                                    const SheddableDeltaClass& rhs) {
    return lhs.drop_rank() <=> rhs.drop_rank();
  }

 private:
  explicit constexpr SheddableDeltaClass(DeltaClass delta_class)
      : value_(delta_class) {}

  DeltaClass value_;
};

// Every sheddable class, already in drop order. The static assertions below
// are the guard: reordering the ranks, or making a protected class sheddable,
// stops the build here.
inline constexpr DeltaClass kDeltaShedOrder[] = {
    DeltaClass::kText,     DeltaClass::kLayout,   DeltaClass::kAttribute,
    DeltaClass::kEdge,     DeltaClass::kNodeAdded,
};

static_assert(std::size(kDeltaShedOrder) == 5,
              "Every sheddable delta class must appear in the shed order, and "
              "no protected class may appear in it.");
static_assert(internal::SheddableDeltaClassRank(DeltaClass::kText) <
                  internal::SheddableDeltaClassRank(DeltaClass::kNodeAdded),
              "Optional text deltas are dropped before node additions.");
static_assert(!IsSheddableDeltaClass(DeltaClass::kNodeRemoved),
              "A node-removal signal is never dropped: a subscriber that lost "
              "one would hold a handle to a node that no longer exists.");
static_assert(!IsSheddableDeltaClass(DeltaClass::kLifecycle),
              "A lifecycle signal is never dropped: it is the signal that says "
              "the whole projection is dead.");

// Why a delta could not be applied to the subscriber's projection. Each of
// these forces a fresh snapshot except kNone and kDuplicate: a duplicate is
// discarded because it was already applied, so the projection is still
// correct (protocol section 6.3).
enum class DeltaRejectReason : uint8_t {
  kNone = 0,
  // Already applied. Discarded, projection intact.
  kDuplicate = 1,
  // Arrived after a later sequence. Discarded, projection intact, but counted:
  // sustained reordering is a transport defect worth seeing.
  kOutOfOrder = 2,
  // A sequence number was skipped. The projection is missing changes nobody
  // can name, so it is dead.
  kSequenceGap = 3,
  // The delta describes a different document.
  kEpochMismatch = 4,
  // The delta starts from a revision the subscriber does not hold.
  kRevisionMismatch = 5,
  // The subscription is paused, stopped, or already awaiting a resnapshot.
  kSubscriptionNotApplying = 6,
  // The delta exceeded the subscription's own byte or node budget.
  kOverBudget = 7,
  // The renderer restarted an adapter, so the projection's provenance is
  // broken even where the node identifiers still match.
  kAdapterRestart = 8,
  // A field the subscriber cannot interpret. Decoding still succeeded; the
  // projection cannot be proven correct, so it resnapshots rather than
  // guessing (taffy-core/contracts/bip delta.schema.json, InvalidationReason).
  kUnknownField = 9,
};

// True when the reason leaves the subscriber's projection intact. Exactly two
// reasons do: a delta that was already applied, and one that lost a race with
// a delta that was.
constexpr bool ProjectionSurvives(DeltaRejectReason reason) {
  return reason == DeltaRejectReason::kNone ||
         reason == DeltaRejectReason::kDuplicate ||
         reason == DeltaRejectReason::kOutOfOrder;
}

// What a subscriber is told when a delta killed its projection.
//
// A reject reason and an invalidation code are two vocabularies for one event:
// the browser decides with the first and reports with the second. The mapping
// is written here, beside the decision, because a table kept at the reporting
// site would drift from the one that did the deciding.
//
// The contract carries SEQUENCE_GAP, DELTA_OVERFLOW, UNKNOWN_DELTA_FIELD and
// ADAPTER_RESTART for precisely these broker-detected failures
// (taffy-core/contracts/bip/schema/delta.schema.json, InvalidationReason). A subscriber
// told only "resnapshot" cannot tell a change that was lost from a document
// that is gone, and section 6.3 gives it the right to know which.
//
// Nullopt for the four reasons that name no invalidation: kNone, kDuplicate
// and kOutOfOrder leave the projection intact, and kSubscriptionNotApplying
// describes a projection that was already dead and already reported, so
// announcing it again would say the same death once per refused message.
constexpr std::optional<InvalidationCode> InvalidationCodeForDeltaReject(
    DeltaRejectReason reason) {
  switch (reason) {
    case DeltaRejectReason::kNone:
    case DeltaRejectReason::kDuplicate:
    case DeltaRejectReason::kOutOfOrder:
    case DeltaRejectReason::kSubscriptionNotApplying:
      return std::nullopt;
    case DeltaRejectReason::kSequenceGap:
      return InvalidationCode::kSequenceGap;
    case DeltaRejectReason::kOverBudget:
      return InvalidationCode::kDeltaOverflow;
    case DeltaRejectReason::kAdapterRestart:
      return InvalidationCode::kAdapterRestart;
    case DeltaRejectReason::kUnknownField:
      return InvalidationCode::kUnknownDeltaField;
    case DeltaRejectReason::kEpochMismatch:
    case DeltaRejectReason::kRevisionMismatch:
      // Neither retires the document: what the delta described did not apply
      // to what the subscriber holds, and the epoch it holds is still live, so
      // a fresh snapshot of the same epoch is the way back.
      return InvalidationCode::kBrokerInvalidation;
  }
  // An unrecognized reason still killed the projection. Saying so without
  // naming a cause is the fail-closed answer; staying silent would leave a
  // subscriber applying deltas onto a projection the browser abandoned.
  return InvalidationCode::kBrokerInvalidation;
}

// What a subscriber currently holds. A delta applies only against exactly
// this (protocol section 10).
struct DeltaProjectionCursor {
  PageEpoch page_epoch;
  GraphRevision revision = 0;
  EventSequence event_sequence = 0;

  friend bool operator==(const DeltaProjectionCursor&,
                         const DeltaProjectionCursor&) = default;
};

// One incremental update, as the core service receives it.
//
// The changed nodes themselves travel as an opaque encoded payload for the
// same reason a snapshot's do: browser-process C++ does not walk untrusted
// structure field by field (bip_observation.h explains the Rule of Two
// argument). What is in this struct is the small set of scalars the browser
// checked, plus the counts a subscriber needs to decide whether the change
// matters to its task.
struct DeltaEnvelope {
  std::string schema_version;
  SubscriptionId subscription_id;
  TabId tab_id;
  FrameId frame_id;
  PageEpoch page_epoch;
  GraphRevision from_revision = 0;
  GraphRevision to_revision = 0;
  EventSequence event_sequence = 0;

  // How many renderer mutations the adapter folded into this delta. Coalescing
  // never spans an action preflight and dispatch boundary in a way that hides
  // a changed precondition (protocol section 5.3).
  uint32_t coalesced_mutation_count = 0;

  uint32_t added_node_count = 0;
  uint32_t changed_node_count = 0;
  uint32_t removed_node_count = 0;
  uint32_t changed_edge_count = 0;

  // Identifiers retired by this delta. Carried in full rather than counted,
  // because a subscriber has to know precisely which handles died: a retired
  // identifier is never reissued inside its epoch, so this is the one list a
  // resnapshot could not reconstruct after the fact.
  std::vector<SemanticNodeId> removed_node_ids;

  TruncationSummary truncation;
  MonotonicMillis observed_at_monotonic_ms = 0;

  GraphPayloadEncoding encoding = GraphPayloadEncoding::kNone;
  std::vector<uint8_t> graph_payload;

  // The cursor this delta advances the projection to when it is applied.
  DeltaProjectionCursor NextCursor() const {
    return DeltaProjectionCursor{page_epoch, to_revision, event_sequence};
  }
};

// The single decision function for "may this delta be applied". Pure, total,
// and the only place the protocol section 10 matching rule is written.
//
// Order matters and is not arbitrary. Epoch is checked first because an epoch
// mismatch means the delta describes a different document, which no revision
// or sequence reasoning could rescue. Sequence is checked before revision
// because a gap is the stronger statement: it says changes were lost, so the
// revision the delta claims to start from is not evidence of anything.
constexpr DeltaRejectReason DeltaApplicability(
    const DeltaProjectionCursor& cursor,
    const PageEpoch& delta_epoch,
    GraphRevision delta_from_revision,
    GraphRevision delta_to_revision,
    EventSequence delta_event_sequence) {
  if (cursor.page_epoch != delta_epoch) {
    return DeltaRejectReason::kEpochMismatch;
  }
  if (delta_event_sequence <= cursor.event_sequence) {
    return delta_event_sequence == cursor.event_sequence
               ? DeltaRejectReason::kDuplicate
               : DeltaRejectReason::kOutOfOrder;
  }
  if (delta_event_sequence != cursor.event_sequence + 1) {
    return DeltaRejectReason::kSequenceGap;
  }
  if (delta_from_revision != cursor.revision) {
    return DeltaRejectReason::kRevisionMismatch;
  }
  // A delta that does not advance the revision cannot be applied on top of
  // itself later, so it is treated as a mismatch rather than as a no-op.
  if (delta_to_revision <= delta_from_revision) {
    return DeltaRejectReason::kRevisionMismatch;
  }
  return DeltaRejectReason::kNone;
}

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_DELTA_H_
