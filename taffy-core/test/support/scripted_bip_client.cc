// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/scripted_bip_client.h"

#include <utility>

#include "base/check.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace taffy::test {

namespace {

std::string PayloadAsText(const std::vector<uint8_t>& payload) {
  // Read as bytes, not decoded. The point is to search it for values that must
  // never be in it; decoding it would mean this file grew an opinion about the
  // encoding, and an encoding change would silently stop the search.
  return std::string(reinterpret_cast<const char*>(payload.data()),
                     payload.size());
}

}  // namespace

ScriptedBipClient::PendingRequest::PendingRequest() = default;
ScriptedBipClient::PendingRequest::PendingRequest(PendingRequest&&) noexcept =
    default;
ScriptedBipClient::PendingRequest::~PendingRequest() = default;

ScriptedBipClient::ScriptedBipClient(std::string label)
    : label_(std::move(label)) {}

ScriptedBipClient::~ScriptedBipClient() = default;

void ScriptedBipClient::Attach(PageIntelligenceService* service) {
  CHECK(service) << label_ << ": attached to a null service.";
  service_ = service;
}

PageIntelligenceService* ScriptedBipClient::service() {
  CHECK(service_) << label_
                  << ": no service attached. Call Attach() before submitting.";
  return service_;
}

void ScriptedBipClient::SetPreflight(base::OnceClosure preflight) {
  preflight_ = std::move(preflight);
}

void ScriptedBipClient::SetObservationGrant(ObservationPolicyGrant grant) {
  observation_grant_ = std::move(grant);
}

void ScriptedBipClient::RunPreflightOnce() {
  if (!preflight_) {
    return;
  }
  // Moved out before it runs: the preflight is allowed to submit through this
  // same client, and taking it out of the member first is what keeps that from
  // recursing forever.
  base::OnceClosure preflight = std::move(preflight_);
  std::move(preflight).Run();
}

// --- scripted calls ---------------------------------------------------------

ProtocolSupportEnvelope ScriptedBipClient::QueryProtocolSupport(
    const TabId& tab_id) {
  RunPreflightOnce();
  const RequestId request_id = service()->QueryProtocolSupport(tab_id);
  RunUntilSettled(request_id);
  const PendingRequest& pending = requests_[request_id];
  CHECK(pending.protocol_support)
      << label_ << ": QueryProtocolSupport settled without an envelope.";
  return *pending.protocol_support;
}

ObservationEnvelope ScriptedBipClient::Observe(ObservationRequest request) {
  return AwaitObservation(SubmitObservation(std::move(request)));
}

SubscriptionEnvelope ScriptedBipClient::Subscribe(SubscriptionRequest request) {
  RunPreflightOnce();
  CHECK(observation_grant_)
      << label_ << ": Subscribe requires an explicit test policy grant.";
  const RequestId request_id =
      service()->Subscribe(std::move(request), *observation_grant_);
  RunUntilSettled(request_id);
  const PendingRequest& pending = requests_[request_id];
  CHECK(pending.subscription)
      << label_ << ": Subscribe settled without an envelope.";
  return *pending.subscription;
}

ActionResult ScriptedBipClient::Act(AuthorizedActionEnvelope envelope) {
  return AwaitActionResult(SubmitAction(std::move(envelope)));
}

ActionResult ScriptedBipClient::Command(AuthorizedBrowserCommand command) {
  return AwaitActionResult(SubmitCommand(std::move(command)));
}

// --- submit without waiting -------------------------------------------------

RequestId ScriptedBipClient::SubmitObservation(ObservationRequest request) {
  RunPreflightOnce();
  CHECK(observation_grant_)
      << label_ << ": SubmitObservation requires an explicit test policy grant.";
  return service()->SubmitObservation(std::move(request), *observation_grant_);
}

RequestId ScriptedBipClient::SubmitAction(AuthorizedActionEnvelope envelope) {
  RunPreflightOnce();
  return service()->SubmitAction(std::move(envelope));
}

RequestId ScriptedBipClient::SubmitCommand(AuthorizedBrowserCommand command) {
  RunPreflightOnce();
  return service()->SubmitBrowserCommand(std::move(command));
}

