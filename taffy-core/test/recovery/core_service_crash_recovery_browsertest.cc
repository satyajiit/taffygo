// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <errno.h>
#include <signal.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "crypto/sha2.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace mojom = core_service::mojom;

constexpr char kDurableTaskId[] = "core-crash-task";
constexpr char kDirectIntentId[] = "direct-intent-core-crash";

struct CoreProcessSnapshot {
  content::ServiceProcessId service_process_id;
  base::ProcessId process_id;
  base::Process process;
};

std::vector<CoreProcessSnapshot> RunningCoreProcesses() {
  std::vector<CoreProcessSnapshot> matches;
  for (const content::ServiceProcessInfo& info :
       content::ServiceProcessHost::GetRunningProcessInfo()) {
    if (!info.IsService<mojom::TaffyCoreService>()) {
      continue;
    }
    matches.push_back(CoreProcessSnapshot{info.service_process_id(),
                                          info.GetProcess().Pid(),
                                          info.GetProcess().Duplicate()});
  }
  return matches;
}

// Android's utility PID belongs to its process-service parent, so base cannot
// waitpid() it. Poll the kernel without running the UI loop: the first crash
// assertion needs the process gone while its disconnect task is still queued.
bool TerminateCoreProcessAndWait(const base::Process& process) {
#if BUILDFLAG(IS_ANDROID)
  if (!process.Terminate(/*exit_code=*/1, /*wait=*/false)) {
    return false;
  }
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(60);
  while (base::TimeTicks::Now() < deadline) {
    if (kill(process.Pid(), 0) != 0 && errno == ESRCH) {
      return true;
    }
    base::PlatformThread::Sleep(base::Milliseconds(1));
  }
  return false;
#else
  return process.Terminate(/*exit_code=*/1, /*wait=*/true);
#endif
}

