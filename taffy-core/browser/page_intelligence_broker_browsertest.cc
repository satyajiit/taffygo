// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/page_intelligence_broker.h"

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "taffy/components/intelligence/content/event_sequence_tracker.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"

// Lifecycle coverage for the protocol section 13 table, one test per row.
//
// These cannot run on the planning workstation: they need a Chromium checkout
// and an Android or Linux build. They are written first anyway, because the
// rows they cover are the ones where a mistake is silent — a page epoch that
// survives a navigation it should not is not a crash, it is an action on the
// wrong document.
//
// browser/README.md lists the order the Linux track should bring these up in.

namespace taffy {
namespace {

class PageIntelligenceBrokerBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");

    // cross_site_iframe_factory.html builds its subframe tree out of
    // /cross-site/<host>/<path> URLs, and nothing serves those until this
    // handler is installed. Without it the factory page commits with no
    // subframe at all, so ChildFrameAt() returns null and a test about frame
    // epochs fails for a reason that has nothing to do with epochs. Upstream
    // installs it the same way (content/browser/renderer_host/
    // render_frame_host_impl_browsertest.cc).
    content::SetupCrossSiteRedirector(embedded_test_server());

    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  PageIntelligenceBroker* GetBroker() {
    PageIntelligenceBroker::CreateForWebContents(web_contents());
    return PageIntelligenceBroker::FromWebContents(web_contents());
  }

  content::WebContents* web_contents() { return shell()->web_contents(); }

  content::RenderFrameHost* main_frame() {
    return web_contents()->GetPrimaryMainFrame();
  }

  GURL PageUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }
};

// Records every invalidation so a test can assert on what the core service would
// have been told, rather than on internal state.
//
// It registers and unregisters itself, and that is not tidiness. Every test
// below has ASSERT_ macros between the AddObserver and the RemoveObserver that
// used to be written out by hand, so the first failing assertion returned from
// the test body with a destroyed observer still in the broker's list. The next
// notification then hit `Check failed: !weak_ptr_.WasInvalidated()` and the
// whole case was reported as a crash rather than as the assertion failure it
// was — the failure message survived, but the diagnosis had to be dug out of a
// stack trace. Owning the registration means a failing assertion fails.
class RecordingObserver : public PageIntelligenceBroker::Observer {
 public:
  explicit RecordingObserver(PageIntelligenceBroker* broker) : broker_(broker) {
    broker_->AddObserver(this);
  }
  RecordingObserver(const RecordingObserver&) = delete;
  RecordingObserver& operator=(const RecordingObserver&) = delete;
  ~RecordingObserver() override {
    if (broker_) {
      broker_->RemoveObserver(this);
    }
  }

  void OnPageInvalidated(const InvalidationNotice& notice) override {
    invalidations.push_back(notice);
  }
  void OnUserPreemption(TabId tab_id) override { preemptions.push_back(tab_id); }
  void OnBrokerDestroyed(TabId) override {
    broker_->RemoveObserver(this);
    broker_ = nullptr;
  }

  std::vector<InvalidationNotice> invalidations;
  std::vector<TabId> preemptions;

 private:
  raw_ptr<PageIntelligenceBroker> broker_;
};

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       CrossDocumentCommitRetiresTheEpoch) {
  PageIntelligenceBroker* broker = GetBroker();
  RecordingObserver observer(broker);

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* first = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(first);
  const PageEpoch first_epoch = first->page_epoch();
  const FrameId frame_id = first->frame_id();

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("b.test", "/title2.html")));
  FrameObservationEndpoint* second = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(second);

  // A new document is a new epoch, always. The frame identity survives,
  // because the frame did.
  EXPECT_NE(first_epoch, second->page_epoch());
  EXPECT_EQ(frame_id, second->frame_id());
  EXPECT_FALSE(observer.invalidations.empty());
  EXPECT_TRUE(observer.invalidations.back().retires_page_epoch);
  EXPECT_TRUE(observer.invalidations.back().resnapshot_required);
}

