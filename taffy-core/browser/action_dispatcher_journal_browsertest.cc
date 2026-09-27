// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/shell/browser/shell.h"
#include "crypto/sha2.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "taffy/components/intelligence/content/action_digest.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"
#include "taffy/components/security/browser/action_authority.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

// Browser-owned navigation provides a real side effect without depending on a
// renderer endpoint. Holding the journal callback therefore proves the
// physical asynchronous commit, rather than a test counter, gates dispatch.

namespace taffy {
namespace {

class DelayedJournal final : public TaskJournalSink {
 public:
  void RecordDispatching(
      DispatchIntentRecord record,
      std::unique_ptr<TaskJournalAppendCallback> callback) override {
    CHECK(!pending_record);
    CHECK(!pending_callback);
    pending_record = std::move(record);
    pending_callback = std::move(callback);
  }

  void RecordTerminalResult(
      ActionResult result,
      std::unique_ptr<TaskJournalAppendCallback> callback) override {
    terminal_results.push_back(std::move(result));
    callback->Run(true);
  }

  void Complete(bool committed) {
    CHECK(pending_record);
    CHECK(pending_callback);
    if (committed) {
      intents.push_back(std::move(*pending_record));
    }
    pending_record.reset();
    pending_callback->Run(committed);
  }

  std::vector<DispatchIntentRecord> intents;
  std::vector<ActionResult> terminal_results;
  std::optional<DispatchIntentRecord> pending_record;
  std::unique_ptr<TaskJournalAppendCallback> pending_callback;
};

class NullObservability final : public ObservabilitySink {
 public:
  void RecordObservation(const ObservationRecord&) override {}
  void RecordAction(const ActionRecord&) override {}
  void RecordSubscription(const SubscriptionRecord&) override {}
};

class ActionDispatcherJournalBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    PageIntelligenceBroker::CreateForWebContents(web_contents());
    broker_ = PageIntelligenceBroker::FromWebContents(web_contents());
    leases_.BeginGeneration("profile_test", 1u);
    capabilities_.BeginGeneration("profile_test", 1u);
    observability_.SetSink(&observability_sink_);
    dispatcher_ = std::make_unique<ActionDispatcher>(
        web_contents(), broker_->GetWeakPtr(), &leases_, &capabilities_,
        &journal_, &observability_);
  }

