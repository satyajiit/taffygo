// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>

#include "base/run_loop.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/common/public/bip_subscription.h"
#include "taffy/test/support/deterministic_clock.h"
#include "taffy/test/support/renderer_reply_plan.h"
#include "taffy/test/support/scripted_renderer_endpoint.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// Bounded inputs and bounded work: oversized messages, wedged renderers, and
// replies that arrive after their request already ended.
//
// The invariant is one sentence — every call that leaves the browser process is
// bounded and produces exactly one terminal result — and it is the invariant a
// hostile renderer attacks most cheaply. Not answering costs an attacker
// nothing. Answering enormously costs an attacker one allocation. Answering
// twice costs an attacker one extra message. All three have to be free of
// consequence in the browser process, and none of them can be defended by Mojo:
// a message that never arrives is not a message Mojo can validate.

namespace taffy::test {
namespace {

class MessageBoundsTest : public TaffyBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    ASSERT_TRUE(NavigateToFixture("static-article"));
    GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
    endpoint_ = ScriptedRendererEndpoint::InstallFor(
        web_contents()->GetPrimaryMainFrame());
    ASSERT_TRUE(endpoint_);
  }

 protected:
  ScriptedRendererEndpoint& endpoint() { return *endpoint_; }

  // A handle for the first node of an observation, assembled once. Every
  // action case needs one, and eleven fields that have to agree assembled
  // slightly differently per test would turn an ordinary refusal into
  // something that looks exactly like the bound under test failing.
  NodeHandle HandleFrom(const ObservationEnvelope& observed) {
    NodeHandle handle;
    handle.tab_id = broker()->tab_id();
    handle.frame_id = MainFrameId();
    handle.page_epoch = observed.page_epoch;
    handle.graph_revision = observed.graph_revision;
    handle.node_id = SemanticNodeId{"node-0"};
    handle.expected_origin = MainFrameOrigin();
    return handle;
  }

 private:
  std::unique_ptr<ScriptedRendererEndpoint> endpoint_;
};

// **Property: a renderer that never answers still produces exactly one terminal
// result.** The bound is the browser's own deadline. Without it the request
// never settles and "exactly one terminal result per request" quietly becomes
// zero, which is indistinguishable from a hang.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, AWedgedRendererStillSettlesTheRequest) {
  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kNeverAnswers;
  endpoint().SetPlan(plan);

  ObservationRequest request =
      builder().Observation(MainFrameId(), ObservationScope::kDocument);
  // Short, so the deadline is reached inside the test rather than inside the
  // harness's own timeout — where it would be reported as a hang rather than
  // as the bound working.
  request.budget.deadline_ms = 250;

  const ObservationEnvelope envelope = client().Observe(std::move(request));
  EXPECT_EQ(ObservationResultCode::kDeadlineExceeded, envelope.code)
      << "A renderer that answered nothing produced result code "
      << static_cast<int>(envelope.code)
      << ". The deadline is the only thing between a wedged renderer and a "
         "request that never ends.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: negotiation is bounded too.** An endpoint that cannot answer is
// unsupported, not assumed — and an endpoint that will not answer must not
// leave negotiation pending forever, because everything else waits behind it.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, NegotiationIsBounded) {
  RendererReplyPlan plan;
  plan.protocol_info = ProtocolInfoBehaviour::kNeverAnswers;
  endpoint().SetPlan(plan);

  const ProtocolSupportEnvelope support =
      client().QueryProtocolSupport(broker()->tab_id());
  EXPECT_NE(ObservationResultCode::kOk, support.code);
  EXPECT_TRUE(support.adapters.empty() ||
              support.endpoint_protocol_version.empty())
      << "Negotiation timed out and still reported adapter support. An "
         "endpoint that could not answer is unsupported, and a set of assumed "
         "defaults is how a planner ends up planning against capabilities "
         "nothing has.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: a command the renderer accepts and never answers still produces
