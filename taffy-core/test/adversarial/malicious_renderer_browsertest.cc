// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_subscription.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/audit_stream_assertions.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "taffy/test/support/journal_assertions.h"
#include "taffy/test/support/renderer_reply_plan.h"
#include "taffy/test/support/scripted_renderer_endpoint.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// A renderer that answers badly on purpose.
//
// The threat model's adversary A2 is a compromised renderer. It is inside the
// sandbox, it speaks the protocol correctly, and every message it sends is
// well formed — which is precisely why Mojo cannot defend against it and the
// browser process has to. Each case below states the property it defends and
// ends in a refusal rather than a repair.
//
// Two attacks are absent from this file on purpose, and their absence is the
// design rather than a gap. An unknown enumeration member and a malformed body
// cannot be sent through the generated bindings at all: every BIP enumeration
// is closed, so Mojo's own validator rejects the message before any Taffy code
// runs. Those are proved by the compatibility fixtures in taffy-core/contracts/bip/compat
// and by the parser fuzzers in this directory's fuzz/ subdirectory, both of
// which construct the bytes directly.

namespace taffy::test {
namespace {

class MaliciousRendererTest : public TaffyBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    ASSERT_TRUE(NavigateToFixture("static-article"));
    GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
    endpoint_ =
        ScriptedRendererEndpoint::InstallFor(web_contents()->GetPrimaryMainFrame());
    ASSERT_TRUE(endpoint_);
  }

 protected:
  ScriptedRendererEndpoint& endpoint() { return *endpoint_; }

  ObservationEnvelope ObserveWith(RendererReplyPlan plan) {
    endpoint_->SetPlan(std::move(plan));
    return client().Observe(
        builder().Observation(MainFrameId(), ObservationScope::kDocument));
  }

 private:
  std::unique_ptr<ScriptedRendererEndpoint> endpoint_;
};

// **Property: a renderer's echo of identity is checked, never adopted.** A
// reply that names a page epoch the broker did not bind describes a different
// document, and adopting it would let a retired document answer for a live one.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, AForeignEpochEchoIsDiscarded) {
  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kForeignPageEpochEcho;
  plan.foreign_page_epoch = ids().NeverIssued("epoch");

  const ObservationEnvelope envelope = ObserveWith(plan);
  EXPECT_NE(ObservationResultCode::kOk, envelope.code)
      << "A snapshot echoing an epoch the broker never bound was accepted. The "
         "echo exists so the broker can reject a reply belonging to a retired "
         "document; accepting it removes the only reason it is on the wire.";
  EXPECT_NE(plan.foreign_page_epoch, envelope.page_epoch.value)
      << "The invented epoch reached the caller.";
}

// **Property: URL and origin metadata are browser-owned.** They arrive from a
// renderer on this wire, so the broker overwrites both from its own committed
// navigation record before any consumer sees them.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, AForeignOriginEchoIsOverwritten) {
  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kForeignOriginEcho;
  plan.foreign_origin_serialization = "https://bank.example";

  const ObservationEnvelope envelope = ObserveWith(plan);
  EXPECT_NE("https://bank.example",
            envelope.committed_url_metadata.origin.serialization)
      << "A renderer told the browser which origin it was on and was believed. "
         "Every origin-scoped decision downstream is now being made about a "
         "site the renderer chose.";
  EXPECT_NE("https://bank.example", envelope.origin.serialization);
  EXPECT_EQ(MainFrameOrigin().serialization, envelope.origin.serialization);
}

// **Property: the browser owns the frame topology.** A renderer describes what
// it can see; parentage comes from Chromium's own frame tree. A frame that is
// not in that tree is not in the observation, whatever the renderer claims.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, AFabricatedFrameIsNotAdopted) {
  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kFabricatedFrame;

  const ObservationEnvelope envelope = ObserveWith(plan);
  for (const FrameSummary& frame : envelope.frames) {
    EXPECT_NE("frame-that-is-not-in-the-tree", frame.frame_id.value)
        << "An invented frame reached the caller. A handle could now be issued "
           "against a frame the browser has never seen, and every check that "
           "resolves a frame identifier would answer 'gone' at dispatch time "
           "at best.";
    EXPECT_NE("http://invented.invalid", frame.origin.serialization);
  }
}

