// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_RENDERER_REPLY_PLAN_H_
#define TAFFY_TEST_SUPPORT_RENDERER_REPLY_PLAN_H_

#include <stdint.h>

#include <string>

// What a scripted renderer endpoint answers, including every way a compromised
// one could answer badly.
//
// The threat model's adversary A2 is a compromised renderer: it can send any
// message the interface allows, at any time, in any order. Every field below is
// one thing such a renderer would try, and each names the browser-side property
// it is attacking. A behaviour that is not on this list is not one the
// adversarial suite claims to cover.
//
// **Two attacks are deliberately absent, and their absence is the design.**
//
// An unknown enumeration member cannot be sent through the generated bindings
// at all: every BIP enumeration is closed — no extensible marker, no default
// member — so Mojo's own validator rejects the message before any Taffy code
// runs. That is the strictest possible form of failing closed, and it is why
// the closed-enumeration rule exists. The property is proved where it can be:
// the compatibility fixtures in taffy-core/contracts/bip/compat and the parser fuzzers in
// this directory's fuzz/ subdirectory, both of which construct the bytes
// directly rather than going through a well-behaved sender.
//
// A malformed message body is the same story for the same reason.
//
// Everything else on the list is reachable from a well-formed message, which is
// exactly why it has to be defended in Taffy's own code rather than by Mojo.

namespace taffy::test {

// How the endpoint answers GetProtocolInfo.
enum class ProtocolInfoBehaviour {
  // Reports the exact implementation set a production endpoint reports.
  kHonest,
  // Claims a major version this build does not speak. Negotiation must refuse
  // without inspecting anything else.
  kUnsupportedMajorVersion,
  // Claims limits far above the process ceiling. The broker clamps to its own
  // ceiling; an endpoint cannot raise it by asking.
  kOversizedLimits,
  // Never answers. Negotiation must still settle, once, with a bounded
  // failure.
  kNeverAnswers,
};

// How the endpoint answers GetSnapshot.
enum class SnapshotBehaviour {
  kHonest,
  // Echoes a page epoch that is not the one the broker bound. The reply belongs
  // to a different document and must be discarded, not adopted.
  kForeignPageEpochEcho,
  // Reports an origin other than the committed one. The browser overwrites both
  // the URL metadata and the origin metadata from its own navigation record, so
  // this must never reach a consumer.
  kForeignOriginEcho,
  // Returns more nodes than the clamped request allowed.
  kOverBudgetNodeCount,
  // Returns a message past the byte bound in force.
  kOversizedMessage,
  // Puts a seeded secret into a text run. Nothing downstream may carry it.
  kSecretInTextRun,
  // Describes a frame that is not in Chromium's frame tree. The broker owns the
  // topology and must not adopt a claimed one.
  kFabricatedFrame,
  // Answers twice for one request. The second must be dropped and counted.
  kRepliesTwice,
  // Never answers. The request must still settle exactly once, on its deadline.
  kNeverAnswers,
};

// How the endpoint answers ExecuteRendererAction.
enum class ActionBehaviour {
  // Reports that the operation was put on the input path, which is a statement
  // about the renderer and not about the page.
  kHonestDispatched,
  // Reports dispatched and advances nothing. The verifier must not reach
  // verified: renderer acknowledgement alone is never sufficient.
  kClaimsDispatchWithNoEffect,
  // Reports a revision far ahead of anything the browser issued, trying to
  // satisfy a revision-advance check by assertion.
  kInventsAdvancedRevision,
  // Reports a resolved node whose role and available actions differ from the
  // authorized ones.
  kSwapsResolvedNode,
  kRepliesTwice,
  // Answers neither of the two calls the dispatch sequence makes. Step 5
  // resolves the node before step 8 sends the command, so this is settled by
  // the resolve deadline and the command is never built: it is the wedge on
  // ResolveNode, not the wedge on ExecuteRendererAction.
  kNeverAnswers,
  // Resolves the node honestly and then never answers the command. Step 5
  // completes, so the wait the browser is left holding is the one on
  // ExecuteRendererAction. Two behaviours rather than one because the two
  // calls are bounded by two separate guards and each needs its own proof;
  // one knob could only ever exercise whichever call comes first.
  kResolvesThenNeverAnswers,
};

// How the endpoint answers Subscribe.
//
// Separate from DeltaBehaviour below, because establishing a stream and
// running one are different failures with different defences: this is about a
// subscription that does not exist yet, DeltaBehaviour is about one that does.
enum class SubscriptionBehaviour {
  // Accepts the subscription and reports the base revision a projection starts
  // from.
  kHonest,
  // Takes the subscription and never answers, while staying bound. Teardown
  // already settles a subscribe whose pipe went away; an endpoint that is
  // still there and simply silent is the case only the browser's own deadline
  // can end.
  kNeverAnswers,
};

// How the endpoint behaves on an open subscription.
enum class DeltaBehaviour {
  kNone,
  // Emits deltas in order with contiguous sequence numbers.
  kHonest,
  // Skips a sequence number. The projection is dead and only a fresh snapshot
  // is a legal way back.
  kSequenceGap,
  // Repeats a sequence number already delivered.
  kDuplicateSequence,
  // Emits a delta whose from-revision does not match the subscriber's cursor.
  kRevisionMismatch,
  // Emits a delta for a page epoch that is not the subscription's.
  kForeignEpoch,
  // Emits far more, far faster, than the budget allows, to drive the
  // backpressure ladder.
  kFlood,
  // Emits a delta carrying a seeded secret.
  kSecretInDelta,
};

// How the endpoint answers the browser-owned media identity revalidation.
// The default is a closed refusal: tests that need a live target must opt in
// to the exact well-formed geometry they want the endpoint to report.
enum class MediaTargetBehaviour {
  kNodeGone,
  kHonest,
  kNeverAnswers,
};

struct RendererReplyPlan {
  RendererReplyPlan();
  RendererReplyPlan(const RendererReplyPlan&);
  RendererReplyPlan& operator=(const RendererReplyPlan&);
  ~RendererReplyPlan();

  ProtocolInfoBehaviour protocol_info = ProtocolInfoBehaviour::kHonest;
  SnapshotBehaviour snapshot = SnapshotBehaviour::kHonest;
  ActionBehaviour action = ActionBehaviour::kHonestDispatched;
  SubscriptionBehaviour subscribe = SubscriptionBehaviour::kHonest;
  DeltaBehaviour delta = DeltaBehaviour::kNone;
  MediaTargetBehaviour media_target = MediaTargetBehaviour::kNodeGone;

  // How many nodes an honest snapshot reports. Kept small so the assertions are
  // about shape rather than volume.
  uint32_t honest_node_count = 4;

  // How many nodes an over-budget snapshot claims, and how many bytes an
  // oversized one carries. Both are set by the test from the limits the broker
  // reported, so neither restates a ceiling that lives elsewhere.
  uint32_t over_budget_node_count = 0;
  uint32_t oversized_message_bytes = 0;

  // The value a secret-carrying behaviour emits. Read from the corpus by the
  // test; never a literal in a test file.
  std::string secret_to_emit;

  // The identity a foreign-echo behaviour claims.
  std::string foreign_page_epoch;
  std::string foreign_origin_serialization;

  // How many deltas a flood emits before the test stops waiting.
  uint32_t flood_delta_count = 64;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_RENDERER_REPLY_PLAN_H_