// exactly one terminal result — and that result is ambiguous rather than
// negative.** This is the only one of the bounded calls with a side effect
// behind it. The command was journalled and handed to the input path, so the
// browser cannot say the action did not happen; it can only say it does not
// know. Anything more definite is a claim about a page nobody observed.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, AnUnansweredActionCommandStillSettles) {
  RendererReplyPlan plan;
  // Resolves step 5 honestly so the wedge lands on the command rather than on
  // the node resolution that precedes it. One knob for both calls could only
  // ever exercise whichever came first.
  plan.action = ActionBehaviour::kResolvesThenNeverAnswers;
  endpoint().SetPlan(plan);

  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  const NodeHandle handle = HandleFrom(observed);

  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  navigated.allowed_origins.push_back(MainFrameOrigin());
  // Both lines matter and neither is ceremony. A bare kCommittedNavigation
  // postcondition carries no observable claim, so AllPostconditionsAreVerifiable
  // refuses the envelope as unverifiable before any capability is looked at;
  // and an envelope whose capability was never registered is refused by the
  // ledger as malformed. Both answer kDeniedByPolicy, which is a step-4 code —
  // so without them this case measures the admission gate and never reaches the
  // renderer-deadline behaviour it is named for.
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, handle.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&envelope));
  const ActionResult result = client().Act(std::move(envelope));

  ASSERT_FALSE(endpoint().received_commands().empty())
      << "The command never reached the endpoint, so whatever settled this "
         "request was not the ExecuteRendererAction deadline and nothing "
         "below is evidence about that call's bound.";

  EXPECT_EQ(ActionResultCode::kOutcomeUnknown, result.result_code)
      << "A command that was journalled, dispatched and never answered "
         "settled with code "
      << static_cast<int>(result.result_code)
      << ". Unknown is the only true answer here: the effect may or may not "
         "have happened and the browser cannot prove which.";
  EXPECT_TRUE(result.dispatched)
      << "The result says nothing was dispatched. That flag is what makes a "
         "refusal safe to treat as 'nothing happened', and this command was "
         "on the renderer's input path before the silence began.";
  EXPECT_TRUE(result.repeat_may_duplicate_effect)
      << "An ambiguous outcome reported as safe to repeat is how one action "
         "becomes two on the page.";
  EXPECT_TRUE(result.verified_by.empty())
      << "A renderer that said nothing corroborated nothing.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: the node resolution that precedes a dispatch is bounded too.**
// Step 5 spends no capability and touches no page, so a renderer that never
// answers it costs nothing — which is exactly why an unbounded wait there
// would be the cheapest way to hold an action open forever. A silent renderer
// is not evidence that the node is still there, so the honest reading is
// unresolved, and unresolved is the node-exists precondition failing.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, AnUnansweredNodeResolutionStillSettles) {
  RendererReplyPlan plan;
  plan.action = ActionBehaviour::kNeverAnswers;
  endpoint().SetPlan(plan);

  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  const NodeHandle handle = HandleFrom(observed);

  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  navigated.allowed_origins.push_back(MainFrameOrigin());
  // Both lines matter and neither is ceremony. A bare kCommittedNavigation
  // postcondition carries no observable claim, so AllPostconditionsAreVerifiable
  // refuses the envelope as unverifiable before any capability is looked at;
  // and an envelope whose capability was never registered is refused by the
  // ledger as malformed. Both answer kDeniedByPolicy, which is a step-4 code —
  // so without them this case measures the admission gate and never reaches the
  // renderer-deadline behaviour it is named for.
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, handle.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&envelope));
  const ActionResult result = client().Act(std::move(envelope));

  EXPECT_EQ(ActionResultCode::kNodeGone, result.result_code)
      << "A resolve that was never answered settled with code "
      << static_cast<int>(result.result_code)
      << ". A renderer that says nothing about a node has not shown it is "
         "there, and gone is the refusal every caller already handles.";
  ASSERT_TRUE(result.failed_precondition.has_value())
      << "The action was refused without naming the precondition that "
         "refused it, so a caller cannot tell a vanished node from a policy "
         "denial.";
  EXPECT_EQ(PreconditionKind::kNodeExists, *result.failed_precondition);
  EXPECT_FALSE(result.dispatched)
      << "Nothing was journalled and nothing left for the input path, so "
         "reporting a dispatch would make a refusal look like a side effect.";
  EXPECT_FALSE(result.repeat_may_duplicate_effect)
      << "Nothing happened, so a repeat can duplicate nothing.";
  EXPECT_TRUE(endpoint().received_commands().empty())
      << "A renderer command was built for an action whose target was never "
         "resolved. Step 5 exists so that step 8 never runs against a node "
         "the browser could not re-read.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: opening a stream is bounded, and a stream that never opened
