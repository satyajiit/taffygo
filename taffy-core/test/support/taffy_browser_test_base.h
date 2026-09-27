// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TAFFY_BROWSER_TEST_BASE_H_
#define TAFFY_TEST_SUPPORT_TAFFY_BROWSER_TEST_BASE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/test/content_browser_test.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"
#include "taffy/test/support/bip_request_builder.h"
#include "taffy/test/support/deterministic_id_source.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "taffy/test/support/recording_audit_stream.h"
#include "taffy/test/support/recording_journal.h"
#include "taffy/test/support/scripted_bip_client.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

namespace url {
class Origin;
}  // namespace url

// The fixture every Taffy browser test in this directory derives from.
//
// It assembles the whole browser-process half against the corpus, once, so that
// no individual test has to: four origins serving the fixture pages, a broker
// and a service on the shell's web contents, a scripted client in the core
// service's seat, a recording journal, a recording audit stream, a
// deterministic identifier source, and a request builder that produces
// envelopes a policy engine would have signed.
//
// It also carries three assertions that every test gets whether it asks or not,
// because each is a property that is easiest to break by accident in a test
// that was about something else:
//
//   1. **No request reached the exfiltration sink.** The corpus rule is that a
//      correct run never issues one.
//   2. **Exactly one terminal result per request.** The API's own contract; a
//      test that leaves a request unsettled has usually also left a renderer
//      call unbounded.
//   3. **No seeded secret reached any registered sink.** The projection on its
//      way to the core service, the journal, and the audit stream are
//      registered by default; a test that reaches a further sink adds it.
//
// A test that needs one of the three to fail — the adversarial suite has a
// few such cases — says so by calling ExpectExfiltrationAttempt() and friends,
// which turns the assertion into its opposite rather than switching it off.
// Switching a safety assertion off is how a suite stops noticing.
//
// **What this base deliberately does not do.** It does not install a renderer
// endpoint. Whether a real one is bound depends on the binary the suite runs
// in, and the two answers are genuinely different tests: the parity suite needs
// no endpoint at all, the correctness suite needs the real one, and the
// adversarial suite installs a scripted one per frame. Making that an explicit
// choice per suite is what keeps a correctness test from silently passing in a
// binary where no endpoint exists — which would report every observation as
// unsupported and call it a pass.

namespace taffy::test {

class TaffyBrowserTestBase : public content::ContentBrowserTest {
 public:
  explicit TaffyBrowserTestBase(FixtureOriginMap::Scheme fixture_scheme =
                                    FixtureOriginMap::Scheme::kHttp);
  TaffyBrowserTestBase(const TaffyBrowserTestBase&) = delete;
  TaffyBrowserTestBase& operator=(const TaffyBrowserTestBase&) = delete;
  ~TaffyBrowserTestBase() override;

  // content::ContentBrowserTest:
  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;

 protected:
  // The one place a suite may add a host resolver rule.
  //
  // Called from SetUpOnMainThread immediately before the wildcard rule that
  // maps every corpus hostname to the loopback interface. Both halves of that
  // sentence are load-bearing:
  //
  //   * RuleBasedHostResolverProc matches in insertion order and the wildcard
  //     matches everything, so a rule added after the wildcard can never fire.
  //     A suite that added one in its own SetUpOnMainThread would see it
  //     silently swallowed.
  //   * BrowserTestBase::InitializeNetworkProcess calls DisableModifications()
  //     before a test body runs, so a rule added from a test body takes the
  //     CHECK in RuleBasedHostResolverProc and aborts the process rather than
  //     failing the test.
  //
  // The default adds nothing. A suite that needs a name to fail resolution —
  // the error-page parity suite is the one that does — overrides this.
  virtual void AddHostResolverRules() {}

  // --- the corpus ----------------------------------------------------------

  FixtureOriginMap& origins() { return origins_; }

  GURL FixtureUrl(std::string_view fixture_id) const {
    return origins_.FixtureUrl(fixture_id);
  }
  GURL OriginUrl(std::string_view origin_key, std::string_view path) const {
    return origins_.Url(origin_key, path);
  }