// **Property: budgets are the browser's ceiling, not the renderer's
// suggestion.** A reply larger than the clamped request is over budget, and the
// result says so rather than being silently accepted or silently trimmed.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, AnOverBudgetReplyIsRefused) {
  // The budget the browser actually sent is the clamped one, so the plan is
  // built from what the endpoint received rather than from what the test asked
  // for. A test that assumed its own number would be testing its assumption.
  RendererReplyPlan probe;
  ObserveWith(probe);
  ASSERT_FALSE(endpoint().received_snapshot_requests().empty());
  const uint32_t clamped_max_nodes =
      endpoint().received_snapshot_requests().front()->max_nodes;
  ASSERT_GT(clamped_max_nodes, 0u);

  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kOverBudgetNodeCount;
  plan.over_budget_node_count = clamped_max_nodes + 1;

  const ObservationEnvelope envelope = ObserveWith(plan);
  EXPECT_NE(ObservationResultCode::kOk, envelope.code)
      << "A reply carrying more nodes than the clamped request allowed was "
         "accepted as complete. The clamp is what keeps one page from "
         "exhausting the browser process.";
}

// **Property: a request can never widen the granted policy.** Whatever the test
// asks for, what leaves the process is the request narrowed by the process
// ceiling and then by the grant. Observed from the endpoint, which is the only
// place the narrowed request is visible.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, TheRequestThatLeavesIsTheNarrowedOne) {
  ObservationRequest request =
      builder().Observation(MainFrameId(), ObservationScope::kDocument);
  request.budget.max_nodes = 1u << 24;
  request.budget.max_total_bytes = 1u << 30;
  request.budget.deadline_ms = 1u << 24;
  request.include_child_frames = true;

  endpoint().SetPlan(RendererReplyPlan());
  client().Observe(std::move(request));

  ASSERT_FALSE(endpoint().received_snapshot_requests().empty());
  const auto& sent = endpoint().received_snapshot_requests().back();
  EXPECT_LT(sent->max_nodes, 1u << 24)
      << "The absurd node budget reached the renderer unchanged.";
  EXPECT_LT(sent->max_total_bytes, 1u << 30);
  EXPECT_LT(sent->deadline_ms, 1u << 24);
  EXPECT_TRUE(sent->allowed_origins.empty() ||
              sent->allowed_origins.size() == 1u)
      << "The grant admits one origin, and the request that left named more.";
}

// **Property: a renderer cannot express that an action succeeded.** The reply
// type has no member meaning "the effect happened"; dispatched is a statement
// about the renderer. Verified comes only from the browser-side verifier.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, ClaimedDispatchNeverReachesVerified) {
  RendererReplyPlan plan;
  plan.action = ActionBehaviour::kClaimsDispatchWithNoEffect;
  endpoint().SetPlan(plan);

  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = MainFrameId();
  handle.page_epoch = observed.page_epoch;
  handle.graph_revision = observed.graph_revision;
  handle.node_id = SemanticNodeId{"node-0"};
  handle.expected_origin = MainFrameOrigin();

  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  // A committed-navigation postcondition carries an observable claim only when
  // it names a destination or an allowed origin, and an action whose declared
  // effect cannot be checked is refused as unverifiable before any capability
  // is looked at. Registering the grant answers the other half: an envelope the
  // ledger has no record of is malformed. Both are kDeniedByPolicy, and both
  // arrive before the renderer is asked to do anything — so without these two
  // lines this case measures the admission gate and never reaches the
  // compromised renderer it is named for.
  navigated.allowed_origins.push_back(MainFrameOrigin());
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, observed.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&envelope));
  const ActionResult result = client().Act(std::move(envelope));

  EXPECT_NE(ActionResultCode::kVerified, result.result_code)
      << "An action reached verified on the renderer's word. A compromised "
         "renderer would say exactly this, and nothing navigated.";
  // Not kDeniedByPolicy. That is the admission gate's answer, and it arrives
  // before a renderer command exists — so it would satisfy the assertion above
  // while proving nothing about the renderer's behaviour. This line is what
  // keeps the case from passing vacuously if the envelope ever stops being
  // admissible again.
  EXPECT_NE(ActionResultCode::kDeniedByPolicy, result.result_code);
  EXPECT_TRUE(AuditStreamAssertions::NoVerifiedWithoutCorroboration(
      audit_stream()));
}

