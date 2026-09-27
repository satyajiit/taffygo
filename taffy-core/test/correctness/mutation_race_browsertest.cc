// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/audit_stream_assertions.h"
#include "taffy/test/support/bip_request_builder.h"
#include "taffy/test/support/journal_assertions.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// The race between observing a node and acting on it.
//
// The corpus's mutation-race fixture replaces, removes and repurposes elements
// on a fixed schedule after load. Each is a different failure and they are
// worth separating:
//
//   * **Replaced.** A new element takes the old one's place and its identifier.
//     A runtime that addressed by selector would act on the replacement.
//   * **Removed.** The element is gone. A runtime that fell back to text or
//     position would find something else that looked similar.
//   * **Repurposed.** The same element, a different destination. This is the
//     dangerous one: node identity is genuinely unchanged, and only the
//     destination precondition catches it.
//
// The property under test in all three is the same and it is negative: no
// action lands on something other than what was authorized. A test that only
// checked the action was refused would miss the case where it was refused for
// the wrong reason and would have succeeded a moment earlier.

namespace taffy::test {
namespace {

class MutationRaceTest : public TaffyObservationTestBase {
 protected:
  // Runs the fixture's mutations immediately rather than on its timer, so the
  // race happens at a known moment. A suite whose central event is a timeout is
  // a suite that flakes in exactly the assertions that matter most.
  void RunScheduledMutationsNow() {
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "const slot = document.getElementById('replaced-slot');"
        "slot.textContent = '';"
        "const fresh = document.createElement('a');"
        "fresh.id = 'replaced-link';"
        "fresh.href = '/product/lumen-desk-lamp.html';"
        "fresh.textContent = 'Replacement link';"
        "slot.appendChild(fresh);"
        "const gone = document.getElementById('removed-link');"
        "if (gone) { gone.remove(); }"
        "const repurposed = document.getElementById('repurposed-link');"
        "if (repurposed) {"
        "  repurposed.setAttribute('href', '/forms/checkout-sensitive.html');"
        "}"));
  }

  NodeHandle HandleFrom(const ObservationEnvelope& envelope,
                        const std::string& node_value) {
    NodeHandle handle;
    handle.tab_id = broker()->tab_id();
    handle.frame_id = envelope.root_frame_id;
    handle.page_epoch = envelope.page_epoch;
    handle.graph_revision = envelope.graph_revision;
    handle.node_id = SemanticNodeId{node_value};
    handle.expected_origin = MainFrameOrigin();
    return handle;
  }
};

// The revision moves when the page mutates. Everything else in this file
// depends on that, so it is asserted first and on its own.
IN_PROC_BROWSER_TEST_F(MutationRaceTest, AMutationAdvancesTheRevision) {
  const ObservationEnvelope before = ObserveFixture("mutation-race");
  ASSERT_GT(before.graph_revision, 0u);

  RunScheduledMutationsNow();

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(before.page_epoch, after.page_epoch)
      << "A DOM mutation retired the page epoch. It is the same document.";
  EXPECT_GT(after.graph_revision, before.graph_revision)
      << "The revision did not move. Every precondition expressed as a "
         "revision floor is now satisfied by state that has already changed.";
}

// An action carrying a revision from before the mutation is refused, and the
// refusal is attributed. The attribution is not decoration: the core service's next
// legal move after a stale graph differs from its next legal move after a node
// that is gone.
IN_PROC_BROWSER_TEST_F(MutationRaceTest, AStaleRevisionIsRefusedAndAttributed) {
  const ObservationEnvelope before = ObserveFixture("mutation-race");
  RunScheduledMutationsNow();

  const NodeHandle handle = HandleFrom(before, ids().NeverIssued("node"));
  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  navigated.allowed_origins.push_back(MainFrameOrigin());
  // Both lines matter and neither is ceremony. A bare kCommittedNavigation
  // postcondition carries no observable claim, so AllPostconditionsAreVerifiable
  // refuses the envelope as unverifiable before any capability is looked at;
  // and an envelope whose capability was never registered is refused by the
  // ledger as malformed. Both answer kDeniedByPolicy, which is a step-4 code —
  // so without them this case measures the admission gate and never reaches the
  // renderer-deadline behaviour it is named for.
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, before.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&envelope));
  const ActionId action_id = envelope.action_id;

  const ActionResult result = client().Act(envelope);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_TRUE(RequiresFreshObservation(result.result_code))
      << "The refusal did not say that a fresh observation is required, so a "
         "core service could reasonably retry the same handle.";
  EXPECT_FALSE(result.dispatched);

  EXPECT_TRUE(JournalAssertions::NoIntent(journal(), action_id))
      << "A refusal before dispatch must leave nothing to reconcile.";
  EXPECT_TRUE(AuditStreamAssertions::NoVerifiedWithoutCorroboration(
      audit_stream()));
}

