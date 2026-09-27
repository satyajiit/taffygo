// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/postcondition_verifier.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
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
#include "taffy/browser/test/bip_fixture_manifest.h"
#include "taffy/components/intelligence/content/browser_effect_source.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

// The DISPATCHED-versus-VERIFIED boundary, proved against a real WebContents
// (protocol section 11.6, CAP-PI-009).
//
// These need a browser because the evidence they are about is a browser fact:
// Chromium's own committed navigation record, its own tab-creation event, and
// its own renderer-process-gone signal. A unit test can prove the checks; only
// a browser can prove that the checks are fed from the right place.
//
// Every URL and every declared transition below comes from
// test-fixtures/web/manifest.json. Nothing is retyped: the corpus is versioned
// and immutable, and a test with a path typed into it would keep passing after
// the corpus moved the fixture it was about.
//
// browser/README.md lists the order the Chromium track should bring these up
// in.

namespace taffy {
namespace {

// A resolve callback the test drives. Standing in for the renderer is
// deliberate and is not the thing under test: whatever this reports, it can
// never produce kVerified on its own, because the verifier weighs it only as an
// observation taken after dispatch and only alongside the browser's own record.
class ScriptedNodeResolver {
 public:
  PostconditionVerifier::ResolveNodeCallback Callback() {
    return base::BindLambdaForTesting(
        [this](
            const NodeHandle& handle,
            base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> done) {
          ++calls;
          std::move(done).Run(facts);
        });
  }

  std::optional<ResolvedNodeFacts> facts;
  int calls = 0;
};

class PostconditionVerifierBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    manifest_ = std::make_unique<test::BipFixtureManifest>(
        test::BipFixtureManifest::Load());
    embedded_test_server()->ServeFilesFromDirectory(
        test::BipFixtureManifest::OriginRoot("primary"));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  PageIntelligenceBroker* GetBroker() {
    PageIntelligenceBroker::CreateForWebContents(web_contents());
    return PageIntelligenceBroker::FromWebContents(web_contents());
  }

  // The corpus decides the path; the test server decides the host and port.
  GURL FixtureUrl(const std::string& fixture_id) {
    const test::BipFixture& fixture = manifest_->ById(fixture_id);
    return embedded_test_server()->GetURL(
        manifest_->HostForOrigin(fixture.origin), fixture.url_path);
  }

  Origin WireOrigin(const GURL& url) {
    return OriginCodec::Get().ToWireOrigin(url::Origin::Create(url));
  }

  NodeHandle HandleFor(PageIntelligenceBroker* broker) {
    content::RenderFrameHost* main = web_contents()->GetPrimaryMainFrame();
    NodeHandle handle;
    handle.tab_id = broker->tab_id();
    handle.frame_id = broker->GetOrAssignFrameId(main);
    if (FrameObservationEndpoint* endpoint =
            broker->GetOrCreateEndpoint(main)) {
      handle.page_epoch = endpoint->page_epoch();
      handle.graph_revision = endpoint->last_reported_revision();
    }
    handle.node_id = SemanticNodeId{"node_1"};
    handle.expected_origin =
        OriginCodec::Get().ToWireOrigin(main->GetLastCommittedOrigin());
    return handle;
  }

  std::unique_ptr<test::BipFixtureManifest> manifest_;
};

// A verifier plus the loop that waits for its one outcome.
class VerifierRun {
 public:
  VerifierRun(content::WebContents* web_contents,
              PageIntelligenceBroker* broker,
              ScriptedNodeResolver* resolver)
      : verifier_(std::make_unique<PostconditionVerifier>(
            web_contents,
            broker->GetWeakPtr(),
            resolver->Callback(),
            /*browser_effects=*/nullptr)) {}

  void Start(const NodeHandle& handle,
             std::vector<Postcondition> postconditions,
             GraphRevision dispatch_revision,
             base::TimeDelta deadline) {
    verifier_->Start(handle, std::move(postconditions), dispatch_revision,
                     DispatchWatermark{}, IdempotencyPolicy::kPureRead,
                     deadline,
                     base::BindLambdaForTesting(
                         [this](PostconditionVerifier::Outcome outcome) {
                           outcome_ = std::move(outcome);
                           run_loop_.Quit();
                         }));
  }

  const PostconditionVerifier::Outcome& Await() {
    if (!outcome_.has_value()) {
      run_loop_.Run();
    }
    return *outcome_;
  }

  PostconditionVerifier* verifier() { return verifier_.get(); }

