// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/lifecycle_continuity_ledger.h"

#include <string>

#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-AND-005 and PAR-AND-007 against a real WebContents.
//
// content_shell has no Activity, so the lifecycle phases are driven directly.
// That is the honest shape of the test at this layer: what a browser test can
// prove here is that a real WebContents keeps its document and its history
// across the sequence, and that the ledger refuses the continuation that was
// queued before it. The Activity callbacks themselves are exercised by the
// instrumentation suite in //taffy/app/android (WP-M1-03), which has a
// real Activity to rotate.

namespace taffy {
namespace {

class LifecycleContinuityBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  GURL PageUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  NavigationLifecycleTracker* GetTracker() {
    NavigationLifecycleTracker::CreateForWebContents(web_contents());
    return NavigationLifecycleTracker::FromWebContents(web_contents());
  }

  void DriveToResumed() {
    ledger_.NotePhase(window_, ActivityLifecyclePhase::kCreated);
    ledger_.NotePhase(window_, ActivityLifecyclePhase::kStarted);
    ledger_.NotePhase(window_, ActivityLifecyclePhase::kResumed);
  }

  ContinuationKey Key(const char* operation, uint64_t generation) {
    ContinuationKey key;
    key.tab_id = ToRecordIdentifier("tab_1");
    key.operation_id = ToRecordIdentifier(operation);
    key.generation = generation;
    return key;
  }

  const BrowserWindowId window_{"window_1"};
  LifecycleContinuityLedger ledger_;
};

IN_PROC_BROWSER_TEST_F(LifecycleContinuityBrowserTest,
                       ARecreationLosesNoDocumentAndNoHistory) {
  NavigationLifecycleTracker* tracker = GetTracker();
  DriveToResumed();

  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title2.html")));
  ASSERT_TRUE(tracker->control_state().can_go_back);

  // The recreation sequence.
  ledger_.NotePhase(window_, ActivityLifecyclePhase::kPaused);
  ledger_.NotePhase(window_, ActivityLifecyclePhase::kStopped);
  ledger_.NotePhase(window_, ActivityLifecyclePhase::kDestroyedForRecreation);
  DriveToResumed();

  // The document and the history are Chromium's and survive by construction;
  // the assertion is that nothing in this seam disturbed them.
  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  EXPECT_EQ(PageUrl("primary.test", "/title2.html"), record->committed_url());
  EXPECT_TRUE(tracker->control_state().can_go_back);
  ASSERT_TRUE(content::HistoryGoBack(web_contents()));
  EXPECT_EQ(PageUrl("primary.test", "/title1.html"),
            web_contents()->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(LifecycleContinuityBrowserTest,
                       AContinuationQueuedBeforeARecreationDoesNotRun) {
  DriveToResumed();
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));

  const uint64_t queued_generation = ledger_.generation(window_);
  ASSERT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger_.Admit(window_, Key("resume_extraction", queued_generation)));

  ledger_.NotePhase(window_, ActivityLifecyclePhase::kDestroyedForRecreation);
  DriveToResumed();

  // The re-delivered continuation is refused, so the step it names runs once
  // in total rather than once per rotation.
  EXPECT_EQ(ContinuationAdmission::kRefusedStaleGeneration,
            ledger_.Admit(window_, Key("resume_extraction", queued_generation)));
}

IN_PROC_BROWSER_TEST_F(LifecycleContinuityBrowserTest,
                       BackgroundingPausesPageControlAndForegroundResumesIt) {
  DriveToResumed();
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));
  EXPECT_TRUE(ledger_.CurrentVerdict(window_).page_control_permitted);

  web_contents()->WasHidden();
  ledger_.NotePhase(window_, ActivityLifecyclePhase::kStopped);
  EXPECT_FALSE(ledger_.CurrentVerdict(window_).page_control_permitted);

  // Manual browsing is unaffected: the page is still there and still
  // navigable, which is the half of PAR-AND-005 a person notices.
  web_contents()->WasShown();
  DriveToResumed();
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title2.html")));
  EXPECT_TRUE(ledger_.CurrentVerdict(window_).page_control_permitted);
}

IN_PROC_BROWSER_TEST_F(LifecycleContinuityBrowserTest,
                       ARendererCrashDoesNotAdvanceTheLifecycleGeneration) {
  // A renderer crash is not a recreation: the Activity did not go away and
  // nothing re-delivers its state. Advancing the generation here would refuse
  // continuations that are perfectly live.
  DriveToResumed();
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));
  const uint64_t before = ledger_.generation(window_);

  content::RenderProcessHost* process =
      web_contents()->GetPrimaryMainFrame()->GetProcess();
  content::RenderProcessHostWatcher watcher(
      process, content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  process->Shutdown(content::RESULT_CODE_KILLED);
  watcher.Wait();

  EXPECT_EQ(before, ledger_.generation(window_));
  EXPECT_TRUE(ledger_.CurrentVerdict(window_).page_control_permitted);
}

}  // namespace
}  // namespace taffy
