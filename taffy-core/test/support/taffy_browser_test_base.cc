// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/taffy_browser_test_base.h"

#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "crypto/sha2.h"
#include "net/dns/mock_host_resolver.h"
#include "taffy/components/intelligence/content/action_digest.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "url/origin.h"

namespace taffy::test {

namespace {

// One task, one tab, for the whole test. A suite that needs two tasks makes a
// second builder with its own identifier prefix.
constexpr char kIdentifierPrefix[] = "suite";

Origin ToContractOrigin(const url::Origin& origin) {
  Origin out;
  if (origin.opaque()) {
    out.kind = OriginKind::kOpaque;
    // An opaque origin's identity is session local. The test never needs to
    // reconstruct the browser's own opaque identifier, only to know that the
    // origin was opaque, so the serialization is left empty on purpose: two
    // opaque origins are equal only when their opaque identifiers are, and
    // comparing serializations would silently make every one of them equal.
    out.opaque_id = "test-opaque";
    return out;
  }
  out.kind = OriginKind::kTuple;
  out.serialization = origin.Serialize();
  return out;
}

bool PolicyShapeForAction(
    ActionType action,
    core_service::mojom::PolicyActionClass* action_class,
    core_service::mojom::TaskActionOperationKind* operation) {
  using ActionClass = core_service::mojom::PolicyActionClass;
  using Operation = core_service::mojom::TaskActionOperationKind;
  switch (action) {
    case ActionType::kActivate:
      *action_class = ActionClass::kSyntheticClick;
      *operation = Operation::kDomClick;
      return true;
    case ActionType::kScrollIntoView:
      *action_class = ActionClass::kScrollIntoView;
      *operation = Operation::kDomScroll;
      return true;
    case ActionType::kSetText:
      *action_class = ActionClass::kFillField;
      *operation = Operation::kFormFill;
      return true;
    case ActionType::kSelectOption:
      *action_class = ActionClass::kSelectOption;
      *operation = Operation::kFormSelect;
      return true;
    case ActionType::kToggle:
      *action_class = ActionClass::kToggleControl;
      *operation = Operation::kFormToggle;
      return true;
    case ActionType::kSubmitForm:
      *action_class = ActionClass::kSubmitForm;
      *operation = Operation::kFormSubmit;
      return true;
    case ActionType::kFocus:
      // Focus has no task operation in the current closed roster. Returning
      // false is more honest than registering it under a neighbouring class.
      return false;
  }
  return false;
}

uint64_t MonotonicMilliseconds(base::TimeTicks value) {
  const int64_t milliseconds = (value - base::TimeTicks()).InMilliseconds();
  return milliseconds < 0 ? 0u : static_cast<uint64_t>(milliseconds);
}

}  // namespace

TaffyBrowserTestBase::TaffyBrowserTestBase(
    FixtureOriginMap::Scheme fixture_scheme)
    : origins_(fixture_scheme),
      ids_(kIdentifierPrefix),
      client_("scripted core service") {}

TaffyBrowserTestBase::~TaffyBrowserTestBase() = default;

void TaffyBrowserTestBase::SetUpOnMainThread() {
  content::ContentBrowserTest::SetUpOnMainThread();

  // A suite's own rules go in first, because the wildcard below matches every
  // name and RuleBasedHostResolverProc answers with the first rule that
  // matches. See AddHostResolverRules() for the other half of the reason this
  // is a hook rather than something a suite does for itself.
  AddHostResolverRules();

  // Every corpus hostname resolves to the loopback interface. Without this the
  // cross-origin cases fail as name-resolution errors, which look nothing like
  // the property they were meant to prove.
  host_resolver()->AddRule("*", "127.0.0.1");
  origins_.Start();

  PageIntelligenceBroker::CreateForWebContents(web_contents());
  broker_ = PageIntelligenceBroker::FromWebContents(web_contents());
  ASSERT_TRUE(broker_) << "The broker did not attach to the web contents.";

  actor_leases_.BeginGeneration("test-profile", 1u);
  capabilities_.BeginGeneration("test-profile", 1u);
  service_ = std::make_unique<PageIntelligenceServiceImpl>(
      web_contents(), broker_, &client_, &journal_, &audit_stream_,
      &actor_leases_, &capabilities_);

  // The shipping encoder, because the shipping browser installs it
  // (taffy_page_intelligence_host.cc). Leaving the default in place meant every
  // suite in this binary observed with GraphPayloadEncoding::kNone while the
  // product observed with kBipContract — so no test in the tree ever saw a
  // node identifier, every action test had to fabricate one, and the success
  // path of the dispatcher had no coverage at all. A test base that differs
  // from the product in what an observation *contains* is testing a browser
  // nobody ships.
  service_->SetGraphPayloadEncoder(MakeBipGraphPayloadEncoder());

  client_.Attach(service_.get());

  StartTask();
}

void TaffyBrowserTestBase::StartTask() {
  const TaskId task_id = ids_.NextTaskId();
  ActorLeaseRequest lease_request;
  lease_request.task_id = task_id;
  lease_request.tab_id = broker_->tab_id();
  lease_request.requested_duration_ms = 60000;
  lease_request.mutating = true;
  const ActorLeaseResult lease = service_->IssueActorLease(lease_request);
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code)
      << "No actor lease was issued, so every action in this test would be "
         "refused for a reason that has nothing to do with what it proves.";