uint64_t NowMonotonicMilliseconds() {
  const int64_t value =
      (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::CoreServiceCommandPtr PendingCommand(uint64_t generation,
                                            std::string operation_id) {
  const uint64_t now = NowMonotonicMilliseconds();
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New();
  command->operation->operation_id = std::move(operation_id);
  command->operation->service_generation = generation;
  command->operation->task_revision = 0u;
  command->operation->deadline_monotonic_ms = now + 60'000u;
  command->operation->idempotency_key = "core-crash-idempotency";
  command->kind = mojom::CoreServiceCommandKind::kStartAuth;
  command->start_auth = mojom::StartAuthCommand::New();
  command->start_auth->flow_id = "core-crash-auth-flow";
  command->start_auth->method = mojom::AccountAuthMethod::kGithub;
  command->start_auth->redirect_binding_id = "core-crash-callback";
  command->start_auth->scopes = {mojom::AccountScope::kOpenId,
                                 mojom::AccountScope::kEmail};
  command->start_auth->issued_at_monotonic_ms = now;
  return command;
}

mojom::CoreServiceCommandPtr DurableTaskCommand(
    uint64_t generation,
    const std::string& browser_profile_id,
    const std::string& browser_session_id,
    mojom::TaskConsentPreviewPtr consent_preview) {
  const uint64_t now = NowMonotonicMilliseconds();
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New();
  command->operation->operation_id = "core-crash-open-task";
  command->operation->service_generation = generation;
  command->operation->task_revision = 0u;
  command->operation->deadline_monotonic_ms = now + 60'000u;
  command->operation->idempotency_key = "core-crash-open-task-key";
  command->kind = mojom::CoreServiceCommandKind::kStartTask;

  auto start = mojom::StartTaskCommand::New();
  start->task_id = kDurableTaskId;
  start->workspace_id = std::nullopt;
  start->browser_profile_id = browser_profile_id;
  start->browser_session_id = browser_session_id;
  start->kind = mojom::TaskKind::kResearch;
  start->goal = "Preserve this task across an isolated core crash";
  start->control_mode = mojom::TaskControlMode::kShared;
  start->provider_route_id = "no_model_required";
  start->assistant_config_version = 1u;
  start->policy_version = 1u;
  start->skill_version_id = std::nullopt;
  start->tool_allowlist = {"browser.dom.read"};
  // The milestone the product is pinned at, not the one this suite was written
  // under. A recovery fixture left behind on a superseded milestone keeps
  // passing while exercising a surface the product no longer has.
  start->milestone = mojom::TaskMilestone::kM5;
  auto source_budget = mojom::TaskBudget::New();
  source_budget->kind = mojom::TaskBudgetKind::kMaxSources;
  source_budget->limit = 1u;
  start->budgets.push_back(std::move(source_budget));
  for (mojom::TaskBudgetKind kind : {mojom::TaskBudgetKind::kMaxModelRequests,
                                     mojom::TaskBudgetKind::kMaxInputUnits,
                                     mojom::TaskBudgetKind::kMaxOutputUnits,
                                     mojom::TaskBudgetKind::kMaxCostUnits}) {
    start->budgets.push_back(mojom::TaskBudget::New(kind, 0u));
  }
  start->has_task_deadline = false;
  start->task_deadline_monotonic_ms = 0u;
  start->task_deadline_utc_ms = 0u;
  start->predecessor_task_id = std::nullopt;
  start->trace_id = "core-crash-task-trace";
  for (uint8_t byte = 1u; byte <= 32u; ++byte) {
    start->task_id_seed.push_back(byte);
  }
  start->template_id = mojom::TaskTemplateId::kBuildSourceTable;
  start->consent_preview = std::move(consent_preview);
  start->initial_consent_receipt_id = "core-crash-initial-consent";
  command->start_task = std::move(start);
  return command;
}

mojom::PolicyEvaluationRequestPtr DirectObservationPolicyRequest(
    const CoreServiceManager& manager,
    const ActorLeaseId& lease_id,
    const TabId& tab_id,
    uint64_t generation,
    uint64_t lease_expires_at_monotonic_ms) {
  const uint64_t now = NowMonotonicMilliseconds();
  auto request = mojom::PolicyEvaluationRequest::New();
  request->operation =
      mojom::OperationEnvelope::New("core-crash-policy", generation, 0u,
                                    now + 1'000u, "core-crash-policy-key");
  request->now_monotonic_ms = now;
  request->now_utc_ms = static_cast<uint64_t>(
      std::max<int64_t>(0, base::Time::Now().InMillisecondsSinceUnixEpoch()));
  request->principal = mojom::PolicyPrincipal::New(
      mojom::PolicyPrincipalKind::kAssistant, std::nullopt);
  request->action_class = mojom::PolicyActionClass::kObservePage;
  request->operation_kind = mojom::TaskActionOperationKind::kDomRead;
  request->proposal_digest = std::string(64u, 'a');
  const std::string canonical_intent_digest =
      crypto::SHA256HashString(request->proposal_digest);
  request->canonical_intent_digest.assign(canonical_intent_digest.begin(),
                                          canonical_intent_digest.end());
  request->scope = mojom::PolicyCapabilityScope::New();
  request->scope->profile_id = manager.browser_profile_id();
  request->scope->tab_id = tab_id.value;
  request->scope->frame_id = "core-crash-frame";
  request->scope->page_epoch = "core-crash-epoch";
  request->scope->origin = mojom::PolicyOrigin::New();
  request->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
  request->scope->origin->serialization = "https://core-crash.test";
  request->scope->required_graph_revision = 0u;
  request->data_classes = {mojom::BipSensitivity::kNotSensitive};
  request->context_risk = mojom::PolicyRiskClass::kLocalRead;
  request->expires_at_monotonic_ms = now + 1'000u;
  request->actor_lease = mojom::ActorLeaseFact::New();
  request->actor_lease->lease_id = lease_id.value;
  request->actor_lease->service_generation = generation;
  request->actor_lease->profile_id = manager.browser_profile_id();
  request->actor_lease->tab_id = tab_id.value;
  request->actor_lease->control_mode = mojom::TaskControlMode::kUser;
  request->actor_lease->expires_at_monotonic_ms = lease_expires_at_monotonic_ms;
  request->actor_lease->authority_subject = mojom::AuthoritySubject::New(
      mojom::AuthoritySubjectKind::kDirectUserIntent, kDirectIntentId);
  request->context = mojom::PolicyEvaluationContext::kDirectUserObservation;
  request->authority_subject = request->actor_lease->authority_subject.Clone();
  request->policy_version = 1u;
  return request;
}

class CoreServiceCrashRecoveryBrowserTest : public PlatformBrowserTest {
 protected:
  content::WebContents* active_tab_contents() {
    TabListInterface* const tabs = GetTabListInterface();
    tabs::TabInterface* const active_tab =
        tabs ? tabs->GetActiveTab() : nullptr;
    return active_tab ? active_tab->GetContents() : nullptr;
  }

  CoreServiceManager* manager() {
    content::WebContents* const contents = active_tab_contents();
    Profile* const profile =
        contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
                 : nullptr;
    return profile ? CoreServiceManagerFactory::GetForProfile(profile)
                   : nullptr;
  }

  CoreProcessSnapshot OnlyCoreProcess() {
    std::vector<CoreProcessSnapshot> processes = RunningCoreProcesses();
    EXPECT_EQ(1u, processes.size());
    if (processes.empty()) {
      return CoreProcessSnapshot{content::ServiceProcessId(),
                                 base::kNullProcessId, base::Process()};
    }
    return std::move(processes.front());
  }

  void WaitUntilGeneration(uint64_t generation) {
    ASSERT_TRUE(base::test::RunUntil([this, generation]() {
      return manager()->service_generation() == generation;
    })) << "The browser did not observe the isolated service loss.";
  }

  void PrepareAndExpectReady(base::TimeDelta minimum_delay) {
    CoreServiceManager* core = manager();
    ASSERT_TRUE(core);
    const base::TimeTicks started = base::TimeTicks::Now();
    base::test::TestFuture<bool> prepared;
    core->PrepareForCoreApi(prepared.GetCallback());
    ASSERT_TRUE(prepared.Get());
    EXPECT_EQ(CoreServiceManager::Availability::kReady, core->availability());
    EXPECT_GE(base::TimeTicks::Now() - started, minimum_delay);
  }
};

IN_PROC_BROWSER_TEST_F(CoreServiceCrashRecoveryBrowserTest,
                       IsolatedCrashFailsClosedAndRecoversLazily) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::NavigateToURL(
      tab, embedded_test_server()->GetURL("a.test", "/title1.html")));

  CoreServiceManager* const core = manager();
  ASSERT_TRUE(core);
  EXPECT_EQ(CoreServiceManager::Availability::kStopped, core->availability());

  PrepareAndExpectReady(base::TimeDelta());
  const uint64_t first_generation = core->service_generation();
  ASSERT_GT(first_generation, 0u);
  const std::string browser_profile_id = core->browser_profile_id();
  ASSERT_FALSE(browser_profile_id.empty());
  ASSERT_FALSE(core->browser_session_id().empty());

  // The product-tab registry, not this test or a UI string, derives the exact
  // BIP tab id, source id, and normalized committed origin. This is the same
  // trusted source-selection seam used by the shipping Core API facade.
  const uint64_t source_window = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, source_window);
  constexpr int kProductTabId = 43;
  ASSERT_TRUE(core->RegisterTaskSourceTab(source_window, kProductTabId, tab));
  ASSERT_TRUE(core->SelectTaskSourceTab(source_window, kProductTabId, tab));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(source_window));
  auto selection_intent = api::TaskConsentPreview::New();
  selection_intent->source_hosts = {"a.test"};
  selection_intent->source_discovery_enabled = false;
  selection_intent->new_source_cap = 0u;
  selection_intent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  std::optional<mojom::TaskConsentPreviewPtr> resolved_consent =
      core->ResolveStartTaskConsent(api::TaskTemplateId::kBuildSourceTable,
                                    *selection_intent);
  ASSERT_TRUE(resolved_consent);
  ASSERT_TRUE(*resolved_consent);

  CoreProcessSnapshot first_process = OnlyCoreProcess();
  ASSERT_TRUE(first_process.process.IsValid());
  EXPECT_NE(base::GetCurrentProcId(), first_process.process_id);