  void TearDownOnMainThread() override {
    dispatcher_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  AuthorizedBrowserCommand MakeNavigateCommand(const GURL& destination) {
    ActorLeaseRequest lease_request;
    lease_request.task_id = TaskId{"task_nav"};
    lease_request.tab_id = broker_->tab_id();
    lease_request.mutating = true;
    lease_request.requested_duration_ms = 5000;
    const ActorLeaseResult lease =
        leases_.Issue(lease_request, base::TimeTicks::Now());
    CHECK_EQ(ActorLeaseResultCode::kIssued, lease.code);

    AuthorizedBrowserCommand command;
    command.schema_version = "0.9";
    command.dispatch_id = DispatchId{"disp_nav"};
    command.action_id = ActionId{"act_nav"};
    command.idempotency_key = "dispatch-nav";
    command.task_id = lease_request.task_id;
    command.command_type = BrowserCommandType::kNavigate;
    command.tab_id = broker_->tab_id();
    command.argument = destination.spec();
    command.idempotency_policy = IdempotencyPolicy::kConditionallyIdempotent;
    command.absolute_deadline_monotonic_ms =
        static_cast<MonotonicMillis>(
            (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds()) +
        30000;

    Origin destination_origin;
    destination_origin.kind = OriginKind::kTuple;
    destination_origin.serialization =
        url::Origin::Create(destination).Serialize();
    Postcondition committed;
    committed.kind = PostconditionKind::kCommittedNavigation;
    committed.allowed_origins.push_back(destination_origin);
    committed.timeout_ms = 2000;
    command.expected_postconditions.push_back(std::move(committed));

    command.capability.capability_reference = CapabilityReference{"cap_nav"};
    command.capability.actor_lease_id = lease.lease_id;
    command.capability.policy_version = 1u;
    command.capability.expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    command.capability_reference = command.capability.capability_reference;
    command.principal.kind = PrincipalKind::kAssistant;
    command.action_digest = ComputeBrowserCommandDigest(command);
    command.canonical_intent_digest =
        crypto::SHA256Hash(base::as_byte_span(command.action_digest.value));

    FrameObservationEndpoint* endpoint =
        broker_->GetOrCreateEndpoint(web_contents()->GetPrimaryMainFrame());
    CHECK(endpoint);
    auto grant = core_service::mojom::MintedCapabilityGrant::New();
    grant->capability_id = command.capability.capability_reference.value;
    grant->service_generation = 1u;
    grant->policy_version = command.capability.policy_version;
    grant->actor_lease_id = lease.lease_id.value;
    grant->task_id = command.task_id.value;
    grant->action_id = command.action_id.value;
    grant->action_class = core_service::mojom::PolicyActionClass::kOpenLink;
    grant->operation_kind =
        core_service::mojom::TaskActionOperationKind::kNavigate;
    grant->canonical_intent_digest.assign(
        command.canonical_intent_digest.begin(),
        command.canonical_intent_digest.end());
    grant->principal = core_service::mojom::PolicyPrincipal::New();
    grant->principal->kind =
        core_service::mojom::PolicyPrincipalKind::kAssistant;
    grant->proposal_digest = command.action_digest.value;
    grant->idempotency_key = "dispatch-nav";
    grant->scope = core_service::mojom::PolicyCapabilityScope::New();
    grant->scope->profile_id = "profile_test";
    grant->scope->tab_id = command.tab_id.value;
    grant->scope->frame_id = endpoint->frame_id().value;
    grant->scope->page_epoch = endpoint->page_epoch().value;
    grant->scope->origin = core_service::mojom::PolicyOrigin::New();
    grant->scope->origin->kind = core_service::mojom::PolicyOriginKind::kTuple;
    grant->scope->origin->serialization = endpoint->origin().serialization;
    grant->scope->required_graph_revision = endpoint->last_reported_revision();
    grant->scope->destination_scope = core_service::mojom::PolicyOrigin::New();
    grant->scope->destination_scope->kind =
        core_service::mojom::PolicyOriginKind::kTuple;
    grant->scope->destination_scope->serialization =
        destination_origin.serialization;
    grant->scope->destination_address = destination.spec();
    grant->effective_risk =
        core_service::mojom::PolicyRiskClass::kReversibleDisclosure;
    // Required by CapabilityLedger::Register, and for a task it must name the
    // grant's own task. Without it every registration answered kInvalidGrant,
    // and the CHECK below aborted the process before the test began.
    grant->authority_subject = core_service::mojom::AuthoritySubject::New();
    grant->authority_subject->kind =
        core_service::mojom::AuthoritySubjectKind::kTask;
    grant->authority_subject->authority_subject_id = grant->task_id;
    grant->issued_at_monotonic_ms = static_cast<uint64_t>(
        (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds());
    grant->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
    CHECK_EQ(core_service::mojom::CapabilityRegistrationStatus::kRegistered,
             capabilities_.Register(*grant, leases_, base::TimeTicks::Now()));
    return command;
  }

  std::unique_ptr<PageIntelligenceServiceImpl> MakeService() {
    return std::make_unique<PageIntelligenceServiceImpl>(
        web_contents(), broker_, nullptr, &journal_, &observability_sink_,
        &leases_, &capabilities_);
  }

  raw_ptr<PageIntelligenceBroker> broker_ = nullptr;
  ActorLeaseRegistry leases_;
  CapabilityLedger capabilities_;
  DelayedJournal journal_;
  NullObservability observability_sink_;
  ObservabilityRecorder observability_;
  std::unique_ptr<ActionDispatcher> dispatcher_;
};

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       CommitPrecedesNavigation) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  base::RunLoop completion;
  std::optional<ActionResult> result;
  dispatcher_->DispatchBrowserCommand(
      RequestId{"req_nav"}, MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult terminal) {
        result = std::move(terminal);
        completion.Quit();
      }));

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(source, web_contents()->GetLastCommittedURL());
  EXPECT_FALSE(result);
  journal_.Complete(true);
  completion.Run();

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->dispatched);
  EXPECT_EQ(destination, web_contents()->GetLastCommittedURL());
  EXPECT_EQ(1u, journal_.intents.size());
  EXPECT_EQ(1u, journal_.terminal_results.size());
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       FailedAppendHasNoSideEffect) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  base::RunLoop completion;
  std::optional<ActionResult> result;
  dispatcher_->DispatchBrowserCommand(
      RequestId{"req_nav"}, MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult terminal) {
        result = std::move(terminal);
        completion.Quit();
      }));
  journal_.Complete(false);
  completion.Run();

  ASSERT_TRUE(result);
  EXPECT_EQ(ActionResultCode::kDispatchFailed, result->result_code);
  EXPECT_FALSE(result->dispatched);
  EXPECT_EQ(source, web_contents()->GetLastCommittedURL());
  EXPECT_TRUE(journal_.intents.empty());
  EXPECT_TRUE(journal_.terminal_results.empty());
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       CancellationWinsLateCommitExactlyOnce) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  int completions = 0;
  const RequestId request_id{"req_nav"};
  dispatcher_->DispatchBrowserCommand(
      request_id, MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult result) {
        ++completions;
        EXPECT_EQ(ActionResultCode::kCancelledByUser, result.result_code);
      }));
  dispatcher_->Cancel(request_id);
  EXPECT_EQ(1, completions);
  journal_.Complete(true);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, completions);
  EXPECT_EQ(source, web_contents()->GetLastCommittedURL());
  EXPECT_EQ(1u, journal_.intents.size());
  EXPECT_TRUE(journal_.terminal_results.empty());
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       TaskRevocationWinsLateCommitBeforePhysicalDispatch) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  base::RunLoop completion;
  std::optional<ActionResult> result;
  dispatcher_->DispatchBrowserCommand(
      RequestId{"req_nav"}, MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult terminal) {
        result = std::move(terminal);
        completion.Quit();
      }));
  ASSERT_TRUE(journal_.pending_record);
  EXPECT_EQ(TaskId{"task_nav"}, journal_.pending_record->task_id);

  // Mirrors CoreServiceManager's task-stop revocation while the profile's
  // sequenced writer still owns the dispatch-intent callback.
  leases_.RevokeTask("task_nav", 1u);
  capabilities_.RevokeTask("task_nav", 1u);
  journal_.Complete(true);
  completion.Run();

  ASSERT_TRUE(result);
  EXPECT_EQ(ActionResultCode::kCancelledByUser, result->result_code);
  EXPECT_FALSE(result->dispatched);
  EXPECT_EQ(source, web_contents()->GetLastCommittedURL());
  ASSERT_EQ(1u, journal_.intents.size());
  ASSERT_EQ(1u, journal_.terminal_results.size());
  EXPECT_EQ(ActionResultCode::kCancelledByUser,
            journal_.terminal_results.front().result_code);
  EXPECT_FALSE(journal_.terminal_results.front().dispatched);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       DestructionWinsLateCommitExactlyOnce) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  int completions = 0;
  dispatcher_->DispatchBrowserCommand(
      RequestId{"req_nav"}, MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult result) {
        ++completions;
        EXPECT_EQ(ActionResultCode::kCancelledByUser, result.result_code);
      }));
  dispatcher_.reset();
  EXPECT_EQ(1, completions);
  journal_.Complete(true);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, completions);
  EXPECT_EQ(source, web_contents()->GetLastCommittedURL());
  EXPECT_EQ(1u, journal_.intents.size());
  EXPECT_TRUE(journal_.terminal_results.empty());
}