  builder_ = std::make_unique<BipRequestBuilder>(
      &ids_, task_id, broker_->tab_id(), lease.lease_id);
  active_lease_expires_at_monotonic_ms_ = lease.expires_at_monotonic_ms;
}

void TaffyBrowserTestBase::RenewLease() {
  ActorLeaseRequest lease_request;
  lease_request.task_id = builder_->task_id();
  lease_request.tab_id = broker_->tab_id();
  lease_request.requested_duration_ms = 60000;
  lease_request.mutating = true;
  const ActorLeaseResult lease = service_->IssueActorLease(lease_request);
  if (lease.code != ActorLeaseResultCode::kIssued) {
    return;
  }
  builder_ = std::make_unique<BipRequestBuilder>(
      &ids_, builder_->task_id(), broker_->tab_id(), lease.lease_id);
  active_lease_expires_at_monotonic_ms_ = lease.expires_at_monotonic_ms;
}

void TaffyBrowserTestBase::BeginNewTask() {
  ASSERT_TRUE(service_)
      << "BeginNewTask ran before the fixture built a service, so there is no "
         "registry to end a task in.";
  if (builder_) {
    // The task is over and its lease goes with it. Released rather than left
    // to expire, because at most one mutating lease exists per tab (domain
    // model section 12.3) and an abandoned one would refuse the next task's
    // with kAlreadyHeld — a refusal about lease bookkeeping, wearing the
    // costume of the property under test.
    service_->ReleaseActorLease(builder_->lease_id());
  }
  StartTask();
}

void TaffyBrowserTestBase::TearDownOnMainThread() {
  AssertNoExfiltration();
  if (!allow_unsettled_requests_) {
    EXPECT_TRUE(client_.AssertOneTerminalResultPerRequest());
  }
  AssertNoSeededSecretLeak();

  // Torn down in dependency order. The service holds a raw pointer to the
  // broker, which is web-contents-owned and outlives this call, so the service
  // goes first and the builder — which holds nothing but identifiers — last.
  builder_.reset();
  service_.reset();
  broker_ = nullptr;

  content::ContentBrowserTest::TearDownOnMainThread();
}

content::WebContents* TaffyBrowserTestBase::web_contents() {
  return shell()->web_contents();
}

bool TaffyBrowserTestBase::NavigateToFixture(std::string_view fixture_id) {
  const GURL url = FixtureUrl(fixture_id);
  if (!content::NavigateToURL(shell(), url)) {
    ADD_FAILURE() << "Navigation to corpus fixture " << fixture_id << " at "
                  << url << " did not commit at the expected URL.";
    return false;
  }
  return true;
}