#if BUILDFLAG(IS_LINUX)
  EXPECT_TRUE(first_process.process.IsSeccompSandboxed());
#endif

  // Commit one canonical Rust task through the real browser storage broker.
  // Admission alone is not durability evidence: the browser binding appears
  // only after the storage completion is reduced and a new CoreStatus is
  // published. Its revision is the checkpoint/journal-tail fact recovered
  // below by a fresh utility process.
  base::test::TestFuture<mojom::AdmissionPtr> task_admission;
  core->Submit(DurableTaskCommand(first_generation, browser_profile_id,
                                  core->browser_session_id(),
                                  std::move(*resolved_consent)),
               task_admission.GetCallback());
  mojom::AdmissionPtr admitted_task = task_admission.Take();
  ASSERT_TRUE(admitted_task);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, admitted_task->status);
  ASSERT_TRUE(base::test::RunUntil(
      [core]() { return core->FindTaskRevision(kDurableTaskId).has_value(); }));
  const std::optional<uint64_t> committed_task_revision =
      core->FindTaskRevision(kDurableTaskId);
  ASSERT_TRUE(committed_task_revision.has_value());
  ASSERT_EQ(6u, *committed_task_revision);

  const base::TimeTicks authority_now = base::TimeTicks::Now();
  const TabId authority_tab{"core-crash-tab"};
  const ActorLeaseResult lease = core->actor_leases()->IssueDirectObservation(
      kDirectIntentId, authority_tab, 2'000u, authority_now);
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  ASSERT_TRUE(core->actor_leases()->IsValidFor(lease.lease_id, authority_tab,
                                               authority_now));

  // Rust policy is the only grant mint. A granted result is returned only
  // after the utility has registered this capability in the production
  // browser ledger, so this is live generation-bound authority rather than a
  // test-constructed ledger record.
  base::test::TestFuture<mojom::PolicyEvaluationResultPtr> policy_evaluation;
  core->EvaluatePolicy(DirectObservationPolicyRequest(
                           *core, lease.lease_id, authority_tab,
                           first_generation, lease.expires_at_monotonic_ms),
                       policy_evaluation.GetCallback());
  mojom::PolicyEvaluationResultPtr policy_result = policy_evaluation.Take();
  ASSERT_TRUE(policy_result);
  ASSERT_EQ(mojom::PolicyEvaluationStatus::kGranted, policy_result->status);
  ASSERT_TRUE(policy_result->minted_grant);
  mojom::MintedCapabilityGrantPtr old_grant =
      policy_result->minted_grant.Clone();

  // Termination completes before the browser UI sequence is allowed to handle
  // the pipe loss. Submitting in that interval deterministically leaves one
  // real request pending on the dead utility pipe; no service double or
  // in-process implementation participates.
  ASSERT_TRUE(TerminateCoreProcessAndWait(first_process.process));
  size_t admission_callback_count = 0u;
  mojom::AdmissionStatus admission_status =
      mojom::AdmissionStatus::kInvalidCommand;
  base::RunLoop admission_loop;
  core->Submit(
      PendingCommand(first_generation, "core-crash-pending-operation"),
      base::BindOnce(
          [](size_t* callback_count, mojom::AdmissionStatus* status,
             base::RunLoop* run_loop, mojom::AdmissionPtr admission) {
            ++*callback_count;
            if (admission) {
              *status = admission->status;
            }
            run_loop->Quit();
          },
          &admission_callback_count, &admission_status, &admission_loop));
  admission_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, admission_callback_count);
  EXPECT_EQ(mojom::AdmissionStatus::kCoreUnavailable, admission_status);
  WaitUntilGeneration(first_generation + 1u);
  EXPECT_EQ(CoreServiceManager::Availability::kUnavailable,
            core->availability());
  EXPECT_FALSE(core->FindTaskRevision(kDurableTaskId).has_value());
  EXPECT_FALSE(core->actor_leases()->IsValidFor(lease.lease_id, authority_tab,
                                                base::TimeTicks::Now()));
  EXPECT_EQ(mojom::CapabilityRegistrationStatus::kStaleGeneration,
            core->capabilities()->Register(*old_grant, *core->actor_leases(),
                                           base::TimeTicks::Now()));

  // The tab and renderer remain ordinary browser-owned state while the Rust
  // process is unavailable. A second committed navigation is stronger than a
  // pointer-liveness check: manual browsing still executes end to end.
  EXPECT_EQ(tab, active_tab_contents());

  base::test::TestFuture<bool> first_restart;
  const base::TimeTicks first_restart_started = base::TimeTicks::Now();
  core->PrepareForCoreApi(first_restart.GetCallback());
  ASSERT_TRUE(content::NavigateToURL(
      tab, embedded_test_server()->GetURL("b.test", "/title2.html")));

  // First and second losses obey the 250 ms and 1 s lazy-restart floors. Each
  // new process initializes from a fresh storage bootstrap containing the
  // browser-owned checkpoint and journal tail, while its OS and service-process
  // identities are fresh. This is utility recovery inside one live browser
  // session; the tab binding is deliberately not evidence for browser-process
  // restart consent restoration.
  ASSERT_TRUE(first_restart.Get());
  EXPECT_GE(base::TimeTicks::Now() - first_restart_started,
            base::Milliseconds(200));
  EXPECT_EQ(CoreServiceManager::Availability::kReady, core->availability());
  EXPECT_EQ(browser_profile_id, core->browser_profile_id());
  EXPECT_EQ(committed_task_revision, core->FindTaskRevision(kDurableTaskId));
  CoreProcessSnapshot second_process = OnlyCoreProcess();
  EXPECT_NE(first_process.service_process_id,
            second_process.service_process_id);
  EXPECT_NE(first_process.process_id, second_process.process_id);

  ASSERT_TRUE(TerminateCoreProcessAndWait(second_process.process));
  WaitUntilGeneration(first_generation + 2u);
  EXPECT_EQ(CoreServiceManager::Availability::kUnavailable,
            core->availability());
  PrepareAndExpectReady(base::Milliseconds(900));
  EXPECT_EQ(browser_profile_id, core->browser_profile_id());
  EXPECT_EQ(committed_task_revision, core->FindTaskRevision(kDurableTaskId));
  CoreProcessSnapshot third_process = OnlyCoreProcess();
  EXPECT_NE(second_process.service_process_id,
            third_process.service_process_id);

  // Three unexpected losses inside five minutes open the profile circuit.
  // No fourth process is launched without the required explicit retry. The
  // policy unit test checks the exact four-second third ordinal; this process
  // test also checks that the manager preserves that cool-down after retry.
  ASSERT_TRUE(TerminateCoreProcessAndWait(third_process.process));
  WaitUntilGeneration(first_generation + 3u);
  EXPECT_EQ(CoreServiceManager::Availability::kCircuitOpen,
            core->availability());
  base::test::TestFuture<bool> refused;
  core->PrepareForCoreApi(refused.GetCallback());
  EXPECT_FALSE(refused.Get());
  EXPECT_TRUE(RunningCoreProcesses().empty());

  core->RetryExplicitly();
  PrepareAndExpectReady(base::Milliseconds(3'800));
  EXPECT_EQ(first_generation + 3u, core->service_generation());
  EXPECT_EQ(browser_profile_id, core->browser_profile_id());
  EXPECT_EQ(committed_task_revision, core->FindTaskRevision(kDurableTaskId));
  CoreProcessSnapshot explicit_retry_process = OnlyCoreProcess();
  EXPECT_NE(third_process.service_process_id,
            explicit_retry_process.service_process_id);
  EXPECT_EQ(tab, active_tab_contents());

  core->DeactivateTaskSourceWindow(source_window);
  core->UnregisterTaskSourceWindow(source_window);
}

}  // namespace
}  // namespace taffy
