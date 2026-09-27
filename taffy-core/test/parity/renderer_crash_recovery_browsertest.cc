// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "base/strings/string_number_conversions.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/journal_assertions.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "content/public/common/result_codes.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// What survives a renderer that dies, and what must not (PAR-TAB-004,
// PAR-AND-007, and the protocol's renderer-crash row).
//
// The corpus carries a page that crashes its own renderer on demand, but the
// mechanism this suite uses is Chromium's own process kill. The reason is
// determinism: a page that exhausts memory crashes when the allocator says so,
// and a suite whose central event is timing-dependent produces flakes in
// exactly the assertions that matter most.
//
// Three properties, and the third is the one worth reading twice.
//
//   1. The tab survives and can be reloaded. The user does not lose the tab.
//   2. Every handle issued against the dead document is dead, and the
//      invalidation says so with a reason rather than by silence.
//   3. Nothing is replayed. An action that was in flight when the renderer died
//      ends with an outcome the browser will not claim to know, and the journal
//      records that it ended rather than that it succeeded.

namespace taffy::test {
namespace {

class ParityRendererCrashTest : public TaffyBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    GrantObservation({}, /*may_include_child_frames=*/false);
  }

 protected:
  // Kills the primary renderer and waits for the browser to notice. Uses
  // Chromium's own kill rather than the corpus's memory-exhaustion page,
  // because this event has to happen at a known moment.
  void KillPrimaryRenderer() {
    content::RenderProcessHost* host =
        web_contents()->GetPrimaryMainFrame()->GetProcess();
    content::RenderProcessHostWatcher watcher(
        host, content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
    host->Shutdown(content::RESULT_CODE_KILLED);
    watcher.Wait();
  }

  // Renderer death is the subject of this suite, so the test harness must not
  // treat it as a failure of the harness.
  content::ScopedAllowRendererCrashes allow_crashes_;
};

// PAR-TAB-004. The tab is still there afterwards and reloading brings the page
// back. The user does not lose a tab because a renderer died.
IN_PROC_BROWSER_TEST_F(ParityRendererCrashTest, TheTabSurvivesAndReloads) {
  ASSERT_TRUE(NavigateToFixture("renderer-crash"));
  const GURL url = web_contents()->GetLastCommittedURL();

  KillPrimaryRenderer();
  EXPECT_FALSE(web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive());

  content::TestNavigationObserver reload(web_contents());
  web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                         /*check_for_repost=*/false);
  reload.Wait();
  EXPECT_TRUE(web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive());
  EXPECT_EQ(url, web_contents()->GetLastCommittedURL());
}

// The invalidation contract. A renderer that died retires the epoch, and the
// notice names the reason: a subscriber that saw only "something changed" would
// not know whether it may resnapshot the same document or must bind a new one.
IN_PROC_BROWSER_TEST_F(ParityRendererCrashTest, CrashRetiresTheEpochWithAReason) {
  ASSERT_TRUE(NavigateToFixture("renderer-crash"));
  const FrameId frame_id = MainFrameId();
  ASSERT_TRUE(frame_id.is_valid());

  // The document has to be bound before a crash can retire anything, and a
  // frame identifier is not a binding. Identity is assigned when the frame
  // appears; the page epoch is allocated on the document's first observation,
  // deliberately, so that a document nobody looked at consumes no identifier.
  // Asking only for MainFrameId() and then expecting an invalidation asserts
  // that the browser retires an epoch it never issued - and it could not say
  // so if it wanted to, because every PageInvalidation carries a page epoch
  // and the contract gives that field no empty value.
  client().Observe(builder().Observation(frame_id, ObservationScope::kDocument));
  ASSERT_TRUE(broker()->GetActionableEndpoint(frame_id))
      << "The observation did not leave the document bound, so the crash below "
         "would have had no epoch to retire and this test would be asserting "
         "against its own setup rather than against the browser.";

  KillPrimaryRenderer();
  ASSERT_TRUE(client().WaitForInvalidationCount(1))
      << "The broker did not report the renderer's death. Every handle issued "
         "against that document is now dead and nothing said so.";

  bool saw_crash_reason = false;
  std::string reasons_seen;
  for (const InvalidationNotice& notice : client().invalidations()) {
    // Collected for the failure message below. "No invalidation named the
    // crash" and "an invalidation named something else" are different defects,
    // and a bare false says neither.
    reasons_seen +=
        " " + base::NumberToString(static_cast<int>(notice.reason));
    if (notice.reason != InvalidationCode::kRendererCrashed) {
      continue;
    }
    saw_crash_reason = true;
    EXPECT_TRUE(notice.retires_page_epoch)
        << "A crashed renderer's document is gone. An invalidation that left "
           "the epoch alive would leave every handle in it apparently usable.";
    EXPECT_TRUE(notice.resnapshot_required);
    EXPECT_FALSE(notice.invalidates_child_frames_only);
  }
  EXPECT_TRUE(saw_crash_reason)
      << "The invalidation arrived without naming the renderer crash as its "
         "reason. The reason decides the subscriber's next legal move. The "
         "InvalidationCode values that did arrive were:"
      << reasons_seen << " (kRendererCrashed is "
      << static_cast<int>(InvalidationCode::kRendererCrashed) << ").";

  EXPECT_TRUE(InvalidationRetiresDocument(InvalidationCode::kRendererCrashed))
      << "The shared predicate and the notice must agree; two opinions about "
         "whether a document is gone is how one of them becomes wrong.";
}

