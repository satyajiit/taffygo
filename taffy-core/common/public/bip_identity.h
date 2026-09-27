// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_IDENTITY_H_
#define TAFFY_PUBLIC_BIP_IDENTITY_H_

#include <stddef.h>
#include <stdint.h>

#include <compare>
#include <string>
#include <string_view>

// The identity hierarchy of the Browser Intelligence Protocol, as owned value
// types (taffy-core/contracts/bip/schema/identity.schema.json, protocol section
// 5).
//
// Everything in //taffy/common/public is the trusted C++ API that the
// sandboxed core service and the Android facade consume. That imposes three
// rules on this directory, and they are why these headers include no Chromium
// headers at all:
//
//   1. Owned value types only. No Chromium raw pointer, no WebContents, no
//      RenderFrameHost, no GURL, no url::Origin, no base::Time, and no
//      borrowed reference crosses the boundary.
//   2. Mechanical mirrors. Every type here is an aggregate of fixed-width
//      integers, enums with an explicit underlying type, std::string,
//      std::vector and std::optional, so generated contract projections do
//      not require a second hand-written serializer.
//   3. Identifiers are opaque. A caller may compare them and hand them back.
//      It may not derive one, order one, or infer what Chromium object it
//      names.

namespace taffy {

// The schema bounds every opaque identifier. The bound is repeated here
// because C++ has to enforce it at the boundary; taffy-core/contracts/bip owns
// the value and this constant is checked against it by the contract conformance
// test.
inline constexpr size_t kMaxIdentifierChars = 128;

// Defines a strongly typed, opaque string identifier. Distinct tags do not
// convert to one another, so a FrameId can never be passed where a TabId is
// expected. An empty value means "unset" and is never issued.
//
// The defaulted three-way comparison exists so an identifier can key an
// ordered container. It carries no domain meaning: no code may read an
// ordering between two identifiers as "newer" or "inside".
#define TAFFY_DEFINE_ID_TYPE(Name)                                             \
  struct Name {                                                                \
    std::string value;                                                         \
    friend bool operator==(const Name &, const Name &) = default;              \
    friend auto operator<=>(const Name &, const Name &) = default;             \
    bool is_valid() const {                                                    \
      return !value.empty() && value.size() <= kMaxIdentifierChars;            \
    }                                                                          \
  }

TAFFY_DEFINE_ID_TYPE(ProfileId);
TAFFY_DEFINE_ID_TYPE(BrowserWindowId);

// Unique within a profile. A closed tab's identifier is never reassigned.
TAFFY_DEFINE_ID_TYPE(TabId);

// Unique within a tab. Survives a same-frame navigation, which is what makes
// "this frame navigated, the parent's assumptions did not" expressible.
TAFFY_DEFINE_ID_TYPE(FrameId);

// Changes whenever previously issued semantic handles must not be used for
// action (protocol section 5.2). Compare for equality only; ordering across
// frames or sessions carries no meaning. Whether a back/forward cache restore
// always allocates a new epoch is [Open (OD-029)].
TAFFY_DEFINE_ID_TYPE(PageEpoch);

// Unique and non-reused within one FrameId plus PageEpoch. Removing a node
// permanently retires its identifier for that epoch; a replacement element
// receives a new one even when its selector, text, role and bounds are
// unchanged (protocol section 5.4).
TAFFY_DEFINE_ID_TYPE(SemanticNodeId);

TAFFY_DEFINE_ID_TYPE(SnapshotId);
TAFFY_DEFINE_ID_TYPE(SubscriptionId);

// One in-flight call from the core service into this API. Exactly one terminal
// result is delivered per request identifier, ever.
TAFFY_DEFINE_ID_TYPE(RequestId);

// One authorized dispatch attempt, assigned by the browser process.
TAFFY_DEFINE_ID_TYPE(DispatchId);

TAFFY_DEFINE_ID_TYPE(CommandId);
TAFFY_DEFINE_ID_TYPE(TaskId);

// One explicit browser-originated user intent that is not a task. Direct page
// inspection uses this identity so the content and audit seams never have to
// invent an empty or namespaced TaskId. The prefix is part of the closed
// boundary: it lets conditional-presence validation reject a direct intent in
// a task slot and a task in a direct-intent slot.
inline constexpr std::string_view kDirectIntentIdPrefix = "direct-intent-";

inline bool IsDirectIntentIdentifier(std::string_view value) {
  return value.starts_with(kDirectIntentIdPrefix) &&
         value.size() <= kMaxIdentifierChars;
}

struct DirectIntentId {
  std::string value;
  friend bool operator==(const DirectIntentId &,
                         const DirectIntentId &) = default;
  friend auto operator<=>(const DirectIntentId &,
                          const DirectIntentId &) = default;
  bool is_valid() const { return IsDirectIntentIdentifier(value); }
};

TAFFY_DEFINE_ID_TYPE(ActionId);
TAFFY_DEFINE_ID_TYPE(SkillVersionId);
TAFFY_DEFINE_ID_TYPE(ActorLeaseId);

// A reference to a capability record held by the policy broker. It is a
// reference, not the unforgeable token: the token never leaves the process
// that minted it, and this reference is consumed in the browser process and
// never forwarded to a renderer (protocol section 11.3).
TAFFY_DEFINE_ID_TYPE(CapabilityReference);

TAFFY_DEFINE_ID_TYPE(ApprovalReceiptReference);
TAFFY_DEFINE_ID_TYPE(SensitivityPolicyId);

// Names a value the browser holds for one field of one task. It is a name and
// never any part of the value: not the bytes, not the length, not a mask, not
// a digest.
//
// A digest is worth spelling out, because it is the form that looks safe. A
// value that enters a form field is usually drawn from a tiny domain - a
// six-digit one-time code has a million members, a card security code ten
// thousand, a date of birth about forty thousand - and a digest over a domain
// that small is the value: anything holding the digest enumerates the domain
// and compares. So a reference carries no function of the value at all. It is
// unpredictable, minted from the browser's own randomness with nothing about
// the value as input, and it says the same thing about a one-character value
// as about a four-thousand-character one.
//
// Minted in the browser process, spent there, and never forwarded to a
// renderer: what a renderer receives is the resolved text on a one-use
// command, and a command carrying a reference is refused
// (//taffy/renderer/action_command_translator.h). The type that mints one is
// //taffy/components/security/browser/value_reference_vault.h, which is
// deliberately not reachable from this directory - nothing the sandboxed core
// service or the Android facade links against can mint, hold, or read one.
TAFFY_DEFINE_ID_TYPE(ValueReference);

#undef TAFFY_DEFINE_ID_TYPE

// Monotonically increasing within one active page epoch. Revisions from
// different epochs or frames are not comparable (protocol section 5.3).
using GraphRevision = uint64_t;

// Whether a graph revision floor is well formed for what it is pinning.
//
// A revision of zero means "this is pinned to no observed graph state". That is
// a legitimate thing to say, and it is what the very first request against a
// document has to say, because the renderer owns the revision (decision 0051)
// and has not reported one yet. It is admissible in exactly one situation:
// there is no node handle to pin. A request that names a node was built from a
// handle, a handle was issued at a revision, and a zero there is a handle with
// no provenance rather than a permissive request.
//
// Every refusal of a zero revision in this product goes through this predicate.
// That is the point of it existing rather than being written out at each site:
// the browser used to keep a counter that began at 1, so zero could not occur
// and `revision == 0` was a safe thing to write. Removing that counter turned
// four such lines into a deadlock in which a task could never take the
// observation that would give it a floor, and nothing failed loudly — the
// product simply stopped being able to read a page. A fifth site written the
// old way would do it again, so there is one predicate to reach for.
constexpr bool GraphRevisionFloorIsWellFormed(bool targets_node,
                                              uint64_t graph_revision) {
  return !targets_node || graph_revision != 0u;
}

// Strictly increasing per epoch, emitted by a frame endpoint. A detected gap
// invalidates delta state and forces a fresh snapshot.
using EventSequence = uint64_t;

// A reading of a monotonic clock in milliseconds. BIP carries no wall-clock
// timestamps, so a message cannot leak browsing time across a trust boundary.
using MonotonicMillis = uint64_t;

enum class OriginKind : uint8_t {
  kTuple = 0,
  kOpaque = 1,
};

// A normalized security origin recorded by the browser broker at observation
// time.
//
// An opaque origin stays opaque and is never broadened to a predecessor
// origin. Two opaque origins are equal only when their opaque_id values are
// equal, which is why comparing serializations would be wrong: every opaque
// origin serializes to the same thing, so string comparison would silently
// let an action authorized against one sandboxed document pass a check
// against a different one (protocol section 5.5).
struct Origin {
  OriginKind kind = OriginKind::kOpaque;
  // Scheme, host and port for a tuple origin. Empty for an opaque origin.
  std::string serialization;
  // Session-local identifier distinguishing one opaque origin from another.
  // Empty for a tuple origin.
  std::string opaque_id;

