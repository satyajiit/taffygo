// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/navigation_lifecycle_tracker.h"

#include <string>
#include <vector>

#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "taffy/browser/browser_navigation_record.h"
#include "testing/gtest/include/gtest/gtest.h"

// Navigation and lifecycle coverage that only a real WebContents can give:
// PAR-NAV-001 (URL navigation and history entries), -002 (back, forward,
// reload, stop), -004 (redirect and origin visibility) and -006 (offline and
// DNS error pages).
//
// These cannot run on the planning workstation. They are written first anyway,
// because the property they hold down — that the browser's record of where the
// user is comes from the browser and not from a renderer — is invisible when
// it breaks. browser/PARITY.md lists the order the Linux track should bring
// them up in.

namespace taffy {
namespace {

class RecordingObserver : public NavigationLifecycleTracker::Observer {
 public:
  void OnNavigationCommitted(const BrowserNavigationRecord& record) override {
    committed_urls.push_back(record.committed_url());
    committed_origins.push_back(record.final_origin());
    crossed_origin.push_back(record.crossed_origin_during_redirect());
  }
  void OnNavigationControlStateChanged(
      const NavigationControlState& state) override {
    control_states.push_back(state);
  }

  std::vector<GURL> committed_urls;
  std::vector<Origin> committed_origins;
  std::vector<bool> crossed_origin;
  std::vector<NavigationControlState> control_states;
};

// The one host in this file that must not resolve. Named once so that the
// rule in SetUpOnMainThread and the test that depends on it cannot drift.
constexpr char kUnresolvableHost[] = "does-not-resolve.test";

class NavigationLifecycleTrackerBrowserTest
    : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();

    // Every host resolver rule this file needs is installed here, and the
    // simulated failure comes first because RuleBasedHostResolverProc matches
    // in insertion order and the wildcard below would otherwise swallow it.
    //
    // It cannot be installed in the test body instead:
    // BrowserTestBase::InitializeNetworkProcess calls
    // DisableModifications() before the body runs, and ClearRules() then
    // CHECKs. That is what DnsFailureCommitsAnErrorRecordWithNoContent used
    // to do, and it aborted the process rather than failing.
    host_resolver()->AddSimulatedFailure(kUnresolvableHost);
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  NavigationLifecycleTracker* GetTracker() {
    NavigationLifecycleTracker::CreateForWebContents(web_contents());
    return NavigationLifecycleTracker::FromWebContents(web_contents());
  }

  content::WebContents* web_contents() { return shell()->web_contents(); }

  GURL PageUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }
};