 private:
  std::unique_ptr<PostconditionVerifier> verifier_;
  base::RunLoop run_loop_;
  std::optional<PostconditionVerifier::Outcome> outcome_;
};

IN_PROC_BROWSER_TEST_F(PostconditionVerifierBrowserTest,
                       ACommitToAnAllowedOriginVerifiesFromTheBrowsersRecord) {
  PageIntelligenceBroker* broker = GetBroker();
  const GURL start = FixtureUrl("static-article");
  const GURL destination = FixtureUrl("static-product");
  ASSERT_TRUE(content::NavigateToURL(shell(), start));

  ScriptedNodeResolver resolver;
  VerifierRun run(web_contents(), broker, &resolver);

  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.allowed_origins.push_back(WireOrigin(destination));
  run.Start(HandleFor(broker), {postcondition}, /*dispatch_revision=*/1,
            base::Seconds(10));

  ASSERT_TRUE(content::NavigateToURL(shell(), destination));
  const PostconditionVerifier::Outcome& outcome = run.Await();

  EXPECT_EQ(outcome.code, ActionResultCode::kVerified);
  ASSERT_EQ(outcome.verified_by.size(), 1u);
  // The corroboration is the browser's navigation record. Nothing a renderer
  // said contributed to it, and kRendererAcknowledgement cannot appear here.
  EXPECT_EQ(outcome.verified_by.front(), VerifierKind::kBrowserNavigationEvent);
  EXPECT_EQ(outcome.telemetry, VerifierOutcome::kCorroborated);
}

IN_PROC_BROWSER_TEST_F(PostconditionVerifierBrowserTest,
                       ACommitToADisallowedOriginContradicts) {
  PageIntelligenceBroker* broker = GetBroker();
  const GURL start = FixtureUrl("static-article");
  const GURL elsewhere = FixtureUrl("static-product");
  ASSERT_TRUE(content::NavigateToURL(shell(), start));

  ScriptedNodeResolver resolver;
  VerifierRun run(web_contents(), broker, &resolver);

  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  // Allowed set names an origin the navigation will not land on.
  Origin unrelated;
  unrelated.kind = OriginKind::kTuple;
  unrelated.serialization = "https://nowhere.taffy.test";
  postcondition.allowed_origins.push_back(unrelated);
  run.Start(HandleFor(broker), {postcondition}, /*dispatch_revision=*/1,
            base::Seconds(10));

  ASSERT_TRUE(content::NavigateToURL(shell(), elsewhere));
  const PostconditionVerifier::Outcome& outcome = run.Await();

  // The effect happened and it was the wrong one, so this is a contradiction
  // rather than a timeout, and the code says the handle is dead.
  EXPECT_EQ(outcome.code, ActionResultCode::kDestinationChanged);
  EXPECT_TRUE(RequiresFreshObservation(outcome.code));
  EXPECT_EQ(outcome.telemetry, VerifierOutcome::kContradicted);
}

IN_PROC_BROWSER_TEST_F(PostconditionVerifierBrowserTest,
                       AnUndeclaredCommitCancelsTheVerification) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-article")));

  ScriptedNodeResolver resolver;
  // Resolves, but never at a newer revision, so the observation alone can
  // never settle anything.
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{"node_1"};
  facts.observed_at_revision = 1;
  facts.asserted_states = {NodeState::kVisible};
  resolver.facts = facts;

  VerifierRun run(web_contents(), broker, &resolver);
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kSectionVisible;
  run.Start(HandleFor(broker), {postcondition}, /*dispatch_revision=*/5,
            base::Seconds(10));

  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-product")));
  const PostconditionVerifier::Outcome& outcome = run.Await();

  // The corpus says a handle taken before a cross-document commit is
  // invalidated; whatever was going to be observed is gone.
  EXPECT_EQ(outcome.code, ActionResultCode::kCancelledByNavigation);
  EXPECT_TRUE(RequiresFreshObservation(outcome.code));
}