IN_PROC_BROWSER_TEST_F(
    PageIntelligenceBrokerBrowserTest,
    SameDocumentNavigationKeepsTheEpochAndRequiresAResnapshot) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* endpoint = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(endpoint);
  const PageEpoch epoch = endpoint->page_epoch();
  ASSERT_FALSE(endpoint->resnapshot_required());

  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("a.test", "/title1.html#fragment")));

  // The document survived, so the epoch survives. What changed is that
  // previously observed nodes have to be revalidated before use — and the
  // browser records that as the fact it is, rather than by advancing a
  // revision it has no standing to number. It has not seen the new graph.
  EXPECT_EQ(epoch, endpoint->page_epoch());
  EXPECT_TRUE(endpoint->resnapshot_required());

  // And the requirement is a refusal in its own right, independent of any
  // revision floor: a handle that is current by every number still describes a
  // document that moved.
  NodeHandle handle;
  handle.tab_id = broker->tab_id();
  handle.frame_id = endpoint->frame_id();
  handle.page_epoch = endpoint->page_epoch();
  handle.graph_revision = endpoint->last_reported_revision();
  handle.node_id = SemanticNodeId{"n_1"};
  handle.expected_origin = endpoint->origin();
  // kGraphMovedDuringPreflight, not kStaleGraph. These were one code until
  // protocol 0.7 split them, and the assertion kept the old one while the
  // paragraph above it describes the new one. The distinction is the point of
  // the case: kStaleGraph means the handle's revision is behind, and it cannot
  // be the answer here — the handle was built from last_reported_revision() and
  // asks for exactly that, so the ordering test in CheckHandleLiveness is false
  // by construction. What is true is that the document moved under a handle
  // that is current by every number, which is a refusal in its own right and
  // has its own code.
  EXPECT_EQ(ActionResultCode::kGraphMovedDuringPreflight,
            broker->CheckHandleLiveness(handle, handle.graph_revision));
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       ChildFrameNavigationLeavesTheParentEpochAlone) {
  PageIntelligenceBroker* broker = GetBroker();
  // The hostnames in the query string must be spelled in full. The factory
  // page appends ".com" to any label with no dot in it, then refuses to build
  // a single subframe when the result does not equal the page's own hostname
  // — "a" would become "a.com" against a host of a.test, and the page would
  // commit with an empty frame tree and a console error rather than fail. The
  // test then asserted on a child that was never created.
  ASSERT_TRUE(content::NavigateToURL(
      shell(),
      PageUrl("a.test", "/cross_site_iframe_factory.html?a.test(b.test)")));

  FrameObservationEndpoint* parent = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(parent);
  const PageEpoch parent_epoch = parent->page_epoch();

  content::RenderFrameHost* child = content::ChildFrameAt(main_frame(), 0);
  ASSERT_TRUE(child);
  FrameObservationEndpoint* child_endpoint = broker->GetOrCreateEndpoint(child);
  ASSERT_TRUE(child_endpoint);
  const PageEpoch child_epoch = child_endpoint->page_epoch();

  ASSERT_TRUE(content::NavigateToURLFromRenderer(
      child, PageUrl("c.test", "/title2.html")));

  // Only the child's handles die. The parent's assumptions did not change, so
  // its epoch does not (protocol section 13).
  EXPECT_EQ(parent_epoch, broker->GetOrCreateEndpoint(main_frame())->page_epoch());
  content::RenderFrameHost* new_child = content::ChildFrameAt(main_frame(), 0);
  ASSERT_TRUE(new_child);
  EXPECT_NE(child_epoch, broker->GetOrCreateEndpoint(new_child)->page_epoch());
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       HandleFromARetiredEpochIsRejected) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* endpoint = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(endpoint);

  NodeHandle handle;
  handle.tab_id = broker->tab_id();
  handle.frame_id = endpoint->frame_id();
  handle.page_epoch = endpoint->page_epoch();
  handle.graph_revision = endpoint->last_reported_revision();
  handle.node_id = SemanticNodeId{"n_1"};
  handle.expected_origin = endpoint->origin();

  ASSERT_FALSE(broker->CheckHandleLiveness(handle, handle.graph_revision)
                   .has_value());

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("b.test", "/title2.html")));

  // 100% rejection of handles from an invalid page epoch is a required safety
  // property (protocol section 17.4).
  const std::optional<ActionResultCode> code =
      broker->CheckHandleLiveness(handle, handle.graph_revision);
  ASSERT_TRUE(code.has_value());
  EXPECT_TRUE(*code == ActionResultCode::kStalePageEpoch ||
              *code == ActionResultCode::kOriginChanged);
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       RendererCrashInvalidatesEveryHandleInTheTab) {
  PageIntelligenceBroker* broker = GetBroker();
  RecordingObserver observer(broker);

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* endpoint = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(endpoint);

  // The crash is deliberate, so the harness has to be told: ContentBrowserTest
  // fails any test whose renderer dies unless a ScopedAllowRendererCrashes is
  // in scope for that process.
  content::ScopedAllowRendererCrashes allow_crashes(web_contents());
  content::RenderProcessHostWatcher watcher(
      web_contents(), content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  // The crash URL never commits, so the navigation reports failure. Upstream
  // asserts the same way (content/browser/renderer_host/
  // document_token_browsertest.cc); the constant moved to blink at the pinned
  // milestone (third_party/blink/public/common/chrome_debug_urls.h).
  EXPECT_FALSE(content::NavigateToURL(shell(), GURL(blink::kChromeUICrashURL)));
  watcher.Wait();

  EXPECT_FALSE(observer.invalidations.empty());
  EXPECT_FALSE(observer.preemptions.empty())
      << "a crash must revoke mutation authority in the tab";
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       BackForwardCacheEntryAndRestoreBothInvalidate) {
  // VERIFY AT SP-04: back/forward cache is not enabled in every test
  // configuration. This test needs the feature enabled and the eligibility
  // conditions met; the Linux track confirms the exact switches at the pinned
  // milestone before treating a skip here as a pass.
  PageIntelligenceBroker* broker = GetBroker();
  RecordingObserver observer(broker);

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* first = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(first);
  const PageEpoch first_epoch = first->page_epoch();

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("b.test", "/title2.html")));
  ASSERT_TRUE(content::HistoryGoBack(web_contents()));

  FrameObservationEndpoint* restored = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(restored);
  // The conservative reading of [Open (OD-029)]: a restore allocates a new
  // epoch, so no handle survives the round trip.
  EXPECT_NE(first_epoch, restored->page_epoch());
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       TabTeardownInvalidatesEverything) {
  PageIntelligenceBroker* broker = GetBroker();
  RecordingObserver observer(broker);

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  ASSERT_TRUE(broker->GetOrCreateEndpoint(main_frame()));

  broker->InvalidateAll(InvalidationCode::kTabClosed);

  EXPECT_FALSE(observer.invalidations.empty());
  EXPECT_FALSE(observer.preemptions.empty());
}


IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       ExplicitInvalidationRetiresAnEpochWithoutANavigation) {
  PageIntelligenceBroker* broker = GetBroker();
  RecordingObserver observer(broker);

  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* endpoint = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(endpoint);
  const FrameId frame_id = endpoint->frame_id();
  const PageEpoch epoch = endpoint->page_epoch();

  // The last entry in the protocol section 5.2 list. Not every reason to stop
  // trusting a handle is a Chromium lifecycle event: a withdrawn grant, an
  // ended task and an adapter restart are all reasons the broker knows and
  // Chromium does not.
  EXPECT_TRUE(broker->Invalidate(frame_id, InvalidationCode::kBrokerInvalidation));

  ASSERT_FALSE(observer.invalidations.empty());
  const InvalidationNotice& notice = observer.invalidations.back();
  EXPECT_EQ(notice.frame_id, frame_id);
  EXPECT_EQ(notice.page_epoch, epoch);
  EXPECT_EQ(notice.reason, InvalidationCode::kBrokerInvalidation);
  EXPECT_TRUE(notice.retires_page_epoch);
  EXPECT_TRUE(notice.resnapshot_required);

  // Retired means retired: the endpoint is no longer actionable, and a fresh
  // one is a new epoch rather than a revival of the old.
  EXPECT_FALSE(broker->GetActionableEndpoint(frame_id));
  EXPECT_FALSE(broker->Invalidate(frame_id, InvalidationCode::kBrokerInvalidation));
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       LateRepliesAreCountedRatherThanOnlyIgnored) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* endpoint = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(endpoint);
  ASSERT_EQ(endpoint->sequence_counters().late_after_terminal, 0u);

  // Protocol section 6.3 requires both halves: ignored, and counted. A drop
  // nobody counted is indistinguishable from a renderer that never answered.
  endpoint->RecordLateReply();
  EXPECT_EQ(endpoint->sequence_counters().late_after_terminal, 1u);
}

IN_PROC_BROWSER_TEST_F(PageIntelligenceBrokerBrowserTest,
                       OneOrderingCounterPerEpochAcrossEveryMessageKind) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), PageUrl("a.test", "/title1.html")));
  FrameObservationEndpoint* endpoint = broker->GetOrCreateEndpoint(main_frame());
  ASSERT_TRUE(endpoint);

  // Every message from one endpoint shares one counter, because there is one
  // ordering per epoch and two counters would disagree about where it is.
  EXPECT_EQ(endpoint->ClassifyEventSequence(1),
            EventSequenceTracker::Verdict::kAccepted);
  EXPECT_EQ(endpoint->ClassifyEventSequence(1),
            EventSequenceTracker::Verdict::kDuplicate);
  EXPECT_EQ(endpoint->ClassifyEventSequence(4),
            EventSequenceTracker::Verdict::kGap);
  EXPECT_EQ(endpoint->sequence_counters().gaps, 1u);
}

}  // namespace
}  // namespace taffy