ObservationEnvelope ScriptedBipClient::AwaitObservation(
    const RequestId& request_id) {
  RunUntilSettled(request_id);
  const PendingRequest& pending = requests_[request_id];
  CHECK(pending.observation)
      << label_ << ": observation " << request_id.value
      << " settled without an envelope. Exactly one terminal result per "
         "request is the API's contract, and a settled request with no result "
         "means the terminal path produced the wrong kind.";
  return *pending.observation;
}

ActionResult ScriptedBipClient::AwaitActionResult(const RequestId& request_id) {
  RunUntilSettled(request_id);
  const PendingRequest& pending = requests_[request_id];
  CHECK(pending.action) << label_ << ": action " << request_id.value
                        << " settled without a result.";
  return *pending.action;
}

ActionResult ScriptedBipClient::CancelActionAndAwait(
    const RequestId& request_id) {
  service()->Cancel(request_id);
  return AwaitActionResult(request_id);
}

ObservationEnvelope ScriptedBipClient::CancelObservationAndAwait(
    const RequestId& request_id) {
  service()->Cancel(request_id);
  return AwaitObservation(request_id);
}

// --- waiting ----------------------------------------------------------------

void ScriptedBipClient::RunUntilSettled(const RequestId& request_id) {
  const auto existing = requests_.find(request_id);
  if (existing != requests_.end() && existing->second.terminal_count > 0) {
    return;
  }
  base::RunLoop loop;
  awaited_request_ = request_id;
  awaited_loop_ = &loop;
  loop.Run();
  awaited_loop_ = nullptr;
  awaited_request_ = RequestId();

  const auto settled = requests_.find(request_id);
  CHECK(settled != requests_.end() && settled->second.terminal_count > 0)
      << label_ << ": the run loop for request " << request_id.value
      << " stopped without a terminal result.";
}

bool ScriptedBipClient::RunUntilSignalCount(size_t& target, size_t count) {
  base::RunLoop loop;
  target = count;
  stream_loop_ = &loop;
  loop.Run();
  stream_loop_ = nullptr;
  target = 0;
  return true;
}

bool ScriptedBipClient::WaitForDeltaCount(size_t count) {
  if (deltas_.size() >= count) {
    return true;
  }
  return RunUntilSignalCount(awaited_delta_count_, count) &&
         deltas_.size() >= count;
}

bool ScriptedBipClient::WaitForInvalidationCount(size_t count) {
  if (invalidations_.size() >= count) {
    return true;
  }
  return RunUntilSignalCount(awaited_invalidation_count_, count) &&
         invalidations_.size() >= count;
}

bool ScriptedBipClient::WaitForBackpressureCount(size_t count) {
  if (backpressure_notices_.size() >= count) {
    return true;
  }
  return RunUntilSignalCount(awaited_backpressure_count_, count) &&
         backpressure_notices_.size() >= count;
}

// --- terminal bookkeeping ---------------------------------------------------

ScriptedBipClient::PendingRequest& ScriptedBipClient::Settle(
    const RequestId& request_id) {
  PendingRequest& pending = requests_[request_id];
  ++pending.terminal_count;
  if (pending.terminal_count > 1) {
    contract_violations_.push_back(base::StrCat(
        {"request ", request_id.value, " received ",
         base::NumberToString(pending.terminal_count),
         " terminal results; the API promises exactly one, ever"}));
  }
  if (awaited_loop_ && awaited_request_ == request_id) {
    awaited_loop_->Quit();
  }
  return pending;
}

// --- PageIntelligenceResultSink ---------------------------------------------

void ScriptedBipClient::OnProtocolSupport(ProtocolSupportEnvelope result) {
  const RequestId request_id = result.request_id;
  protocol_supports_.push_back(result);
  Settle(request_id).protocol_support = std::move(result);
}

void ScriptedBipClient::OnObservationResult(ObservationEnvelope result) {
  const RequestId request_id = result.request_id;
  observations_.push_back(result);
  Settle(request_id).observation = std::move(result);
}

void ScriptedBipClient::OnSubscriptionResult(SubscriptionEnvelope result) {
  const RequestId request_id = result.request_id;
  subscriptions_.push_back(result);
  Settle(request_id).subscription = std::move(result);
}