// **Property: a revision the renderer invented proves nothing.** A verifier
// that accepted a revision advance reported by the party being verified would
// be asking the suspect to confirm the alibi.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, AnInventedRevisionDoesNotSatisfyTheVerifier) {
  RendererReplyPlan plan;
  plan.action = ActionBehaviour::kInventsAdvancedRevision;
  endpoint().SetPlan(plan);

  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = MainFrameId();
  handle.page_epoch = observed.page_epoch;
  handle.graph_revision = observed.graph_revision;
  handle.node_id = SemanticNodeId{"node-0"};
  handle.expected_origin = MainFrameOrigin();

  Postcondition state_changed;
  state_changed.kind = PostconditionKind::kNodeStateChanged;
  // A node-state postcondition carries a claim only when it says which state,
  // and an unverifiable action is refused before the capability is reached. The
  // registration answers the other half. See the note on
  // ClaimedDispatchNeverReachesVerified: without both, this case is answered by
  // the admission gate and the invented revision is never examined.
  state_changed.expected_node_state = NodeState::kChecked;
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, observed.graph_revision, {state_changed});
  ASSERT_TRUE(RegisterActionGrant(&envelope));
  const ActionResult result = client().Act(std::move(envelope));

  EXPECT_NE(ActionResultCode::kVerified, result.result_code);
  // Not kDeniedByPolicy. That is the admission gate's answer, and it arrives
  // before a renderer command exists — so it would satisfy the assertion above
  // while proving nothing about the renderer's behaviour. This line is what
  // keeps the case from passing vacuously if the envelope ever stops being
  // admissible again.
  EXPECT_NE(ActionResultCode::kDeniedByPolicy, result.result_code);
}

// **Property: protocol discovery is descriptive and side-effect free.** The
// complete implementation set is useful to a planner, but advertising an
// action neither authorizes it nor dispatches it.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest,
                       ProtocolSupportReportsActionsWithoutExecutingOne) {
  endpoint().SetPlan(RendererReplyPlan());

  const ProtocolSupportEnvelope support =
      client().QueryProtocolSupport(broker()->tab_id());
  EXPECT_EQ(
      (std::vector<ActionType>{ActionType::kScrollIntoView, ActionType::kFocus,
                               ActionType::kActivate, ActionType::kSetText,
                               ActionType::kSelectOption, ActionType::kToggle,
                               ActionType::kSubmitForm}),
      support.supported_action_types);
  EXPECT_TRUE(endpoint().received_commands().empty());
}

// **Property: an endpoint cannot raise the process ceiling by reporting a
// higher one.** The effective limits a caller plans against are the endpoint's
// narrowed by the ceiling, never the endpoint's own.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, OversizedLimitsAreClampedNotAdopted) {
  RendererReplyPlan plan;
  plan.protocol_info = ProtocolInfoBehaviour::kOversizedLimits;
  endpoint().SetPlan(plan);

  const ProtocolSupportEnvelope support =
      client().QueryProtocolSupport(broker()->tab_id());
  EXPECT_LT(support.effective_limits.max_nodes, support.endpoint_limits.max_nodes)
      << "The effective limits equal the endpoint's, so the endpoint raised "
         "the ceiling by asking. The clamp is the whole reason two limit sets "
         "are reported rather than one.";
  EXPECT_LE(support.effective_limits.max_total_bytes,
            support.endpoint_limits.max_total_bytes);
}

// **Property: a secret a renderer volunteers is still a secret.** The redaction
// layers are in the renderer, so a compromised one can skip them; the browser's
// own scrubbing is what makes that a bounded failure rather than a disclosure.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, ASecretInAReplyDoesNotReachAnySink) {
  const std::vector<std::string> tokens = CorpusManifest::Get().AllCanaryTokens();
  ASSERT_FALSE(tokens.empty());

  RendererReplyPlan plan;
  plan.snapshot = SnapshotBehaviour::kSecretInTextRun;
  plan.secret_to_emit = tokens.front();
  ObserveWith(plan);

  CanaryLeakScanner scanner = CanaryLeakScanner::ForWholeCorpus();
  scanner.AddSink("projection from a hostile endpoint",
                  client().TranscriptForLeakScan());
  scanner.AddJournal(journal());
  scanner.AddAuditStream(audit_stream());
  EXPECT_TRUE(scanner.AssertAllSinksClean())
      << "A value a compromised renderer volunteered reached a sink. The "
         "renderer-side redaction is the first layer and this is what happens "
         "when it is not there.";
}