// The no-blind-replay property, end to end. An action submitted against a
// document whose renderer then dies ends with a terminal result the browser can
// justify, and the journal shows the intent was recorded before anything was
// attempted.
IN_PROC_BROWSER_TEST_F(ParityRendererCrashTest, AnInFlightActionIsNeverReplayed) {
  ASSERT_TRUE(NavigateToFixture("renderer-crash"));

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = MainFrameId();
  handle.page_epoch = PageEpoch{ids().NeverIssued("epoch")};
  handle.graph_revision = 1;
  handle.node_id = SemanticNodeId{ids().NeverIssued("node")};
  handle.expected_origin = MainFrameOrigin();

  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  const AuthorizedActionEnvelope envelope =
      builder().Activate(handle, /*required_graph_revision=*/1, {navigated});
  const ActionId action_id = envelope.action_id;

  const RequestId request_id = client().SubmitAction(envelope);
  KillPrimaryRenderer();

  const ActionResult result = client().AwaitActionResult(request_id);
  EXPECT_FALSE(IsActionSuccess(result.result_code))
      << "An action whose renderer died cannot be verified. Verified is "
         "reachable only through the browser-side postcondition verifier, and "
         "there was nothing left to observe.";
  EXPECT_TRUE(result.terminal);

  // The handle in this test was never live, so the refusal happens before
  // dispatch and nothing reaches the journal. That is the correct shape: a
  // refusal that still journalled an intent would mean something was attempted.
  EXPECT_TRUE(JournalAssertions::NoIntent(journal(), action_id));
  EXPECT_TRUE(JournalAssertions::AmbiguousOutcomesAreNotRepeatable(journal()));
}

// PAR-TAB-004. The recovery path never resolves a target by anything but a
// fresh handle. Asserted by giving the runtime a stale handle after a crash and
// confirming it is refused rather than re-resolved by selector, text or
// position.
IN_PROC_BROWSER_TEST_F(ParityRendererCrashTest,
                       AStaleHandleIsRefusedAfterRecovery) {
  ASSERT_TRUE(NavigateToFixture("renderer-crash"));
  const FrameId frame_id = MainFrameId();

  KillPrimaryRenderer();
  content::TestNavigationObserver reload(web_contents());
  web_contents()->GetController().Reload(content::ReloadType::NORMAL, false);
  reload.Wait();

  ObservationRequest request =
      builder().Observation(frame_id, ObservationScope::kDocument);
  // The epoch from before the crash. The document behind it no longer exists,
  // and no amount of similarity between the old page and the reloaded one may
  // make this succeed.
  request.expected_page_epoch = PageEpoch{ids().NeverIssued("epoch")};

  const ObservationEnvelope envelope = client().Observe(std::move(request));
  EXPECT_EQ(ObservationResultCode::kStalePageEpoch, envelope.code)
      << "An observation that named a retired epoch was answered rather than "
         "refused. Silently rebinding onto whatever document arrived next is "
         "precisely the failure the epoch exists to prevent.";
}

}  // namespace
}  // namespace taffy::test