void ScriptedBipClient::OnActionResult(ActionResult result) {
  const RequestId request_id = result.request_id;
  if (!result.terminal) {
    contract_violations_.push_back(
        base::StrCat({"action result for request ", request_id.value,
                      " arrived with terminal=false; this API delivers "
                      "terminal results only"}));
  }
  action_results_.push_back(result);
  Settle(request_id).action = std::move(result);
}

void ScriptedBipClient::OnDelta(DeltaEnvelope delta) {
  deltas_.push_back(std::move(delta));
  if (stream_loop_ && awaited_delta_count_ > 0 &&
      deltas_.size() >= awaited_delta_count_) {
    stream_loop_->Quit();
  }
}

void ScriptedBipClient::OnPageInvalidated(InvalidationNotice notice) {
  invalidations_.push_back(std::move(notice));
  if (stream_loop_ && awaited_invalidation_count_ > 0 &&
      invalidations_.size() >= awaited_invalidation_count_) {
    stream_loop_->Quit();
  }
}

void ScriptedBipClient::OnBackpressure(BackpressureNotice notice) {
  backpressure_notices_.push_back(std::move(notice));
  if (stream_loop_ && awaited_backpressure_count_ > 0 &&
      backpressure_notices_.size() >= awaited_backpressure_count_) {
    stream_loop_->Quit();
  }
}

void ScriptedBipClient::OnActorLeasePreempted(ActorLeaseId lease_id,
                                              TabId tab_id) {
  preemptions_.emplace_back(std::move(lease_id), std::move(tab_id));
}

// --- assertions -------------------------------------------------------------

::testing::AssertionResult ScriptedBipClient::AssertOneTerminalResultPerRequest()
    const {
  std::vector<std::string> findings = contract_violations_;
  for (const auto& [request_id, pending] : requests_) {
    if (pending.terminal_count == 0) {
      findings.push_back(base::StrCat(
          {"request ", request_id.value,
           " never received a terminal result; not even a cancelled request "
           "is allowed to end with none"}));
    }
  }
  if (findings.empty()) {
    return ::testing::AssertionSuccess();
  }
  ::testing::AssertionResult failure = ::testing::AssertionFailure();
  failure << label_ << ": the one-terminal-result-per-request contract was "
          << "broken " << findings.size() << " time(s):";
  for (const std::string& finding : findings) {
    failure << "\n  " << finding;
  }
  return failure;
}

std::string ScriptedBipClient::TranscriptForLeakScan() const {
  std::string out;
  for (const ObservationEnvelope& envelope : observations_) {
    out = base::StrCat({out, envelope.snapshot_id.value, " ",
                        envelope.tab_id.value, " ",
                        envelope.root_frame_id.value, " ",
                        envelope.page_epoch.value, " ",
                        envelope.committed_url_metadata.origin.serialization,
                        " ",
                        envelope.committed_url_metadata.path.value_or(
                            std::string()),
                        " ",
                        envelope.committed_url_metadata.url.value_or(
                            std::string()),
                        "\n"});
    for (const AdapterReport& report : envelope.adapters) {
      out = base::StrCat({out, report.detail_code.value_or(std::string()), "\n"});
    }
    // The projection on its way to the core service. The sink that matters most.
    out = base::StrCat({out, PayloadAsText(envelope.graph_payload), "\n"});
  }
  for (const DeltaEnvelope& delta : deltas_) {
    out = base::StrCat({out, PayloadAsText(delta.graph_payload), "\n"});
    for (const SemanticNodeId& node_id : delta.removed_node_ids) {
      out = base::StrCat({out, node_id.value, " "});
    }
    out = base::StrCat({out, "\n"});
  }
  for (const ActionResult& result : action_results_) {
    out = base::StrCat({out, result.detail_code.value_or(std::string()), "\n"});
  }
  for (const ProtocolSupportEnvelope& support : protocol_supports_) {
    out = base::StrCat({out, support.implementation_id, " ",
                        support.endpoint_protocol_version, "\n"});
  }
  return out;
}

}  // namespace taffy::test