// delivers nothing.** Teardown already settles a subscribe whose pipe went
// away. The case it cannot see is an endpoint that is still there, still
// holding the delta pipe, and simply silent — and a subscription request that
// never settles is worse than a one-shot one that never settles, because the
// caller is waiting to start a projection it will keep waiting on.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, AnUnansweredSubscribeStillSettles) {
  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  RendererReplyPlan plan;
  plan.subscribe = SubscriptionBehaviour::kNeverAnswers;
  // So the endpoint has something to push once the browser has torn the
  // receiver down. A stream the subscriber was told does not exist must not
  // then deliver into it.
  plan.delta = DeltaBehaviour::kHonest;
  endpoint().SetPlan(plan);

  const SubscriptionEnvelope subscription = client().Subscribe(
      builder().Subscription(MainFrameId(), observed.page_epoch));

  EXPECT_EQ(ObservationResultCode::kDeadlineExceeded, subscription.code)
      << "A subscribe that was never answered settled with code "
      << static_cast<int>(subscription.code)
      << ". Without the deadline the subscriber waits forever for a stream "
         "the renderer already decided not to open.";
  EXPECT_FALSE(subscription.subscription_id.has_value())
      << "An identifier was handed out for a stream that was never opened. A "
         "subscriber holding one has every reason to believe it can cancel, "
         "resume, or apply deltas against it.";
  EXPECT_FALSE(subscription.base_revision.has_value())
      << "A base revision was reported for a projection that cannot start.";

  endpoint().PumpDeltas();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(client().deltas().empty())
      << "A delta arrived on a subscription the caller was told does not "
         "exist. The receiver is removed before the request is settled "
         "precisely so that a late endpoint has nowhere to deliver.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: an unsupported major version is refused on the version alone.**
// The body validating is the point: a decoder must refuse before the body
// persuades it that everything looks familiar.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, AnUnsupportedMajorVersionIsRefused) {
  RendererReplyPlan plan;
  plan.protocol_info = ProtocolInfoBehaviour::kUnsupportedMajorVersion;
  endpoint().SetPlan(plan);

  const ProtocolSupportEnvelope support =
      client().QueryProtocolSupport(broker()->tab_id());
  EXPECT_EQ(ObservationResultCode::kUnsupported, support.code)
      << "An endpoint speaking a major version this build does not know was "
         "accepted. A major version changes required meaning; the body looking "
         "correct is exactly the trap.";
}

// **Property: an oversized message is refused rather than parsed.** The
// endpoint reports a maximum message size and the browser enforces its own; a
// message past the bound in force is refused, and the refusal is a bounded
// failure rather than an allocation.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, AnOversizedReplyIsRefused) {
  // Sized from the effective limits the browser reports, so the test does not
  // restate a ceiling that lives in exactly one place.
  RendererReplyPlan probe;
  endpoint().SetPlan(probe);
  const ProtocolSupportEnvelope support =
      client().QueryProtocolSupport(broker()->tab_id());
  ASSERT_GT(support.effective_limits.max_text_bytes, 0u);

  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kOversizedMessage;
  plan.honest_node_count = 1;
  plan.oversized_message_bytes = support.effective_limits.max_text_bytes + 1024;
  endpoint().SetPlan(plan);

  const ObservationEnvelope envelope = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_NE(ObservationResultCode::kOk, envelope.code)
      << "A reply past the byte bound in force was accepted as complete.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

// **Property: a late reply is dropped and counted, never delivered.** After a
// request has produced its terminal result, nothing that arrives for it may
// reach a consumer — including a second reply from a renderer that decided to
// answer twice.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, ALateReplyIsDroppedRatherThanDelivered) {
  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kRepliesTwice;
  endpoint().SetPlan(plan);

  client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  // Give anything late a chance to arrive before asserting it did not.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest())
      << "A second answer for one request reached the caller. Exactly one "
         "terminal result per request is the contract every consumer is "
         "written against.";
}

// **Property: a cancelled request still gets its one terminal result, with a
// cancellation code.** Cancel is advisory about timing and absolute about
// outcome; a cancelled request that produced nothing would be
// indistinguishable from a lost one.
IN_PROC_BROWSER_TEST_F(MessageBoundsTest, ACancelledRequestStillSettles) {
  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kNeverAnswers;
  endpoint().SetPlan(plan);

  ObservationRequest request =
      builder().Observation(MainFrameId(), ObservationScope::kDocument);
  request.budget.deadline_ms = 30000;
  const RequestId request_id = client().SubmitObservation(std::move(request));

  const ObservationEnvelope envelope =
      client().CancelObservationAndAwait(request_id);
  EXPECT_EQ(ObservationResultCode::kCancelled, envelope.code)
      << "A cancelled request settled with code "
      << static_cast<int>(envelope.code)
      << " rather than a cancellation. A consumer cannot tell a cancellation "
         "from a failure without it.";
  EXPECT_TRUE(client().AssertOneTerminalResultPerRequest());
}

}  // namespace
}  // namespace taffy::test
