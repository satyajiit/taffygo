// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_SCRIPTED_BIP_CLIENT_H_
#define TAFFY_TEST_SUPPORT_SCRIPTED_BIP_CLIENT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "taffy/common/public/page_intelligence_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
class RunLoop;
}  // namespace base

// The sandboxed core service's seat at the trusted browser-process API, played
// by a test.
//
// In production PageIntelligenceService is reached through a typed Core
// Service effect broker. A browser test should isolate the browser half: using
// the real service here would let a service bug and a broker bug produce the
// same red.
//
// So this class is the scripted client. A test writes a script as a sequence of
// calls, each of which blocks until the single terminal result for that request
// arrives, and the class holds the API's own contract while it does:
//
//   * Exactly one terminal result per request identifier, ever. Not zero, not
//     two. A second one is recorded as a violation rather than overwriting the
//     first, so a test can assert on the exact failure it was hunting.
//   * A cancelled request still produces its one terminal result.
//   * Every unsolicited signal — delta, invalidation, backpressure, actor lease
//     preemption — is kept in arrival order, because for several of the
//     properties this directory proves, the order is the property.
//
// It also builds the transcript the leak scanner reads. That is the reason the
// graph payload is retained: the payload is the untrusted page projection on
// its way to the core service, so it is the sink that matters most and the one a
// suite would otherwise have no way to inspect.
//
// UI thread only, like the interface it consumes.

namespace taffy::test {

class ScriptedBipClient : public PageIntelligenceResultSink {
 public:
  // `label` appears in every failure message this class produces, so a test
  // with two clients can tell them apart.
  explicit ScriptedBipClient(std::string label);
  ScriptedBipClient(const ScriptedBipClient&) = delete;
  ScriptedBipClient& operator=(const ScriptedBipClient&) = delete;
  ~ScriptedBipClient() override;

  // The service must outlive this client.
  void Attach(PageIntelligenceService* service);

  // A check to run once, immediately before the first scripted call this client
  // makes, and then never again.
  //
  // It exists for preconditions that can only be checked against a document,
  // not against a fixture's SetUpOnMainThread. The renderer endpoint
  // requirement is the case that motivated it: content_shell's initial
  // about:blank has no observable document at all, so asking it whether a
  // renderer endpoint answers gets "no" for a reason that has nothing to do
  // with the binary under test. Asking on the first scripted call instead asks
  // about whatever page the test actually navigated to, which is the document
  // the test's own assertions are about.
  //
  // One shot, and moved out before it runs, so a preflight that itself submits
  // a request — Require() consumes a QueryProtocolSupport — does not recurse.
  // A test that issues no scripted call never runs it, which is correct: a test
  // that never uses the endpoint is not measuring it.
  void SetPreflight(base::OnceClosure preflight);

  // Test-root-only policy input. Production receives a one-shot projection of
  // a registered Rust-minted capability; browser tests state the same ceiling
  // explicitly before issuing observation or subscription calls.
  void SetObservationGrant(ObservationPolicyGrant grant);

  // --- scripted calls: submit, then run until the one terminal result -------

  ProtocolSupportEnvelope QueryProtocolSupport(const TabId& tab_id);
  ObservationEnvelope Observe(ObservationRequest request);
  SubscriptionEnvelope Subscribe(SubscriptionRequest request);
  ActionResult Act(AuthorizedActionEnvelope envelope);
  ActionResult Command(AuthorizedBrowserCommand command);

  // --- submit without waiting, for the races -------------------------------
  //
  // A race test has to do something between the submission and the result —
  // navigate, close a tab, kill a renderer, deliver user input — so the
  // blocking form above cannot express it.

  RequestId SubmitObservation(ObservationRequest request);
  RequestId SubmitAction(AuthorizedActionEnvelope envelope);
  RequestId SubmitCommand(AuthorizedBrowserCommand command);

  ObservationEnvelope AwaitObservation(const RequestId& request_id);
  ActionResult AwaitActionResult(const RequestId& request_id);

  // Cancels and then waits for the terminal result the contract still promises.
  ActionResult CancelActionAndAwait(const RequestId& request_id);
  ObservationEnvelope CancelObservationAndAwait(const RequestId& request_id);

  // --- streams -------------------------------------------------------------