FrameId TaffyBrowserTestBase::MainFrameId() {
  return broker_->GetOrAssignFrameId(web_contents()->GetPrimaryMainFrame());
}

Origin TaffyBrowserTestBase::MainFrameOrigin() {
  return ToContractOrigin(
      web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin());
}

void TaffyBrowserTestBase::GrantObservation(std::vector<Origin> allowed_origins,
                                            bool may_include_child_frames) {
  ObservationPolicyGrant grant = builder_->Grant(
      ObservationScope::kDocument, std::move(allowed_origins),
      may_include_child_frames);
  // The document in front of the browser at the moment the grant is made,
  // which is what a policy engine would have decided against. Reading it here
  // rather than taking it as an argument is what makes the grant behave like
  // the product's: a suite that grants and then navigates somewhere else finds
  // its grant refused, because that is the property
  // BfcacheAndRedirectTest.AGrantDoesNotFollowARedirect exists to hold.
  grant.document_origin = MainFrameOrigin();
  client_.SetObservationGrant(std::move(grant));
}

bool TaffyBrowserTestBase::RegisterActionGrant(
    AuthorizedActionEnvelope* envelope) {
  if (!envelope || !builder_ || envelope->task_id != builder_->task_id() ||
      envelope->target_handle.tab_id != broker_->tab_id() ||
      envelope->target_handle.expected_origin.kind != OriginKind::kTuple ||
      envelope->target_handle.expected_origin.serialization.empty()) {
    return false;
  }

  core_service::mojom::PolicyActionClass action_class;
  core_service::mojom::TaskActionOperationKind operation;
  if (!PolicyShapeForAction(envelope->action_type, &action_class, &operation)) {
    return false;
  }

  // The product releases an action's lease when that action finishes
  // (ActionDispatcher::FinishAction), so the lease a task starts with
  // authorizes one action, and a task takes a fresh one for the next. A test
  // that makes several does the same, under the same task. Only a released
  // lease is renewed here: one revoked by the person taking over was revoked
  // for a reason, and a test registering after that is asking about it.
  if (!actor_leases_.IsValidFor(builder_->lease_id(), broker_->tab_id(),
                                base::TimeTicks::Now()) &&
      !actor_leases_.WasRevokedByHandover(broker_->tab_id(),
                                          builder_->lease_id())) {
    RenewLease();
  }

  const uint64_t now_ms = MonotonicMilliseconds(base::TimeTicks::Now());
  if (active_lease_expires_at_monotonic_ms_ <= now_ms) {
    LOG(WARNING) << "[taffy_test_grant_refused] at=lease-expired";
    return false;
  }

  envelope->capability.actor_lease_id = builder_->lease_id();
  envelope->capability.policy_version = 1u;
  envelope->capability.expires_at_monotonic_ms =
      active_lease_expires_at_monotonic_ms_;
  const std::string canonical_intent = base::StrCat(
      {"browser-test:", envelope->task_id.value, ":", envelope->action_id.value,
       ":", base::NumberToString(static_cast<uint8_t>(envelope->action_type))});
  envelope->canonical_intent_digest =
      crypto::SHA256Hash(base::as_byte_span(canonical_intent));
  envelope->idempotency_key =
      base::StrCat({"browser-test-", envelope->dispatch_id.value});
  envelope->action_digest = ComputeActionDigest(*envelope);

  auto grant = core_service::mojom::MintedCapabilityGrant::New();
  grant->capability_id = envelope->capability.capability_reference.value;
  grant->service_generation = 1u;
  grant->policy_version = envelope->capability.policy_version;
  grant->actor_lease_id = envelope->capability.actor_lease_id.value;
  grant->task_id = envelope->task_id.value;
  grant->action_id = envelope->action_id.value;
  grant->action_class = action_class;
  grant->operation_kind = operation;
  grant->canonical_intent_digest.assign(
      envelope->canonical_intent_digest.begin(),
      envelope->canonical_intent_digest.end());
  grant->principal = core_service::mojom::PolicyPrincipal::New();
  grant->principal->kind = core_service::mojom::PolicyPrincipalKind::kAssistant;
  grant->proposal_digest = envelope->action_digest.value;
  grant->idempotency_key = envelope->idempotency_key;
  grant->scope = core_service::mojom::PolicyCapabilityScope::New();
  grant->scope->profile_id = "test-profile";
  grant->scope->tab_id = envelope->target_handle.tab_id.value;
  grant->scope->frame_id = envelope->target_handle.frame_id.value;
  grant->scope->page_epoch = envelope->target_handle.page_epoch.value;
  grant->scope->required_graph_revision = envelope->required_graph_revision;
  grant->scope->node_id = envelope->target_handle.node_id.value;
  grant->scope->origin = core_service::mojom::PolicyOrigin::New();
  grant->scope->origin->kind = core_service::mojom::PolicyOriginKind::kTuple;
  grant->scope->origin->serialization =
      envelope->target_handle.expected_origin.serialization;
  grant->data_classes = {core_service::mojom::BipSensitivity::kPersonal};
  grant->effective_risk =
      core_service::mojom::PolicyRiskClass::kSensitiveDisclosure;
  grant->issued_at_monotonic_ms = now_ms;
  grant->expires_at_monotonic_ms = active_lease_expires_at_monotonic_ms_;
  grant->authority_subject = core_service::mojom::AuthoritySubject::New();
  grant->authority_subject->kind =
      core_service::mojom::AuthoritySubjectKind::kTask;
  grant->authority_subject->authority_subject_id = envelope->task_id.value;

  const core_service::mojom::CapabilityRegistrationStatus status =
      capabilities_.Register(*grant, actor_leases_, base::TimeTicks::Now());
  LOG_IF(WARNING,
         status !=
             core_service::mojom::CapabilityRegistrationStatus::kRegistered)
      << "[taffy_test_grant_refused] status=" << status;
  return status ==
         core_service::mojom::CapabilityRegistrationStatus::kRegistered;
}