// The service, unlike a standalone dispatcher, observes the broker's actual
// page retirement and revokes the old actor leases on every navigation.
IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       PageRetirementPreservesDispatchedNavigationVerification) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination = embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  auto service = MakeService();
  base::RunLoop completion;
  std::optional<ActionResult> result;
  service->SubmitTaskBrowserCommand(
      MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult terminal) {
        result = std::move(terminal);
        completion.Quit();
      }));
  ASSERT_TRUE(journal_.pending_record);
  journal_.Complete(true);
  completion.Run();
  ASSERT_TRUE(result);
  EXPECT_EQ(destination, web_contents()->GetLastCommittedURL());
  EXPECT_TRUE(result->dispatched);
  EXPECT_EQ(ActionResultCode::kVerified, result->result_code);
  ASSERT_EQ(1u, journal_.terminal_results.size());
  EXPECT_EQ(ActionResultCode::kVerified,
            journal_.terminal_results.front().result_code);
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       PageRetirementCancelsNavigationStillWaitingForJournal) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination = embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  auto service = MakeService();
  int completions = 0;
  service->SubmitTaskBrowserCommand(
      MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult terminal) {
        ++completions;
        EXPECT_EQ(ActionResultCode::kCancelledByNavigation, terminal.result_code);
        EXPECT_FALSE(terminal.dispatched);
      }));
  ASSERT_TRUE(journal_.pending_record);
  const GURL replacement = embedded_test_server()->GetURL("a.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), replacement));
  EXPECT_EQ(1, completions);
  journal_.Complete(true);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(replacement, web_contents()->GetLastCommittedURL());
  EXPECT_EQ(1, completions);
  EXPECT_TRUE(journal_.terminal_results.empty());
}

IN_PROC_BROWSER_TEST_F(ActionDispatcherJournalBrowserTest,
                       UserPreemptionStillCancelsDispatchedNavigationVerification) {
  const GURL source = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL destination = embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), source));
  auto service = MakeService();
  int completions = 0;
  service->SubmitTaskBrowserCommand(
      MakeNavigateCommand(destination),
      base::BindLambdaForTesting([&](ActionResult terminal) {
        ++completions;
        EXPECT_EQ(ActionResultCode::kCancelledByUser, terminal.result_code);
        EXPECT_TRUE(terminal.dispatched);
      }));
  ASSERT_TRUE(journal_.pending_record);
  journal_.Complete(true);
  service->OnUserPreemption(broker_->tab_id());
  EXPECT_EQ(1, completions);
  EXPECT_TRUE(content::WaitForLoadStop(web_contents()));
  EXPECT_EQ(1, completions);
  ASSERT_EQ(1u, journal_.terminal_results.size());
  EXPECT_EQ(ActionResultCode::kCancelledByUser,
            journal_.terminal_results.front().result_code);
}

}  // namespace
}  // namespace taffy