  friend bool operator==(const Origin &, const Origin &) = default;

  bool is_opaque() const { return kind == OriginKind::kOpaque; }
  bool is_valid() const {
    return is_opaque() ? !opaque_id.empty() : !serialization.empty();
  }
};

enum class DigestAlgorithm : uint8_t {
  kSha256 = 0,
};

// A digest over a canonical encoding of a message. Used to prove that the
// action authorized is exactly the action proposed.
struct ContentDigest {
  DigestAlgorithm algorithm = DigestAlgorithm::kSha256;
  // Lowercase hexadecimal.
  std::string value;

  friend bool operator==(const ContentDigest &,
                         const ContentDigest &) = default;

  bool is_valid() const { return !value.empty(); }
};

// The full handle required to act on a node (protocol section 5.4).
struct NodeHandle {
  TabId tab_id;
  FrameId frame_id;
  PageEpoch page_epoch;
  GraphRevision graph_revision = 0;
  SemanticNodeId node_id;
  Origin expected_origin;

  friend bool operator==(const NodeHandle &, const NodeHandle &) = default;

  // Well formed is not the same as live. Liveness is decided by the broker
  // against browser-owned state at dispatch time, never here.
  bool is_well_formed() const {
    return tab_id.is_valid() && frame_id.is_valid() && page_epoch.is_valid() &&
           node_id.is_valid() && expected_origin.is_valid();
  }
};

} // namespace taffy

#endif // TAFFY_PUBLIC_BIP_IDENTITY_H_
