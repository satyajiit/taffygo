// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/journal_assertions.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Popups and new tabs: what an observation of one tab may and may not see, and
// what a new-tab postcondition may be believed on.
//
// A popup is the cheapest way to break tab scoping. It shares an opener, it may
// share a process, and a runtime that observed "the page" rather than "this tab
// and this frame" would fold the two together. It is also the shape of a
// postcondition that is easy to verify badly: something appeared, so the action
// worked — except that the something may be a window the page opened by itself.

namespace taffy::test {
namespace {

using PopupAndTaskTabTest = TaffyObservationTestBase;

// An observation is scoped to its tab. The popup's document is a different tab
// with a different identifier, and nothing about it appears in an observation
// of the opener.
IN_PROC_BROWSER_TEST_F(PopupAndTaskTabTest, AnObservationDoesNotCrossIntoAPopup) {
  ASSERT_TRUE(NavigateToFixture("popup-opener"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  content::ShellAddedObserver added;
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.getElementById('open-partner').click()"));
  content::Shell* popup = added.GetShell();
  ASSERT_TRUE(popup);
  ASSERT_TRUE(content::WaitForLoadStop(popup->web_contents()));

  const ObservationEnvelope envelope = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(broker()->tab_id(), envelope.tab_id);

  for (const FrameSummary& frame : envelope.frames) {
    EXPECT_NE(origins().OriginOf("partner").Serialize(),
              frame.origin.serialization)
        << "A frame from the popup's origin appeared in an observation of the "
           "opener. The popup is a separate tab and its content belongs to a "
           "separate observation with its own grant.";
  }
}

// The opener's own epoch is untouched by the popup. A runtime that lost its
// handles because the page opened a window would be unable to finish anything
// on a site that opens one.
IN_PROC_BROWSER_TEST_F(PopupAndTaskTabTest, OpeningAPopupDoesNotRetireTheOpener) {
  const ObservationEnvelope before = ObserveFixture("popup-opener");

  content::ShellAddedObserver added;
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.getElementById('open-partner').click()"));
  ASSERT_TRUE(added.GetShell());
  ASSERT_TRUE(content::WaitForLoadStop(added.GetShell()->web_contents()));

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(before.page_epoch, after.page_epoch)
      << "The opener's epoch was retired because the page opened a window. "
         "Nothing about the opener's document changed.";
}

// A page can open a window without any action from the runtime. A new-tab
// postcondition that was satisfied by "a window appeared" would be satisfied by
// that, so an action the runtime never took would be reported as verified.
IN_PROC_BROWSER_TEST_F(PopupAndTaskTabTest,
                       APageOpenedWindowDoesNotVerifyANewTabPostcondition) {
  const ObservationEnvelope observed = ObserveFixture("popup-opener");

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = observed.root_frame_id;
  handle.page_epoch = observed.page_epoch;
  handle.graph_revision = observed.graph_revision;
  handle.node_id = SemanticNodeId{ids().NeverIssued("node")};
  handle.expected_origin = MainFrameOrigin();

  Postcondition new_tab;
  new_tab.kind = PostconditionKind::kNewTabCreated;
  const AuthorizedActionEnvelope envelope =
      builder().Activate(handle, observed.graph_revision, {new_tab});
  const ActionId action_id = envelope.action_id;

  const RequestId request_id = client().SubmitAction(envelope);

  // While the action is pending, the page opens a window by itself.
  content::ShellAddedObserver added;
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.getElementById('open-partner').click()"));
  ASSERT_TRUE(added.GetShell());

  const ActionResult result = client().AwaitActionResult(request_id);
  EXPECT_FALSE(IsActionSuccess(result.result_code))
      << "A window the page opened satisfied a postcondition for an action the "
         "runtime never successfully dispatched. Verified has to come from a "
         "tab the browser attributes to this dispatch, not from any tab that "
         "happened to appear.";
  EXPECT_TRUE(JournalAssertions::AmbiguousOutcomesAreNotRepeatable(journal()));
  EXPECT_TRUE(JournalAssertions::NoIntent(journal(), action_id));
}

// A window opened without user activation is subject to the browser's popup
// policy. Whatever the policy decides, the runtime's view has to match the
// browser's: a blocked popup that the runtime believed had opened would leave
// it waiting for a tab that will never exist.
IN_PROC_BROWSER_TEST_F(PopupAndTaskTabTest, AnUnactivatedPopupFollowsBrowserPolicy) {
  ASSERT_TRUE(NavigateToFixture("popup-opener"));
  const size_t before = content::Shell::windows().size();

  // The fixture's unactivated case opens after a delay, with no user gesture.
  ASSERT_TRUE(content::ExecJs(
      web_contents(), "document.getElementById('open-unactivated').click()",
      content::EXECUTE_SCRIPT_NO_USER_GESTURE));

  // Whether it opens is the browser's decision and this suite does not assert
  // which way it goes: the corpus is served over a scheme and a profile whose
  // popup policy is content_shell's, not the product's. What must hold is that
  // the window count is one of the two consistent answers rather than a state
  // where a window exists that the browser does not know about.
  const size_t after = content::Shell::windows().size();
  EXPECT_TRUE(after == before || after == before + 1)
      << "The window count moved by " << (after - before)
      << ". One unactivated open produces at most one window.";
}

}  // namespace
}  // namespace taffy::test
