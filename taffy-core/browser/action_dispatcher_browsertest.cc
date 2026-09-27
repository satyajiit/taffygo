// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_dispatcher.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "crypto/sha2.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "taffy/components/intelligence/content/action_digest.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/components/security/browser/action_authority.h"
#include "testing/gtest/include/gtest/gtest.h"

// The dispatch path's security properties, exercised against a real
// WebContents. These cannot run on the planning workstation.
//
// Node-targeted happy paths need a renderer endpoint that answers ResolveNode.

namespace taffy {
namespace {

class RecordingJournal : public TaskJournalSink {
 public:
  void RecordDispatching(
      DispatchIntentRecord record,
      std::unique_ptr<TaskJournalAppendCallback> callback) override {
    if (accept_appends) {
      intents.push_back(std::move(record));
    }
    callback->Run(accept_appends);
  }
  void RecordTerminalResult(
      ActionResult result,
      std::unique_ptr<TaskJournalAppendCallback> callback) override {
    terminal_results.push_back(std::move(result));
    callback->Run(true);
  }

  std::vector<DispatchIntentRecord> intents;
  std::vector<ActionResult> terminal_results;
  bool accept_appends = true;
};

class RecordingObservability : public ObservabilitySink {
 public:
  void RecordObservation(const ObservationRecord&) override {}
  void RecordAction(const ActionRecord& record) override {
    actions.push_back(record);
  }
  void RecordSubscription(const SubscriptionRecord&) override {}
  std::vector<ActionRecord> actions;
};

class ActionDispatcherBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());