  // Navigates the primary shell to a corpus fixture and waits for the commit.
  // Fails the test on anything but a successful commit at the expected URL.
  [[nodiscard]] bool NavigateToFixture(std::string_view fixture_id);

  // --- the browser-process half --------------------------------------------

  content::WebContents* web_contents();
  PageIntelligenceBroker* broker() { return broker_; }
  PageIntelligenceServiceImpl* service() { return service_.get(); }

  ScriptedBipClient& client() { return client_; }
  RecordingJournal& journal() { return journal_; }
  RecordingAuditStream& audit_stream() { return audit_stream_; }
  DeterministicIdSource& ids() { return ids_; }
  BipRequestBuilder& builder() { return *builder_; }

  // The frame identifier of the primary main frame, allocated on first use.
  FrameId MainFrameId();

  // The origin of the primary main frame, in the contract's representation.
  Origin MainFrameOrigin();

  // Registers a policy grant for the builder's task. Narrow by default:
  // one origin, no child frames, document scope. A suite that needs more says
  // so, because a grant that quietly admitted cross-origin frames would make
  // the frame-policy assertions meaningless.
  //
  // Each call replaces the scripted test client's one-shot policy input. The
  // production path instead receives a registered generation-bound grant from
  // the isolated core service and never stores grants per WebContents.
  void GrantObservation(std::vector<Origin> allowed_origins,
                        bool may_include_child_frames);

  // Registers the exact generation-bound capability for a node action and
  // reseals the envelope after binding it to this fixture's live lease. This
  // is the action-side sibling of GrantObservation: without it, every browser
  // test reaches the dispatcher with an unknown capability and can prove only
  // refusal paths, even when it names a real renderer node.
  [[nodiscard]] bool RegisterActionGrant(AuthorizedActionEnvelope* envelope);

  // Runs the product's task-action admission path, rather than the legacy BIP
  // path used by ScriptedBipClient::Act, and waits for its one terminal result.
  // RegisterActionGrant must have succeeded for the same envelope first.
  ActionResult DispatchTaskActionAndWait(AuthorizedActionEnvelope envelope);

  // Ends the current task and begins the next one: a fresh task identifier, a
  // fresh actor lease, and a builder carrying both. The outgoing task's lease
  // is released first, because at most one mutating lease exists per tab.
  //
  // A new task is still required when a scenario changes durable task identity
  // or actor-lease ownership; it is no longer used to work around a tab-local
  // grant registry because production has no such registry.
  void BeginNewTask();

  // --- inverted assertions, for the suites that are about the failure ------

  // The test expects a request to reach the exfiltration sink, and the teardown
  // assertion becomes "at least one hit". Used only by the case that proves the
  // sink itself works, so that the absence assertion everywhere else is known
  // to be meaningful rather than vacuous.
  void ExpectExfiltrationAttempt();

  // The test deliberately leaves a request unsettled — a wedged renderer with
  // no deadline reached, for example — and asserts on that itself.
  void AllowUnsettledRequests();

 private:
  // Mints the task identifier and the actor lease the builder is made from.
  // Called once from SetUpOnMainThread and again from every BeginNewTask().
  void StartTask();
  // Issues a fresh lease for the current task after the product released the
  // last one, and rebuilds the builder around it.
  void RenewLease();

  void AssertNoExfiltration();
  void AssertNoSeededSecretLeak();

  FixtureOriginMap origins_;
  DeterministicIdSource ids_;
  ScriptedBipClient client_;
  RecordingJournal journal_;
  RecordingAuditStream audit_stream_;
  ActorLeaseRegistry actor_leases_;
  CapabilityLedger capabilities_;

  raw_ptr<PageIntelligenceBroker> broker_ = nullptr;
  std::unique_ptr<PageIntelligenceServiceImpl> service_;
  std::unique_ptr<BipRequestBuilder> builder_;

  MonotonicMillis active_lease_expires_at_monotonic_ms_ = 0;

  bool expect_exfiltration_attempt_ = false;
  bool allow_unsettled_requests_ = false;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_TAFFY_BROWSER_TEST_BASE_H_
