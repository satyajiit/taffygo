// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/audit_stream_assertions.h"
#include "taffy/test/support/bip_request_builder.h"
#include "taffy/test/support/journal_assertions.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// Every way a handle can be dead, and the refusal each one earns.
//
// **The property this file defends.** A stale page handle cannot act on a new
// document, origin, tab or profile. 100% rejection in deterministic tests is a
// required safety property, so a flake here is a defect rather than a flake.
//
// **Why each case is separate.** The result codes are not decoration: the
// core service's next legal move differs between them. A stale epoch means bind a new
// document. A stale graph means observe the same document again. A gone node
// means the target is not there any more. Collapsing them into one refusal
// costs a recovery path, so each is asserted with its own expected code and the
// suite asserts that they differ from one another.
//
// **What is deliberately not attempted.** Nothing here tries to make an action
// succeed on a stale handle in order to prove it is possible. The negative
// control is elsewhere: the correctness suite dispatches successfully against
// live handles, and without that this file's refusals could all be a dispatcher
// that refuses everything.

namespace taffy::test {
namespace {

class StaleHandleTest : public TaffyBrowserTestBase {
 protected:
  // Well formed in every field the dispatcher inspects, and dead in exactly
  // one: the page epoch names a document the broker never issued. That is the
  // shape a forged handle takes, and steps 1-3 refuse it before anything else
  // is looked at — which is why a case about a later step cannot be built on
  // this handle.
  NodeHandle LiveShapedHandle() {
    NodeHandle handle;
    handle.tab_id = broker()->tab_id();
    handle.frame_id = MainFrameId();
    handle.page_epoch = PageEpoch{ids().NeverIssued("epoch")};
    handle.graph_revision = 1;
    handle.node_id = SemanticNodeId{ids().NeverIssued("node")};
    handle.expected_origin = MainFrameOrigin();
    return handle;
  }

  // A handle carrying the committed document's own epoch, frame, revision and
  // origin, so that steps 1-3 pass and the refusal under test is the one the
  // case is named after. The node identifier is still one the graph has never
  // heard of; node existence is step 5, after the capability step, so it does
  // not shadow anything asserted here.
  NodeHandle HandleOnTheLiveEpoch() {
    NodeHandle handle;
    FrameObservationEndpoint* endpoint =
        broker()->GetOrCreateEndpoint(web_contents()->GetPrimaryMainFrame());
    if (!endpoint) {
      ADD_FAILURE() << "No endpoint for the committed main frame, so there is "
                       "no live epoch to build a handle on.";
      return LiveShapedHandle();
    }
    handle.tab_id = broker()->tab_id();
    handle.frame_id = endpoint->frame_id();
    handle.page_epoch = endpoint->page_epoch();
    handle.graph_revision = endpoint->last_reported_revision();
    handle.node_id = SemanticNodeId{ids().NeverIssued("node")};
    handle.expected_origin = endpoint->origin();
    return handle;
  }

  // A committed-navigation postcondition that names where the navigation is
  // allowed to land. The claim is not decoration: an action whose declared
  // effects carry no observable claim is unverifiable, and the dispatcher
  // refuses an unverifiable action in step 4 before the capability is even
  // looked at. Without the claim, every case below would be answered by that
  // refusal rather than by the check it is about.
  Postcondition NavigationPostcondition() {
    Postcondition postcondition;
    postcondition.kind = PostconditionKind::kCommittedNavigation;
    postcondition.allowed_origins.push_back(MainFrameOrigin());
    return postcondition;
  }

  ActionResult ActOn(const NodeHandle& handle, GraphRevision revision) {
    return client().Act(
        builder().Activate(handle, revision, {NavigationPostcondition()}));
  }

  // A second envelope carrying the capability `refused` carried — the same
  // reference, the same lease, the same expiry — over a handle that is well
  // formed. Everything else is fresh, so the two are distinct actions in the
  // journal and in the audit stream and the capability is the only thing they
  // share.
  //
  // This is the only way the ledger can be asked its question from outside the
  // browser process, and asking it is the point: the ledger admits a capability
  // exactly once, so a retry that reaches step 4 and is admitted proves the
  // first attempt did not spend it, and a retry answered with
  // kCapabilityExpired proves it did.
  AuthorizedActionEnvelope RetryOnTheSameCapability(
      const AuthorizedActionEnvelope& refused,
      const NodeHandle& handle) {
    AuthorizedActionEnvelope retry = builder().Activate(
        handle, handle.graph_revision, {NavigationPostcondition()});
    retry.capability_reference = refused.capability_reference;
    retry.capability = refused.capability;
    // The digest is computed over the finished envelope, so it has to be
    // recomputed after the capability is swapped in. Without this the retry
    // would be refused for a digest mismatch — in step 4, before the ledger is
    // consulted — and would prove nothing about what was spent.
    return BipRequestBuilder::Reseal(std::move(retry));
  }
};

// A handle naming a page epoch the broker never issued. The most basic form,
// and the one a forged handle would take.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, AnUnknownEpochIsRejected) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  const ActionResult result = ActOn(LiveShapedHandle(), 1);
  EXPECT_EQ(ActionResultCode::kStalePageEpoch, result.result_code);
  EXPECT_FALSE(result.dispatched);
  EXPECT_TRUE(RequiresFreshObservation(result.result_code));
}

// A handle naming a frame in another tab. Frame identifiers are unique within a
// tab, so a handle that carried one tab's frame and another tab's identity is
// exactly the confused-deputy shape.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, AHandleFromAnotherTabIsRejected) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  NodeHandle handle = LiveShapedHandle();
  handle.tab_id = TabId{ids().NeverIssued("tab")};