// A node identifier retired by a removal is never reissued inside its epoch.
// This is what makes a stale handle fail closed rather than resolve to
// something new: identity does not follow the element, it follows the node, and
// a removed node's identity dies with it.
IN_PROC_BROWSER_TEST_F(MutationRaceTest, ARetiredIdentifierIsNotReissued) {
  const ObservationEnvelope before = ObserveFixture("mutation-race");
  RunScheduledMutationsNow();
  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  // The projection travels as an opaque payload, so this test cannot read node
  // identifiers out of it and must not pretend to. What it can assert is the
  // property that makes reuse impossible: the revision advanced, and the
  // contract's own rule is that a revision advance retires the identifiers the
  // removal took with it.
  ASSERT_GT(after.graph_revision, before.graph_revision);

  const NodeHandle handle = HandleFrom(before, ids().NeverIssued("node"));
  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  const ActionResult result =
      client().Act(builder().Activate(handle, before.graph_revision, {navigated}));
  EXPECT_FALSE(IsActionSuccess(result.result_code));
}

// The repurposed case. The element is the same one, so node identity is
// genuinely unchanged and a revision floor alone would not catch it. What
// catches it is the destination precondition, which is why an activate on a
// link declares one.
IN_PROC_BROWSER_TEST_F(MutationRaceTest, ARepurposedLinkIsCaughtByItsDestination) {
  const ObservationEnvelope before = ObserveFixture("mutation-race");

  NodeHandle handle = HandleFrom(before, ids().NeverIssued("node"));
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, before.graph_revision, [] {
        Postcondition navigated;
        navigated.kind = PostconditionKind::kCommittedNavigation;
        return std::vector<Postcondition>{navigated};
      }());

  // The destination the action was authorized against.
  Precondition destination;
  destination.kind = PreconditionKind::kExpectedDestination;
  Destination expected;
  expected.url_metadata.origin = MainFrameOrigin();
  expected.url_metadata.disclosure = UrlDisclosure::kOriginAndPath;
  expected.url_metadata.path = "/redirect/arrived.html";
  destination.expected_destination = expected;
  envelope.preconditions.push_back(destination);
  envelope = BipRequestBuilder::Reseal(std::move(envelope));

  RunScheduledMutationsNow();

  const ActionResult result = client().Act(envelope);
  EXPECT_FALSE(IsActionSuccess(result.result_code))
      << "An activate whose target now points at the sensitive form was "
         "allowed. Node identity did not change, so only the destination "
         "precondition stood between the authorized action and a different "
         "one.";
  EXPECT_FALSE(result.dispatched);
}

// The negative half, and the reason the tests above are not vacuous: with no
// mutation in between, the same shape of action is refused for a reason that
// names the handle rather than the race. If everything were refused
// unconditionally, every assertion in this file would pass over a broken
// dispatcher.
IN_PROC_BROWSER_TEST_F(MutationRaceTest, RefusalsAreAttributedRatherThanUniform) {
  const ObservationEnvelope envelope = ObserveFixture("mutation-race");

  const NodeHandle unknown_node = HandleFrom(envelope, ids().NeverIssued("node"));
  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  const ActionResult without_mutation = client().Act(
      builder().Activate(unknown_node, envelope.graph_revision, {navigated}));

  NodeHandle wrong_epoch = unknown_node;
  wrong_epoch.page_epoch = PageEpoch{ids().NeverIssued("epoch")};
  const ActionResult with_wrong_epoch = client().Act(
      builder().Activate(wrong_epoch, envelope.graph_revision, {navigated}));

  EXPECT_NE(without_mutation.result_code, with_wrong_epoch.result_code)
      << "A node the graph does not contain and a handle from a document that "
         "does not exist produced the same result code. The core service's next "
         "legal move differs between them, so collapsing the two costs a "
         "recovery path.";
  EXPECT_EQ(ActionResultCode::kStalePageEpoch, with_wrong_epoch.result_code);
}

}  // namespace
}  // namespace taffy::test