    PageIntelligenceBroker::CreateForWebContents(web_contents());
    broker_ = PageIntelligenceBroker::FromWebContents(web_contents());
    leases_.BeginGeneration("profile_test", 1u);
    capabilities_.BeginGeneration("profile_test", 1u);
    dispatcher_ = std::make_unique<ActionDispatcher>(
        web_contents(), broker_->GetWeakPtr(), &leases_, &capabilities_,
        &journal_, &observability_);
    observability_.SetSink(&observability_sink_);
  }

  void TearDownOnMainThread() override {
    dispatcher_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  // Runs one dispatch to completion and returns its single terminal result.
  ActionResult DispatchAndWait(AuthorizedActionEnvelope envelope) {
    base::RunLoop run_loop;
    std::optional<ActionResult> captured;
    dispatcher_->Dispatch(RequestId{"req_1"}, std::move(envelope),
                          base::BindLambdaForTesting([&](ActionResult result) {
                            captured = std::move(result);
                            run_loop.Quit();
                          }));
    run_loop.Run();
    return std::move(*captured);
  }

  // The capability's lifetime is a parameter because the ledger reads the
  // expiry from the grant it registered, never from the envelope handed back,
  // so a test that wants an expired capability has to register one.
  AuthorizedActionEnvelope MakeLiveEnvelope(
      base::TimeDelta capability_lifetime = base::Milliseconds(4000)) {
    FrameObservationEndpoint* endpoint =
        broker_->GetOrCreateEndpoint(web_contents()->GetPrimaryMainFrame());
    CHECK(endpoint);

    ActorLeaseRequest lease_request;
    lease_request.task_id = TaskId{"task_1"};
    lease_request.tab_id = broker_->tab_id();
    lease_request.mutating = true;
    lease_request.requested_duration_ms = 5000;
    const ActorLeaseResult lease =
        leases_.Issue(lease_request, base::TimeTicks::Now());

    AuthorizedActionEnvelope envelope;
    envelope.dispatch_id = DispatchId{"disp_1"};
    envelope.action_id = ActionId{"act_1"};
    envelope.task_id = TaskId{"task_1"};
    envelope.idempotency_key = "dispatch_idempotency_1";
    envelope.action_type = ActionType::kActivate;
    envelope.target_handle.tab_id = broker_->tab_id();
    envelope.target_handle.frame_id = endpoint->frame_id();
    envelope.target_handle.page_epoch = endpoint->page_epoch();
    envelope.target_handle.graph_revision = endpoint->last_reported_revision();
    envelope.target_handle.node_id = SemanticNodeId{"n_link"};
    envelope.target_handle.expected_origin = endpoint->origin();
    envelope.required_graph_revision = endpoint->last_reported_revision();
    envelope.idempotency_policy = IdempotencyPolicy::kIdempotentWrite;
    envelope.absolute_deadline_monotonic_ms =
        static_cast<MonotonicMillis>(
            (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds()) +
        30000;

    Postcondition committed;
    committed.kind = PostconditionKind::kCommittedNavigation;
    // A postcondition has to carry an observable claim or the action cannot be
    // verified, and the dispatcher refuses an unverifiable action before a
    // capability is consumed (protocol section 11.6). Naming the origin the
    // navigation is allowed to land on is that claim.
    committed.allowed_origins.push_back(endpoint->origin());
    committed.timeout_ms = 2000;
    envelope.expected_postconditions.push_back(committed);

    envelope.capability.capability_reference = CapabilityReference{"cap_1"};
    envelope.capability.actor_lease_id = lease.lease_id;
    envelope.capability.expires_at_monotonic_ms =
        static_cast<MonotonicMillis>(
            (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds()) +
        static_cast<MonotonicMillis>(capability_lifetime.InMilliseconds());
    envelope.capability.policy_version = 1u;
    envelope.action_digest = ComputeActionDigest(envelope);
    envelope.canonical_intent_digest = crypto::SHA256Hash(
        base::as_byte_span(envelope.action_digest.value));

    auto grant = core_service::mojom::MintedCapabilityGrant::New();
    grant->capability_id = envelope.capability.capability_reference.value;
    grant->service_generation = 1u;
    grant->policy_version = envelope.capability.policy_version;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = envelope.task_id.value;
    grant->action_id = envelope.action_id.value;
    grant->action_class =
        core_service::mojom::PolicyActionClass::kSyntheticClick;
    grant->operation_kind =
        core_service::mojom::TaskActionOperationKind::kDomClick;
    grant->canonical_intent_digest.assign(
        envelope.canonical_intent_digest.begin(),
        envelope.canonical_intent_digest.end());
    grant->principal = core_service::mojom::PolicyPrincipal::New();
    grant->principal->kind =
        core_service::mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = envelope.action_digest.value;
    grant->idempotency_key = envelope.idempotency_key;
    grant->scope = core_service::mojom::PolicyCapabilityScope::New();
    grant->scope->profile_id = "profile_test";
    grant->scope->tab_id = broker_->tab_id().value;
    grant->scope->frame_id = endpoint->frame_id().value;
    grant->scope->page_epoch = endpoint->page_epoch().value;
    grant->scope->origin = core_service::mojom::PolicyOrigin::New();
    grant->scope->origin->kind = core_service::mojom::PolicyOriginKind::kTuple;
    grant->scope->origin->serialization = endpoint->origin().serialization;
    grant->scope->required_graph_revision = endpoint->last_reported_revision();
    grant->effective_risk =
        core_service::mojom::PolicyRiskClass::kReversibleDisclosure;
    // CapabilityLedger::Register refuses a grant with no authority subject, and
    // for a task it must name the same task the grant carries. Leaving it unset
    // made every registration here kInvalidGrant, which is a CHECK, so each of
    // these tests aborted its browser process before reaching its own subject.
    grant->authority_subject = core_service::mojom::AuthoritySubject::New();
    grant->authority_subject->kind =
        core_service::mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = grant->task_id;
    grant->issued_at_monotonic_ms = static_cast<uint64_t>(
        (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds());
    grant->expires_at_monotonic_ms =
        envelope.capability.expires_at_monotonic_ms;
    CHECK_EQ(core_service::mojom::CapabilityRegistrationStatus::kRegistered,
             capabilities_.Register(*grant, leases_, base::TimeTicks::Now()));
    return envelope;
  }

  raw_ptr<PageIntelligenceBroker> broker_ = nullptr;
  ActorLeaseRegistry leases_;
  CapabilityLedger capabilities_;
  RecordingJournal journal_;
  RecordingObservability observability_sink_;
  ObservabilityRecorder observability_;
  std::unique_ptr<ActionDispatcher> dispatcher_;
};

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       SubmitWithoutItsExplicitNoneInputSpendsNothing) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  envelope.action_type = ActionType::kSubmitForm;
  envelope.action_digest = ComputeActionDigest(envelope);

  const ActionResult result = DispatchAndWait(std::move(envelope));

  EXPECT_EQ(ActionResultCode::kDeniedByPolicy, result.result_code);
  EXPECT_FALSE(result.dispatched);
  // Submit has an exact input shape too: explicitly none. An absent input is
  // refused before anything is spent, with no journal entry and nothing for a
  // renderer to have seen.
  EXPECT_FALSE(capabilities_.IsSpent(CapabilityReference{"cap_1"}));
  EXPECT_TRUE(journal_.intents.empty());
  EXPECT_FALSE(result.repeat_may_duplicate_effect);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       AWriteThatNamesAndCarriesIsDeniedAndValueSurvives) {
  // A model-authored literal cannot ride beside a legitimate browser-owned
  // reference. Shape rejection stays before capability admission and before
  // the resolution that would spend the person's value.
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  ValueReferenceVault vault;
  vault.BeginGeneration("profile_test", 1u);
  const std::optional<FillClearance> clearance =
      FillClearance::For(Sensitivity::kIdentity);
  ASSERT_TRUE(clearance.has_value());
  const ValueReference reference =
      vault.Mint(TaskId{"task_dispatch"}, *clearance, "1234 5678 9012",
                 base::TimeTicks::Now() + base::Seconds(30));
  ASSERT_TRUE(reference.is_valid());
  dispatcher_->SetValueReferenceVault(&vault);

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  envelope.action_type = ActionType::kSetText;
  ActionInput input;
  input.kind = ActionInputKind::kText;
  input.sensitivity = Sensitivity::kIdentity;
  input.value_reference = reference;
  input.text = "model-authored bytes";
  envelope.input = std::move(input);
  ASSERT_FALSE(IsNamedValueInputShape(*envelope.input));
  envelope.action_digest = ComputeActionDigest(envelope);

  const ActionResult result = DispatchAndWait(std::move(envelope));

  EXPECT_EQ(ActionResultCode::kDeniedByPolicy, result.result_code);
  EXPECT_FALSE(result.dispatched);
  EXPECT_FALSE(capabilities_.IsSpent(CapabilityReference{"cap_1"}));
  EXPECT_TRUE(journal_.intents.empty());
  // Nothing reached the vault. A refusal upstream of the resolution must not
  // cost the person the value they typed.
  EXPECT_TRUE(vault.Holds(reference));
  EXPECT_EQ(1u, vault.HeldCount());

  dispatcher_->SetValueReferenceVault(nullptr);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       StaleEpochFailsClosedBeforeTheCapabilityIsSpent) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();

  // The page moves on between authorization and dispatch. This is the race the
  // whole stale-node algorithm exists for.
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.test", "/title2.html")));

  const ActionResult result = DispatchAndWait(std::move(envelope));

  EXPECT_TRUE(result.result_code == ActionResultCode::kStalePageEpoch ||
              result.result_code == ActionResultCode::kOriginChanged ||
              result.result_code == ActionResultCode::kFrameGone);
  EXPECT_FALSE(result.dispatched);
  EXPECT_TRUE(journal_.intents.empty())
      << "nothing may be journalled for an action that never happened";
  EXPECT_FALSE(capabilities_.IsSpent(CapabilityReference{"cap_1"}))
      << "steps 1-3 refuse before step 4 consumes the capability";
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       EditedEnvelopeIsRefusedByTheDigestCheck) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  // Authorized for one node; dispatched against another. The digest was
  // computed before the edit, which is exactly the attack it defends against.
  envelope.target_handle.node_id = SemanticNodeId{"n_somewhere_else"};

  const ActionResult result = DispatchAndWait(std::move(envelope));

  EXPECT_EQ(ActionResultCode::kDeniedByPolicy, result.result_code);
  EXPECT_TRUE(journal_.intents.empty());
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       EnvelopeWithNoDeclaredEffectIsRefused) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  envelope.expected_postconditions.clear();
  envelope.action_digest = ComputeActionDigest(envelope);

  // An unverifiable action is not an authorized action (protocol section 11.6).
  EXPECT_EQ(ActionResultCode::kDeniedByPolicy,
            DispatchAndWait(std::move(envelope)).result_code);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       EnvelopeWhoseEffectCarriesNoClaimIsRefused) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  // "It will navigate somewhere" is not a claim: any commit at all would
  // satisfy it, including one to an origin the capability never allowed. An
  // action nobody could check is refused rather than left to time out, because
  // a timeout reads as "the effect may still be pending".
  envelope.expected_postconditions.front().allowed_origins.clear();
  envelope.expected_postconditions.front().expected_destination.reset();
  envelope.action_digest = ComputeActionDigest(envelope);

  const ActionResult result = DispatchAndWait(std::move(envelope));
  EXPECT_EQ(ActionResultCode::kDeniedByPolicy, result.result_code);
  EXPECT_FALSE(result.dispatched);
  EXPECT_TRUE(journal_.intents.empty());
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       ExpiredCapabilityIsRefused) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  // Shortening only the envelope's copy does not expire anything: the ledger
  // compares it against the registered grant and answers kActorLeaseMissing,
  // because the two disagree. Register a capability that genuinely runs out,
  // and wait past the instant it does. CapabilityLedger::Admit reads the clock
  // itself and prunes nothing, so the expired record is still there to refuse.
  AuthorizedActionEnvelope envelope =
      MakeLiveEnvelope(/*capability_lifetime=*/base::Milliseconds(300));
  base::RunLoop expiry;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, expiry.QuitClosure(), base::Milliseconds(600));
  expiry.Run();

  EXPECT_EQ(ActionResultCode::kCapabilityExpired,
            DispatchAndWait(std::move(envelope)).result_code);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       PreemptedLeaseRefusesTheAction) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  // The user touched the tab. Take over revokes undispatched mutation
  // authority (protocol section 17.4).
  leases_.PreemptTab(broker_->tab_id());

  EXPECT_EQ(ActionResultCode::kActorLeaseMissing,
            DispatchAndWait(std::move(envelope)).result_code);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherBrowserTest,
                       EveryTerminalResultIsRecordedContentFree) {
  ASSERT_TRUE(content::NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  AuthorizedActionEnvelope envelope = MakeLiveEnvelope();
  envelope.action_type = ActionType::kSubmitForm;
  envelope.action_digest = ComputeActionDigest(envelope);
  DispatchAndWait(std::move(envelope));

  // Even a refusal produces exactly one observability record, and the record
  // type cannot hold page content by construction (observability_recorder.h).
  ASSERT_EQ(1u, observability_sink_.actions.size());
  EXPECT_EQ(ActionResultCode::kDeniedByPolicy,
            observability_sink_.actions[0].code);
}

}  // namespace
}  // namespace taffy