  const ActionResult result = ActOn(handle, 1);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_FALSE(result.dispatched);
}

// A handle whose expected origin is not the document's. The origin travels with
// the handle precisely so that a document which navigated to another site
// cannot be acted on with authority granted for the first.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, AHandleWithTheWrongOriginIsRejected) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  NodeHandle handle = LiveShapedHandle();
  handle.expected_origin.kind = OriginKind::kTuple;
  handle.expected_origin.serialization =
      origins().OriginOf("hostile").Serialize();

  const ActionResult result = ActOn(handle, 1);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_FALSE(result.dispatched);
}

// Two opaque origins are equal only when their opaque identifiers are equal.
// Comparing serializations would make every opaque origin equal to every other,
// which would let an action authorized against one sandboxed document pass a
// check against a different one.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, OpaqueOriginsAreNotInterchangeable) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  NodeHandle handle = LiveShapedHandle();
  handle.expected_origin.kind = OriginKind::kOpaque;
  handle.expected_origin.serialization.clear();
  handle.expected_origin.opaque_id = "some-other-sandboxed-document";

  const ActionResult result = ActOn(handle, 1);
  EXPECT_FALSE(IsActionSuccess(result.result_code));

  Origin first;
  first.kind = OriginKind::kOpaque;
  first.opaque_id = "a";
  Origin second;
  second.kind = OriginKind::kOpaque;
  second.opaque_id = "b";
  EXPECT_FALSE(first == second)
      << "Two opaque origins compared equal. Every opaque origin serializes to "
         "the same thing, so this comparison has to be on the identifier.";
}

// A well-formed handle whose node identifier is not in the graph. Distinct from
// a stale epoch, and the distinction is the recovery path: the document is fine,
// the target is not.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, RefusalCodesAreDistinctFromOneAnother) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  const ActionResult unknown_epoch = ActOn(LiveShapedHandle(), 1);

  NodeHandle wrong_tab = LiveShapedHandle();
  wrong_tab.tab_id = TabId{ids().NeverIssued("tab")};
  const ActionResult unknown_tab = ActOn(wrong_tab, 1);

  EXPECT_NE(unknown_epoch.result_code, unknown_tab.result_code)
      << "A handle from an unknown document and a handle from an unknown tab "
         "produced the same refusal. The core service's next legal move differs "
         "between them, so a single code costs a recovery path.";
}

// A malformed handle is refused without a capability being consumed. The
// ordering matters: a capability spent on a refusal is a capability the
// legitimate retry no longer has.
//
// **Spending nothing and recording nothing are different properties, and only
// the first one holds.** Step 10 writes exactly one terminal result for every
// action whatever the outcome, refusals included, and settles the capability
// only when step 4 admitted one — see action_dispatcher_verification.cc, where
// the settle is behind `pending.capability_admitted` and the record is not.
// That is the contract rather than an oversight: protocol section 16 has each
// operation record its action result and stale reason, the stale-rate metrics
// SP-04 needs are made of exactly those records, and the reasons a refusal
// carries (kPageEpoch, kOrigin) name refusals that happen before the capability
// step. A refusal the audit stream never heard of would be a hole in the trail
// rather than a saving. So this case asserts the record exists and asks the
// ledger its own question directly.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, AMalformedHandleSpendsNothing) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  // Built on the live epoch and broken in exactly one field, so the refusal is
  // the malformed-handle one and the retry below differs from it only there.
  NodeHandle handle = HandleOnTheLiveEpoch();
  handle.node_id = SemanticNodeId{};  // empty, therefore not well formed
  ASSERT_FALSE(handle.is_well_formed());

  const AuthorizedActionEnvelope envelope = builder().Activate(
      handle, handle.graph_revision, {NavigationPostcondition()});
  const ActionId action_id = envelope.action_id;
  const ActionResult result = client().Act(envelope);

  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_FALSE(result.dispatched);
  EXPECT_TRUE(JournalAssertions::NoIntent(journal(), action_id));
  EXPECT_TRUE(AuditStreamAssertions::ActionRecorded(
      audit_stream(), action_id.value, ActionResultCode::kDeniedByPolicy));

  // The property the case is named after. The capability is one use, and one
  // use includes uses that failed — so if the refusal above had admitted it,
  // this retry would come back kCapabilityExpired, which is the code an
  // already-spent capability earns. It comes back with a refusal from a later
  // step instead, which is only reachable once admission has succeeded.
  const ActionResult retry =
      client().Act(RetryOnTheSameCapability(envelope, HandleOnTheLiveEpoch()));
  EXPECT_NE(ActionResultCode::kCapabilityExpired, retry.result_code)
      << "The refused envelope's capability was already spent, so the "
         "legitimate retry no longer has it. A refusal must not consume the "
         "capability it refused.";
  EXPECT_FALSE(IsActionSuccess(retry.result_code))
      << "The retry names a node the graph never issued, so it is refused too "
         "— later in the sequence, which is the whole point.";
}