  // Runs the message loop until at least `count` deltas have arrived, and
  // returns whether they did.
  //
  // There is no timeout here, deliberately. A request has the browser's own
  // deadline behind it and always settles; a STREAM has no such guarantee,
  // because a page that never mutates never produces a delta. Adding a timeout
  // would turn "the stream is broken" into "the stream was slow", and a suite
  // that treated those the same would flake instead of failing. A stream that
  // genuinely never arrives ends as a harness timeout naming this call, which
  // is the honest report.
  [[nodiscard]] bool WaitForDeltaCount(size_t count);
  [[nodiscard]] bool WaitForInvalidationCount(size_t count);
  [[nodiscard]] bool WaitForBackpressureCount(size_t count);

  const std::vector<DeltaEnvelope>& deltas() const { return deltas_; }
  const std::vector<InvalidationNotice>& invalidations() const {
    return invalidations_;
  }
  const std::vector<BackpressureNotice>& backpressure_notices() const {
    return backpressure_notices_;
  }
  const std::vector<std::pair<ActorLeaseId, TabId>>& preemptions() const {
    return preemptions_;
  }

  const std::vector<ObservationEnvelope>& observations() const {
    return observations_;
  }
  const std::vector<ActionResult>& action_results() const {
    return action_results_;
  }

  // --- contract assertions -------------------------------------------------

  // Fails naming every request identifier that received two terminal results
  // or none, and every late signal that arrived after a terminal result.
  [[nodiscard]] ::testing::AssertionResult AssertOneTerminalResultPerRequest()
      const;

  // Everything this client ever received, flattened, including the encoded
  // graph payloads. Handed to CanaryLeakScanner as the projection sink.
  std::string TranscriptForLeakScan() const;

  // PageIntelligenceResultSink:
  void OnProtocolSupport(ProtocolSupportEnvelope result) override;
  void OnObservationResult(ObservationEnvelope result) override;
  void OnSubscriptionResult(SubscriptionEnvelope result) override;
  void OnActionResult(ActionResult result) override;
  void OnDelta(DeltaEnvelope delta) override;
  void OnPageInvalidated(InvalidationNotice notice) override;
  void OnBackpressure(BackpressureNotice notice) override;
  void OnActorLeasePreempted(ActorLeaseId lease_id, TabId tab_id) override;

 private:
  // One in-flight request and whatever terminal result it produced.
  struct PendingRequest {
    PendingRequest();
    PendingRequest(PendingRequest&&) noexcept;
    ~PendingRequest();

    int terminal_count = 0;
    std::optional<ProtocolSupportEnvelope> protocol_support;
    std::optional<ObservationEnvelope> observation;
    std::optional<SubscriptionEnvelope> subscription;
    std::optional<ActionResult> action;
  };

  // Marks a terminal result for `request_id` and stops the run loop waiting on
  // it, if any. Records a violation when one has already arrived.
  PendingRequest& Settle(const RequestId& request_id);

  void RunUntilSettled(const RequestId& request_id);
  [[nodiscard]] bool RunUntilSignalCount(size_t& observed_counter_target,
                                         size_t count);

  PageIntelligenceService* service();

  // Runs the preflight, if one is still pending. Moved out first, so it runs
  // at most once and cannot re-enter through its own scripted call.
  void RunPreflightOnce();

  const std::string label_;
  base::OnceClosure preflight_;
  raw_ptr<PageIntelligenceService> service_ = nullptr;
  std::optional<ObservationPolicyGrant> observation_grant_;

  std::map<RequestId, PendingRequest> requests_;
  std::vector<std::string> contract_violations_;

  // The request the current run loop is waiting for, and the loop itself.
  RequestId awaited_request_;
  raw_ptr<base::RunLoop> awaited_loop_ = nullptr;
  // A stream wait: the loop stops when the named counter reaches the target.
  size_t awaited_delta_count_ = 0;
  size_t awaited_invalidation_count_ = 0;
  size_t awaited_backpressure_count_ = 0;
  raw_ptr<base::RunLoop> stream_loop_ = nullptr;

  std::vector<ProtocolSupportEnvelope> protocol_supports_;
  std::vector<ObservationEnvelope> observations_;
  std::vector<SubscriptionEnvelope> subscriptions_;
  std::vector<ActionResult> action_results_;
  std::vector<DeltaEnvelope> deltas_;
  std::vector<InvalidationNotice> invalidations_;
  std::vector<BackpressureNotice> backpressure_notices_;
  std::vector<std::pair<ActorLeaseId, TabId>> preemptions_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_SCRIPTED_BIP_CLIENT_H_
