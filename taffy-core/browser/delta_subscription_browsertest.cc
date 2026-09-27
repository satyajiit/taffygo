// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/delta_subscription_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/browser/test/bip_fixture_manifest.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

// The delta path's lifetime rules, against a real WebContents
// (protocol sections 10 and 13).
//
// What is provable here and nowhere else: that a subscription is bound to a
// document rather than to a frame, that the epoch a Subscribe call names is
// checked against the live one, and that every refusal still settles exactly
// once. The rest of protocol section 10 — the matching rule, the ladder, the
// drop order — is proved in delta_subscription_unittest.cc and
// delta_backpressure_policy_unittest.cc, because none of it needs a page.
//
// What is deliberately NOT here: the full round trip, renderer to browser to
// core service. That lives in test/correctness/delta_stream_browsertest.cc, which
// drives a real renderer endpoint. The tests below are the manager's own
// lifetime rules: refuse when there is no observation endpoint, refuse a
// retired epoch, refuse a missing encoder, and stay a no-op when there is
// nothing to tear down. taffy_browsertests does install a renderer client, so
// a case that wants "no endpoint" has to leave the broker's endpoint uncreated
// rather than assume the renderer will stay silent.

namespace taffy {
namespace {

class RecordingDelegate : public DeltaSubscriptionManager::Delegate {
 public:
  void OnSubscriptionSettled(SubscriptionEnvelope envelope) override {
    settled.push_back(std::move(envelope));
  }
  void OnDeltaReady(DeltaEnvelope delta) override {
    deltas.push_back(std::move(delta));
  }
  void OnBackpressureNotice(BackpressureNotice notice) override {
    backpressure.push_back(std::move(notice));
  }
  void OnStreamInvalidated(InvalidationNotice notice) override {
    invalidations.push_back(std::move(notice));
  }

  std::vector<SubscriptionEnvelope> settled;
  std::vector<DeltaEnvelope> deltas;
  std::vector<BackpressureNotice> backpressure;
  std::vector<InvalidationNotice> invalidations;
};

class DiscardingObservabilitySink : public ObservabilitySink {
 public:
  void RecordObservation(const ObservationRecord& record) override {}
  void RecordAction(const ActionRecord& record) override {}
  void RecordSubscription(const SubscriptionRecord& record) override {
    ++subscription_records;
  }

  int subscription_records = 0;
};

class DeltaSubscriptionBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    manifest_ = std::make_unique<test::BipFixtureManifest>(
        test::BipFixtureManifest::Load());
    embedded_test_server()->ServeFilesFromDirectory(
        test::BipFixtureManifest::OriginRoot("primary"));
    ASSERT_TRUE(embedded_test_server()->Start());

    encoder_ = MakeMetadataOnlyGraphPayloadEncoder();
    observability_.SetSink(&sink_);
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  PageIntelligenceBroker* GetBroker() {
    PageIntelligenceBroker::CreateForWebContents(web_contents());
    return PageIntelligenceBroker::FromWebContents(web_contents());
  }

  std::unique_ptr<DeltaSubscriptionManager> MakeManager(
      PageIntelligenceBroker* broker) {
    auto manager = std::make_unique<DeltaSubscriptionManager>(
        broker->GetWeakPtr(), &delegate_, &observability_);
    manager->SetGraphPayloadEncoder(encoder_.get());
    return manager;
  }

  GURL FixtureUrl(const std::string& fixture_id) {
    const test::BipFixture& fixture = manifest_->ById(fixture_id);
    return embedded_test_server()->GetURL(
        manifest_->HostForOrigin(fixture.origin), fixture.url_path);
  }

  SubscriptionRequest RequestFor(PageIntelligenceBroker* broker,
                                 const PageEpoch& epoch) {
    SubscriptionRequest request;
    request.request_id = RequestId{"req_1"};
    request.task_id = TaskId{"task_1"};
    request.tab_id = broker->tab_id();
    request.frame_id =
        broker->GetOrAssignFrameId(web_contents()->GetPrimaryMainFrame());
    request.expected_page_epoch = epoch;
    request.scope = ObservationScope::kViewport;
    request.budget.max_queue_depth = 8;
    request.budget.max_queued_bytes = 4096;
    request.budget.max_delta_bytes = 1024;
    request.budget.coalescing_window_ms =
        GetProcessBudgetLimits().min_delta_interval_ms;
    return request;
  }

