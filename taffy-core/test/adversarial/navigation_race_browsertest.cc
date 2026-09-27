// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/journal_assertions.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "url/gurl.h"

// Races between an action and everything that can invalidate it: a navigation
// the page starts, a navigation the user starts, direct user input, and a tab
// that closes underneath.
//
// **The property this file defends.** Navigation invalidation has priority over
// queued extraction and action work, and a stop or takeover prevents subsequent
// mutating actions including work already queued across process boundaries. The
// consequence a user cares about: nothing lands on the page they navigated to
// because it was authorized against the page they navigated from.
//
// **Why these are hard.** The window is small and the failure is silent. An
// action that lands on the wrong document does not crash; it clicks something.
// So every case here submits without waiting, causes the race deliberately, and
// then asserts on the single terminal result the contract still promises.

namespace taffy::test {
namespace {

class NavigationRaceTest : public TaffyBrowserTestBase {
 protected:
  NodeHandle HandleFor(const ObservationEnvelope& observed) {
    NodeHandle handle;
    handle.tab_id = broker()->tab_id();
    handle.frame_id = observed.root_frame_id.is_valid() ? observed.root_frame_id
                                                        : MainFrameId();
    handle.page_epoch = observed.page_epoch.is_valid()
                            ? observed.page_epoch
                            : PageEpoch{ids().NeverIssued("epoch")};
    handle.graph_revision = observed.graph_revision;
    handle.node_id = SemanticNodeId{ids().NeverIssued("node")};
    handle.expected_origin = MainFrameOrigin();
    return handle;
  }

  Postcondition Navigated() {
    Postcondition postcondition;
    postcondition.kind = PostconditionKind::kCommittedNavigation;
    return postcondition;
  }

  ObservationEnvelope Observe() {
    return client().Observe(
        builder().Observation(MainFrameId(), ObservationScope::kDocument));
  }
};

// **Property: an action submitted before a navigation does not land after it.**
// The document the action was authorized against is gone by the time the
// dispatcher looks, and the terminal result says so.
IN_PROC_BROWSER_TEST_F(NavigationRaceTest, ANavigationDuringDispatchRefusesTheAction) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  const ObservationEnvelope observed = Observe();

  const AuthorizedActionEnvelope envelope =
      builder().Activate(HandleFor(observed), observed.graph_revision,
                         {Navigated()});
  const ActionId action_id = envelope.action_id;
  const RequestId request_id = client().SubmitAction(envelope);

  ASSERT_TRUE(NavigateToFixture("static-product"));

  const ActionResult result = client().AwaitActionResult(request_id);
  EXPECT_FALSE(IsActionSuccess(result.result_code))
      << "An action authorized against one document was allowed after the "
         "browser committed another. Nothing about the target survived the "
         "commit.";
  EXPECT_TRUE(JournalAssertions::AmbiguousOutcomesAreNotRepeatable(journal()));
  EXPECT_TRUE(JournalAssertions::NoIntent(journal(), action_id));
}

// **Property: a navigation the page starts is treated exactly like one the user
// starts.** There is no privileged path by which a page can move the document
// without invalidating what was observed of it.
IN_PROC_BROWSER_TEST_F(NavigationRaceTest, APageStartedNavigationInvalidatesToo) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  const ObservationEnvelope observed = Observe();

  const RequestId request_id = client().SubmitAction(builder().Activate(
      HandleFor(observed), observed.graph_revision, {Navigated()}));

  content::TestNavigationObserver navigation(web_contents());
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "location.href = '/product/lumen-desk-lamp.html'"));
  navigation.Wait();

  const ActionResult result = client().AwaitActionResult(request_id);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
}

// **Property: direct user input preempts the assistant's mutation authority,
// synchronously.** Preemption that was a round trip would be worth nothing: the
// action it was meant to stop would already have been dispatched.
IN_PROC_BROWSER_TEST_F(NavigationRaceTest, UserInputPreemptsTheActorLease) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  const ObservationEnvelope observed = Observe();

  // Both envelopes are built and registered here, before the click, and that
  // ordering is the case rather than a convenience. This handle is live — same
  // document, same epoch, same revision — so unlike the navigation cases above
  // nothing refuses it in steps 1 to 3, and it reaches the step-4 admission
  // gate. An envelope with a bare committed-navigation postcondition is
  // unverifiable there, and one whose capability the ledger never recorded is
  // malformed; both answer kDeniedByPolicy, which satisfies "not a success"
  // just as well as the preemption this case exists to observe. Registering
  // before the click also makes the second half say what it means: authority
  // that was valid when the user took over is refused afterwards.
  Postcondition navigated = Navigated();
  navigated.allowed_origins.push_back(MainFrameOrigin());
  AuthorizedActionEnvelope first = builder().Activate(
      HandleFor(observed), observed.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&first));
  AuthorizedActionEnvelope second = builder().Activate(
      HandleFor(observed), observed.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&second));

  const RequestId request_id = client().SubmitAction(std::move(first));

  // A real click, delivered through Chromium's own input path, so the
  // preemption signal is the one the product would receive.
  content::SimulateMouseClick(web_contents(), 0,
                              blink::WebMouseEvent::Button::kLeft);

  const ActionResult result = client().AwaitActionResult(request_id);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_NE(ActionResultCode::kDeniedByPolicy, result.result_code)
      << "The admission gate answered, so this case measured the envelope's "
         "shape rather than the preemption it is named for.";

  // A later action under the same lease is refused too: the lease is revoked,
  // not merely the one action.
  const ActionResult after = client().Act(std::move(second));
  EXPECT_FALSE(IsActionSuccess(after.result_code))
      << "A second action succeeded after the user took over. Revoking one "
         "action rather than the authority is how the next one gets through.";
}

// **Property: a request outlives nothing.** When the tab goes away, every
// pending request settles rather than being abandoned, and the settlement is a
// terminal result the caller can act on.
IN_PROC_BROWSER_TEST_F(NavigationRaceTest, ClosingTheTabSettlesPendingRequests) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  const ObservationEnvelope observed = Observe();

  const RequestId request_id = client().SubmitAction(builder().Activate(
      HandleFor(observed), observed.graph_revision, {Navigated()}));

  broker()->InvalidateAll(InvalidationCode::kTabClosed);

  const ActionResult result = client().AwaitActionResult(request_id);
  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_TRUE(result.terminal);
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: invalidation reaches the caller before the refusal it explains.**
// A caller that learned its handles were dead only from the refusal would have
// no way to prepare a fresh observation while the action was still in flight.
IN_PROC_BROWSER_TEST_F(NavigationRaceTest, InvalidationArrivesWithItsReason) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  Observe();

  ASSERT_TRUE(NavigateToFixture("static-product"));
  ASSERT_TRUE(client().WaitForInvalidationCount(1));

  const InvalidationNotice& notice = client().invalidations().front();
  EXPECT_EQ(InvalidationCode::kCrossDocumentCommit, notice.reason);
  EXPECT_TRUE(notice.retires_page_epoch);
  EXPECT_TRUE(notice.new_page_epoch.has_value() ||
              notice.resnapshot_required)
      << "The invalidation neither named the replacement epoch nor asked for a "
         "fresh snapshot, so a subscriber is left with no legal next move.";
}

}  // namespace
}  // namespace taffy::test