// **Property: a resolved node that is not the node that was authorized cannot
// carry an action.** The browser re-reads the node's role and available actions
// before it allows anything, and a renderer that swapped one for another is the
// most direct way to move an action onto a different element.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, ASwappedResolvedNodeRefusesTheAction) {
  RendererReplyPlan plan;
  plan.action = ActionBehaviour::kSwapsResolvedNode;
  endpoint().SetPlan(plan);

  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = MainFrameId();
  handle.page_epoch = observed.page_epoch;
  handle.graph_revision = observed.graph_revision;
  handle.node_id = SemanticNodeId{"node-0"};
  handle.expected_origin = MainFrameOrigin();

  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  // See the note on ClaimedDispatchNeverReachesVerified. Without the named
  // origin and the registered grant the envelope is refused at the admission
  // gate, and the re-read this case is about never happens.
  navigated.allowed_origins.push_back(MainFrameOrigin());
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, observed.graph_revision, {navigated});
  ASSERT_TRUE(RegisterActionGrant(&envelope));
  const ActionResult result = client().Act(std::move(envelope));

  EXPECT_NE(ActionResultCode::kVerified, result.result_code)
      << "An activate authorized against a link was allowed after the renderer "
         "re-read the target as a text field. The re-read exists precisely so "
         "that a target which changed role is caught before dispatch.";
  // Not kDeniedByPolicy. That is the admission gate's answer, and it arrives
  // before a renderer command exists — so it would satisfy the assertion above
  // while proving nothing about the renderer's behaviour. This line is what
  // keeps the case from passing vacuously if the envelope ever stops being
  // admissible again.
  EXPECT_NE(ActionResultCode::kDeniedByPolicy, result.result_code);
}

// **Property: a delta that skips a sequence number kills the projection.** A
// gap says changes were lost, so the revision the delta claims to start from is
// not evidence of anything, and only a fresh snapshot is a legal way back.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, ASequenceGapKillsTheProjection) {
  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  RendererReplyPlan plan;
  plan.delta = DeltaBehaviour::kSequenceGap;
  endpoint().SetPlan(plan);

  const SubscriptionEnvelope subscription = client().Subscribe(
      builder().Subscription(MainFrameId(), observed.page_epoch));
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);

  endpoint().PumpDeltas();
  ASSERT_TRUE(client().WaitForInvalidationCount(1))
      << "A delta with a skipped sequence number was applied rather than "
         "invalidating the projection.";

  bool saw_gap = false;
  for (const InvalidationNotice& notice : client().invalidations()) {
    if (notice.reason == InvalidationCode::kSequenceGap) {
      saw_gap = true;
      EXPECT_TRUE(notice.resnapshot_required);
    }
  }
  EXPECT_TRUE(saw_gap)
      << "The projection was invalidated without naming the gap, so a "
         "subscriber cannot tell a lost change from a dead document.";
}

// **Property: a secret in a delta is a secret.** The delta path has the same
// redaction obligation as the snapshot path, and it is a separate code path, so
// it needs its own proof.
IN_PROC_BROWSER_TEST_F(MaliciousRendererTest, ASecretInADeltaDoesNotReachAnySink) {
  const std::vector<std::string> tokens = CorpusManifest::Get().AllCanaryTokens();
  ASSERT_FALSE(tokens.empty());

  const ObservationEnvelope observed = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  RendererReplyPlan plan;
  plan.delta = DeltaBehaviour::kSecretInDelta;
  plan.secret_to_emit = tokens.back();
  endpoint().SetPlan(plan);

  const SubscriptionEnvelope subscription = client().Subscribe(
      builder().Subscription(MainFrameId(), observed.page_epoch));
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);
  endpoint().PumpDeltas();

  CanaryLeakScanner scanner = CanaryLeakScanner::ForWholeCorpus();
  scanner.AddSink("delta stream from a hostile endpoint",
                  client().TranscriptForLeakScan());
  scanner.AddAuditStream(audit_stream());
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

}  // namespace
}  // namespace taffy::test