  std::unique_ptr<test::BipFixtureManifest> manifest_;
  RecordingDelegate delegate_;
  DiscardingObservabilitySink sink_;
  ObservabilityRecorder observability_;
  std::unique_ptr<GraphPayloadEncoder> encoder_;
};

IN_PROC_BROWSER_TEST_F(DeltaSubscriptionBrowserTest,
                       SubscribeWithNoEndpointSettlesExactlyOnceAndHonestly) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("spa-router")));

  std::unique_ptr<DeltaSubscriptionManager> manager = MakeManager(broker);
  // Frame identity is assigned; the observation endpoint is not.
  // DeltaSubscriptionManager::Subscribe looks up an existing actionable
  // endpoint and refuses when there is none — that is the no-endpoint path
  // this case exists to prove. Creating one first (GetOrCreateEndpoint) would
  // bind a remote, the renderer client this binary installs would answer, and
  // the case would be asserting the success path under a name that says the
  // opposite. The epoch is never consulted on this path: the endpoint check
  // is first.
  manager->Subscribe(RequestFor(broker, PageEpoch{"no-endpoint"}));

  ASSERT_EQ(delegate_.settled.size(), 1u);
  EXPECT_EQ(delegate_.settled.front().code,
            ObservationResultCode::kDocumentInactive);
  EXPECT_FALSE(delegate_.settled.front().subscription_id.has_value());
  EXPECT_EQ(manager->subscription_count(), 0u);
}

IN_PROC_BROWSER_TEST_F(DeltaSubscriptionBrowserTest,
                       SubscribeNamingARetiredEpochIsStale) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-article")));
  FrameObservationEndpoint* first =
      broker->GetOrCreateEndpoint(web_contents()->GetPrimaryMainFrame());
  ASSERT_TRUE(first);
  const PageEpoch retired = first->page_epoch();

  // The corpus declares this transition: cross-document, same origin, new
  // epoch. A subscription naming the old one must not silently rebind.
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-product")));
  FrameObservationEndpoint* second =
      broker->GetOrCreateEndpoint(web_contents()->GetPrimaryMainFrame());
  ASSERT_TRUE(second);
  ASSERT_NE(retired, second->page_epoch());

  std::unique_ptr<DeltaSubscriptionManager> manager = MakeManager(broker);
  manager->Subscribe(RequestFor(broker, retired));

  ASSERT_EQ(delegate_.settled.size(), 1u);
  EXPECT_EQ(delegate_.settled.front().code,
            ObservationResultCode::kStalePageEpoch);
  EXPECT_EQ(manager->subscription_count(), 0u);
}

IN_PROC_BROWSER_TEST_F(DeltaSubscriptionBrowserTest,
                       SubscribeWithNoEncoderIsUnsupportedRatherThanEmpty) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-article")));
  FrameObservationEndpoint* endpoint =
      broker->GetOrCreateEndpoint(web_contents()->GetPrimaryMainFrame());
  ASSERT_TRUE(endpoint);

  auto manager = std::make_unique<DeltaSubscriptionManager>(
      broker->GetWeakPtr(), &delegate_, &observability_);
  // No encoder installed: a delta could carry no graph, and opening a stream
  // that could only ever deliver empty payloads would be dishonest.
  manager->Subscribe(RequestFor(broker, endpoint->page_epoch()));

  ASSERT_EQ(delegate_.settled.size(), 1u);
  EXPECT_EQ(delegate_.settled.front().code,
            ObservationResultCode::kUnsupported);
}

IN_PROC_BROWSER_TEST_F(DeltaSubscriptionBrowserTest,
                       NavigationInvalidationReachesTheStreamManager) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("spa-router")));

  std::unique_ptr<DeltaSubscriptionManager> manager = MakeManager(broker);

  InvalidationNotice notice;
  notice.tab_id = broker->tab_id();
  notice.frame_id =
      broker->GetOrAssignFrameId(web_contents()->GetPrimaryMainFrame());
  notice.reason = InvalidationCode::kCrossDocumentCommit;
  notice.retires_page_epoch = true;

  // With no live stream there is nothing to tear down, and that has to be a
  // no-op rather than a crash: the broker notifies unconditionally, and the
  // manager is one of several observers that may have nothing to do.
  manager->OnPageInvalidated(notice);
  EXPECT_EQ(manager->subscription_count(), 0u);
  EXPECT_TRUE(delegate_.invalidations.empty());
}

IN_PROC_BROWSER_TEST_F(DeltaSubscriptionBrowserTest,
                       MemoryPressureStopsEveryStream) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("infinite-scroll")));

  std::unique_ptr<DeltaSubscriptionManager> manager = MakeManager(broker);
  // Deltas are the first thing memory pressure takes away
  // (protocol section 15). With none open the count is zero, and the call must
  // still be safe: the governor cannot know how many there were.
  EXPECT_EQ(manager->StopAllForMemoryPressure(), 0u);
  EXPECT_EQ(manager->subscription_count(), 0u);
}

IN_PROC_BROWSER_TEST_F(DeltaSubscriptionBrowserTest,
                       UnsubscribeIsIdempotent) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-article")));

  std::unique_ptr<DeltaSubscriptionManager> manager = MakeManager(broker);
  manager->Unsubscribe(SubscriptionId{"sub_never_existed"});
  manager->Unsubscribe(SubscriptionId{"sub_never_existed"});
  EXPECT_EQ(manager->subscription_count(), 0u);
  EXPECT_TRUE(delegate_.deltas.empty());
}

}  // namespace
}  // namespace taffy