IN_PROC_BROWSER_TEST_F(NavigationLifecycleTrackerBrowserTest,
                       CommittedUrlAndOriginComeFromTheBrowser) {
  NavigationLifecycleTracker* tracker = GetTracker();
  RecordingObserver observer;
  tracker->AddObserver(&observer);

  const GURL url = PageUrl("primary.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(shell(), url));

  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  EXPECT_EQ(url, record->committed_url());
  EXPECT_EQ(url::Origin::Create(url).Serialize(),
            record->final_origin().serialization);
  EXPECT_TRUE(record->is_primary_main_frame());
  EXPECT_TRUE(record->CarriesRequestedContent());
  EXPECT_EQ(NavigationCommitKind::kNewDocument, record->commit_kind());

  ASSERT_EQ(1u, observer.committed_urls.size());
  EXPECT_EQ(url, observer.committed_urls.front());

  tracker->RemoveObserver(&observer);
}

IN_PROC_BROWSER_TEST_F(NavigationLifecycleTrackerBrowserTest,
                       RendererReportedUrlCannotReplaceTheRecord) {
  // The property the whole seam exists for. history.pushState changes what the
  // document says its URL is; the browser process is what decides whether that
  // is true, and the record follows the browser.
  NavigationLifecycleTracker* tracker = GetTracker();
  const GURL start = PageUrl("primary.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(shell(), start));

  ASSERT_TRUE(content::ExecJs(
      web_contents(), "history.pushState({}, '', '/pushed-by-the-page.html')"));

  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  // A same-document push is a real navigation the browser committed, so the
  // URL moves — but it moved because the browser committed it, and the origin
  // is unchanged. What must never happen is the URL moving to another origin.
  EXPECT_EQ(url::Origin::Create(start).Serialize(),
            record->final_origin().serialization);
  EXPECT_EQ(NavigationCommitKind::kSameDocument, record->commit_kind());
  EXPECT_TRUE(record->is_same_document());
}

IN_PROC_BROWSER_TEST_F(NavigationLifecycleTrackerBrowserTest,
                       CrossOriginRedirectExposesTheFinalOrigin) {
  // PAR-NAV-004.
  NavigationLifecycleTracker* tracker = GetTracker();
  RecordingObserver observer;
  tracker->AddObserver(&observer);

  const GURL destination = PageUrl("partner.test", "/title2.html");
  const GURL start =
      PageUrl("primary.test", "/server-redirect?" + destination.spec());

  ASSERT_TRUE(content::NavigateToURL(shell(), start, destination));

  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  EXPECT_EQ(destination, record->committed_url());
  EXPECT_EQ(url::Origin::Create(destination).Serialize(),
            record->final_origin().serialization);
  EXPECT_EQ(url::Origin::Create(start).Serialize(),
            record->initial_origin().serialization);
  EXPECT_TRUE(record->crossed_origin_during_redirect());
  EXPECT_GE(record->redirect_origins().size(), 2u);

  tracker->RemoveObserver(&observer);
}

IN_PROC_BROWSER_TEST_F(NavigationLifecycleTrackerBrowserTest,
                       BackForwardReloadAndStopFollowTheController) {
  // PAR-NAV-002. The control state is derived from the NavigationController
  // and from nothing else, so it is correct with the AI runtime absent.
  NavigationLifecycleTracker* tracker = GetTracker();

  ASSERT_TRUE(
      content::NavigateToURL(shell(), PageUrl("primary.test", "/title1.html")));
  EXPECT_FALSE(tracker->control_state().can_go_back);
  EXPECT_FALSE(tracker->control_state().can_go_forward);
  EXPECT_TRUE(tracker->control_state().can_reload);
  EXPECT_FALSE(tracker->control_state().can_stop);
  EXPECT_FALSE(tracker->ExecuteControl(BrowserCommandType::kStopLoading));
  ASSERT_TRUE(tracker->ExecuteControl(BrowserCommandType::kReload));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));
  ASSERT_TRUE(tracker->PrimaryMainFrameRecord());
  EXPECT_EQ(NavigationCommitKind::kReload,
            tracker->PrimaryMainFrameRecord()->commit_kind());

  ASSERT_TRUE(
      content::NavigateToURL(shell(), PageUrl("primary.test", "/title2.html")));
  EXPECT_TRUE(tracker->control_state().can_go_back);
  EXPECT_FALSE(tracker->control_state().can_go_forward);

  ASSERT_TRUE(tracker->ExecuteControl(BrowserCommandType::kGoBack));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));
  EXPECT_FALSE(tracker->control_state().can_go_back);
  EXPECT_TRUE(tracker->control_state().can_go_forward);

  ASSERT_TRUE(tracker->ExecuteControl(BrowserCommandType::kGoForward));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));
  EXPECT_TRUE(tracker->control_state().can_go_back);
  EXPECT_FALSE(tracker->control_state().can_go_forward);
}

IN_PROC_BROWSER_TEST_F(NavigationLifecycleTrackerBrowserTest,
                       DnsFailureCommitsAnErrorRecordWithNoContent) {
  // PAR-NAV-006. The record must say the page did not load, so that nothing
  // downstream can report having read it.
  NavigationLifecycleTracker* tracker = GetTracker();

  // The rule is installed in SetUpOnMainThread; see the comment there for why
  // it cannot be installed from here.
  const GURL url = PageUrl(kUnresolvableHost, "/title1.html");
  // The navigation commits an error page, so NavigateToURL reports failure
  // while a record still exists.
  EXPECT_FALSE(content::NavigateToURL(shell(), url));

  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  EXPECT_FALSE(record->CarriesRequestedContent());
  EXPECT_TRUE(record->error_verdict().content_is_absent);
  EXPECT_TRUE(NavigationErrorIsRetryable(record->error_verdict().error_class));
}

IN_PROC_BROWSER_TEST_F(NavigationLifecycleTrackerBrowserTest,
                       SubframeRecordsAreKeptSeparatelyAndDroppedOnRemoval) {
  NavigationLifecycleTracker* tracker = GetTracker();
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/page_with_iframe.html")));

  // The main frame plus its subframe both committed.
  EXPECT_GE(tracker->recorded_frame_count(), 2u);

  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.querySelector('iframe').remove()"));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));

  // The subframe's record is gone; the main frame's is not.
  EXPECT_EQ(1u, tracker->recorded_frame_count());
  EXPECT_TRUE(tracker->PrimaryMainFrameRecord());
}

}  // namespace
}  // namespace taffy