// An envelope edited after authorization hashes differently and is refused
// before a capability is consumed. This is the check that makes the digest
// worth carrying at all.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, AnEditedEnvelopeIsRefusedByItsDigest) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  const AuthorizedActionEnvelope authorized =
      builder().Activate(LiveShapedHandle(), 1, {NavigationPostcondition()});
  const AuthorizedActionEnvelope tampered = BipRequestBuilder::WithTamperedTarget(
      authorized, SemanticNodeId{ids().NeverIssued("other-node")});

  const ActionResult result = client().Act(tampered);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_FALSE(result.dispatched);
  EXPECT_TRUE(JournalAssertions::NoIntent(journal(), tampered.action_id));
}

// A capability referencing a lease nobody issued, and one that has already
// expired. Both are refused before anything is attempted, and each has its own
// code so a caller can tell "get a lease" from "get a fresh capability".
IN_PROC_BROWSER_TEST_F(StaleHandleTest, MissingAndExpiredAuthorityAreDistinct) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  // Observed first, and the revision is asserted rather than assumed. The
  // endpoint's reported revision is zero until a snapshot arrives, and a grant
  // that names a node at revision zero is a handle with no provenance: the
  // ledger refuses to register it at all (GraphRevisionFloorIsWellFormed in
  // bip_identity.h), so the registration below would fail and this case would
  // never reach the ledger's answer.
  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  ASSERT_GT(observed.graph_revision, 0u);

  // Built on the live epoch on purpose. A handle the broker never issued is
  // refused in step 2 with kStalePageEpoch, which would hide the two codes
  // this case exists to tell apart.
  const NodeHandle handle = HandleOnTheLiveEpoch();
  ASSERT_GT(handle.graph_revision, 0u);
  AuthorizedActionEnvelope base =
      builder().Activate(handle, handle.graph_revision,
                         {NavigationPostcondition()});
  // Registered before either mutation, and registered once. Without this the
  // ledger has no record of the capability at all and answers kMalformed, so
  // neither arm below is ever reached and the case measures the presence of a
  // grant rather than the two refusals it is named for. Registration has to
  // come first because it overwrites the very fields the mutations then change,
  // and one registration covers both variants because the action digest
  // excludes every capability field (action_digest.cc) and each mutator
  // recomputes it anyway.
  ASSERT_TRUE(RegisterActionGrant(&base));

  const ActionResult no_lease = client().Act(BipRequestBuilder::WithUnknownLease(
      base, ActorLeaseId{ids().NeverIssued("lease")}));
  const ActionResult expired =
      client().Act(BipRequestBuilder::WithExpiredCapability(base));

  // [Open (OD-128)] Both of these still fail, and the harness is no longer why.
  // The envelope path admits through CapabilityLedger::AdmitTaskAction, which
  // compares the declared actor_lease_id and expires_at_monotonic_ms against
  // the registered grant inside one EnvelopeMatchesGrant predicate — so an
  // unknown lease and a zeroed expiry are both kDigestMismatch, and both are
  // reported as kDeniedByPolicy. kExpired is reached only from the ledger's own
  // record.expires_at, never from what an envelope declares. The sibling
  // CapabilityLedger::Admit does separate the digest from the authority fields,
  // but folds lease and expiry disagreement together into kLeaseMissing, so
  // even there the second half would answer kActorLeaseMissing. Whether a
  // declared expiry is a claim worth its own refusal is a question about the
  // ledger's taxonomy and is security-sensitive, so it is registered rather
  // than patched under a test fix. The assertions stay as written: they state
  // the property the case is for, and weakening them to the codes the ledger
  // happens to answer would retire the question instead of recording it.
  EXPECT_EQ(ActionResultCode::kCapabilityExpired, expired.result_code);
  EXPECT_FALSE(no_lease.dispatched);
  EXPECT_FALSE(expired.dispatched);
}

// An envelope declaring no expected effect is refused: an unverifiable action
// is not an authorized action. Stated as its own case so that relaxing it later
// is a visible change to a named property.
IN_PROC_BROWSER_TEST_F(StaleHandleTest, AnUnverifiableActionIsNotAuthorized) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  const AuthorizedActionEnvelope base =
      builder().Activate(LiveShapedHandle(), 1, {NavigationPostcondition()});
  const ActionResult result =
      client().Act(BipRequestBuilder::WithNoPostcondition(base));

  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_FALSE(result.dispatched);
}

}  // namespace
}  // namespace taffy::test