IN_PROC_BROWSER_TEST_F(PostconditionVerifierBrowserTest,
                       NothingHappeningIsATimeoutNotAFailure) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-article")));

  ScriptedNodeResolver resolver;
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{"node_1"};
  facts.observed_at_revision = 1;
  resolver.facts = facts;

  VerifierRun run(web_contents(), broker, &resolver);
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNodeStateChanged;
  postcondition.expected_node_state = NodeState::kChecked;
  run.Start(HandleFor(broker), {postcondition}, /*dispatch_revision=*/1,
            base::Milliseconds(300));

  const PostconditionVerifier::Outcome& outcome = run.Await();
  // The effect may still be pending, which is why this is a timeout and why
  // the result taxonomy keeps it distinct from a failure.
  EXPECT_EQ(outcome.code, ActionResultCode::kPostconditionTimeout);
  EXPECT_TRUE(IsAmbiguousOutcome(outcome.code));
  EXPECT_NE(outcome.code, ActionResultCode::kVerified);
  // The renderer stand-in answered every poll and it changed nothing: an
  // observation that is not newer than dispatch is not evidence of an effect.
  EXPECT_GT(resolver.calls, 0);
}

IN_PROC_BROWSER_TEST_F(PostconditionVerifierBrowserTest,
                       ARendererCrashNeverProducesVerified) {
  PageIntelligenceBroker* broker = GetBroker();
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("renderer-crash")));

  ScriptedNodeResolver resolver;
  // The resolver has to answer, and its answer has to leave the postcondition
  // pending, or the crash is not what settles this verification and the test
  // is about something else entirely.
  //
  // With no facts at all the resolver reports the node as gone, kSectionVisible
  // is contradicted on the very first poll — before anything has crashed — and
  // the outcome is a contradiction corroborated by a fresh snapshot. That is
  // correct behaviour for a node that vanished, and it is not this case: it
  // settled the run early and then the assertions below, which are about the
  // crash path, were read against an outcome the crash never touched.
  //
  // Resolving at the dispatch revision, with the node off screen, leaves it
  // open: a reading no newer than dispatch may say a scroll's target is in
  // view, but "off screen" there may be the reading from before the scroll,
  // so it neither satisfies nor contradicts (decision 0245). The renderer's
  // death is then the first terminal event to reach the verification.
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{"node_1"};
  facts.observed_at_revision = 1;
  facts.asserted_states = {NodeState::kOffscreen};
  resolver.facts = facts;

  VerifierRun run(web_contents(), broker, &resolver);
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kSectionVisible;
  run.Start(HandleFor(broker), {postcondition}, /*dispatch_revision=*/1,
            base::Seconds(10));

  content::RenderProcessHostWatcher watcher(
      web_contents(),
      content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  content::ScopedAllowRendererCrashes allow_crashes;
  ASSERT_FALSE(content::NavigateToURL(shell(), GURL("chrome://crash")));
  watcher.Wait();

  const PostconditionVerifier::Outcome& outcome = run.Await();
  // The corpus is explicit: no action is reported verified from a pre-crash
  // acknowledgement, and a consequential action is never replayed.
  EXPECT_NE(outcome.code, ActionResultCode::kVerified);
  EXPECT_TRUE(outcome.code == ActionResultCode::kRendererCrashed ||
              outcome.code == ActionResultCode::kOutcomeUnknown);
  EXPECT_TRUE(outcome.verified_by.empty());
}

IN_PROC_BROWSER_TEST_F(PostconditionVerifierBrowserTest,
                       EveryDeclaredEffectMustBeSatisfied) {
  PageIntelligenceBroker* broker = GetBroker();
  const GURL destination = FixtureUrl("static-product");
  ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl("static-article")));

  ScriptedNodeResolver resolver;
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{"node_1"};
  facts.observed_at_revision = 1;
  resolver.facts = facts;

  VerifierRun run(web_contents(), broker, &resolver);
  Postcondition navigation;
  navigation.kind = PostconditionKind::kCommittedNavigation;
  navigation.allowed_origins.push_back(WireOrigin(destination));
  Postcondition state;
  state.kind = PostconditionKind::kNodeStateChanged;
  state.expected_node_state = NodeState::kChecked;

  run.Start(HandleFor(broker), {navigation, state}, /*dispatch_revision=*/1,
            base::Milliseconds(500));
  ASSERT_TRUE(content::NavigateToURL(shell(), destination));

  const PostconditionVerifier::Outcome& outcome = run.Await();
  // The navigation happened; the state change did not. A proposal that
  // declares two effects is claiming both, and verifying the easier one would
  // let it buy a cheap success.
  EXPECT_NE(outcome.code, ActionResultCode::kVerified);
  ASSERT_EQ(outcome.outcomes.size(), 2u);
  EXPECT_TRUE(outcome.outcomes[0].satisfied);
  EXPECT_FALSE(outcome.outcomes[1].satisfied);
}

}  // namespace
}  // namespace taffy