ActionResult TaffyBrowserTestBase::DispatchTaskActionAndWait(
    AuthorizedActionEnvelope envelope) {
  base::RunLoop run_loop;
  std::optional<ActionResult> captured;
  service_->SubmitTaskAction(
      std::move(envelope), base::BindLambdaForTesting([&](ActionResult result) {
        EXPECT_FALSE(captured.has_value())
            << "A task action produced more than one terminal result.";
        captured = std::move(result);
        run_loop.Quit();
      }));
  run_loop.Run();
  CHECK(captured.has_value());
  return std::move(*captured);
}

void TaffyBrowserTestBase::ExpectExfiltrationAttempt() {
  expect_exfiltration_attempt_ = true;
}

void TaffyBrowserTestBase::AllowUnsettledRequests() {
  allow_unsettled_requests_ = true;
}

void TaffyBrowserTestBase::AssertNoExfiltration() {
  const ExfiltrationSentinel& sentinel = origins_.sentinel();
  if (expect_exfiltration_attempt_) {
    EXPECT_TRUE(sentinel.HasHits())
        << "This test declared that it would reach the corpus exfiltration "
           "sink and did not. The absence assertion every other test relies on "
           "is only meaningful while this case proves the sink is reachable at "
           "all.";
    return;
  }
  EXPECT_FALSE(sentinel.HasHits()) << sentinel.DescribeHits();
}

void TaffyBrowserTestBase::AssertNoSeededSecretLeak() {
  CanaryLeakScanner scanner = CanaryLeakScanner::ForWholeCorpus();
  scanner.AddSink("projection delivered to the core service",
                  client_.TranscriptForLeakScan());
  scanner.AddJournal(journal_);
  scanner.AddAuditStream(audit_stream_);
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

}  // namespace taffy::test
