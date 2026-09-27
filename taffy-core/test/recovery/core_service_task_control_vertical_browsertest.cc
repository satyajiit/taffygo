// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/process/process.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;
using test::CoreApiStatusObserver;
using test::EmptyVaultProfilePlatformAdapter;
using test::ObservedTaskStatus;

base::Process OnlyRunningCoreProcess() {
  base::Process process;
  size_t matches = 0u;
  for (const content::ServiceProcessInfo& info :
       content::ServiceProcessHost::GetRunningProcessInfo()) {
    if (!info.IsService<service::TaffyCoreService>()) {
      continue;
    }
    ++matches;
    if (!process.IsValid()) {
      process = info.GetProcess().Duplicate();
    }
  }
  EXPECT_EQ(1u, matches);
  return process;
}

class CoreServiceTaskControlVerticalBrowserTest : public PlatformBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  content::WebContents* active_tab_contents() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  CoreServiceManager* manager() {
    content::WebContents* const contents = active_tab_contents();
    Profile* const profile =
        contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
                 : nullptr;
    return profile ? CoreServiceManagerFactory::GetForProfile(profile)
                   : nullptr;
  }

  void BindEmptyVaultPlatformAdapter(CoreServiceManager& core) {
    core.BindPlatformAdapter(platform_adapter_.BindNewPipeAndPassRemote());
  }

  uint32_t platform_inspection_count() const {
    return platform_adapter_.inspection_count();
  }

 private:
  EmptyVaultProfilePlatformAdapter platform_adapter_;
};

IN_PROC_BROWSER_TEST_F(CoreServiceTaskControlVerticalBrowserTest,
                       CancelTaskPublishesCancelledThroughIsolatedCore) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  const GURL source_url =
      embedded_test_server()->GetURL("cancel.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(tab, source_url));
  ASSERT_TRUE(TaffyPageIntelligenceHost::FromWebContents(tab));

  CoreServiceManager* const core = manager();
  ASSERT_TRUE(core);
  BindEmptyVaultPlatformAdapter(*core);
  base::test::TestFuture<bool> prepared;
  core->PrepareForCoreApi(prepared.GetCallback());
  ASSERT_TRUE(prepared.Get());
  EXPECT_GT(platform_inspection_count(), 0u);
  ASSERT_EQ(CoreServiceManager::Availability::kReady, core->availability());

  const uint64_t window_token = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, window_token);
  constexpr int kProductTabId = 42;
  ASSERT_TRUE(core->RegisterTaskSourceTab(window_token, kProductTabId, tab));
  ASSERT_TRUE(core->SelectTaskSourceTab(window_token, kProductTabId, tab));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(window_token));

  ProfileCoreApiFacade facade(core);
  mojo::Remote<api::TaffyProfileCoreApi> facade_remote;
  mojo::Receiver<api::TaffyProfileCoreApi> facade_receiver(
      &facade, facade_remote.BindNewPipeAndPassReceiver());
  CoreApiStatusObserver status_observer;
  facade_remote->Observe(status_observer.BindNewPipeAndPassRemote());

  std::string task_id;
  api::TaskPhase first_phase = api::TaskPhase::kOutcomeUnknown;
  base::test::TestFuture<api::CoreApiSubmissionStatus> cancel_admission;
  status_observer.SetFirstTaskCallback(base::BindOnce(
      [](mojo::Remote<api::TaffyProfileCoreApi>* facade_remote,
         base::test::TestFuture<api::CoreApiSubmissionStatus>* cancel_admission,
         std::string* task_id, api::TaskPhase* first_phase,
         ObservedTaskStatus task) {
        *task_id = task.task_id;
        *first_phase = task.phase;
        (*facade_remote)
            ->CancelTask(task.task_id, cancel_admission->GetCallback());
      },
      &facade_remote, &cancel_admission, &task_id, &first_phase));

  auto consent_intent = api::TaskConsentPreview::New();
  consent_intent->source_hosts = {std::string(source_url.host())};
  consent_intent->source_discovery_enabled = false;
  consent_intent->new_source_cap = 0u;
  consent_intent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  base::test::TestFuture<api::CoreApiSubmissionStatus> start_admission;
  facade_remote->StartTask("Cancel the selected-page task",
                           api::TaskTemplateId::kBuildSourceTable, std::nullopt,
                           std::move(consent_intent), std::nullopt,
                           start_admission.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start_admission.Get());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, cancel_admission.Get());
  ASSERT_FALSE(task_id.empty());
  ASSERT_NE(api::TaskPhase::kCompleted, first_phase);
  ASSERT_NE(api::TaskPhase::kFailed, first_phase);
  ASSERT_NE(api::TaskPhase::kCancelled, first_phase);

  // Cancelling and Cancelled deliberately share the plain-language Stopped
  // phase. Wait for the browser-private terminal too, or this predicate can
  // return on the settlement snapshot before the core has durably finished
  // cancellation.
  ASSERT_TRUE(base::test::RunUntil([&status_observer, core, &task_id]() {
    const std::optional<ObservedTaskStatus> observed =
        status_observer.only_task_in_phase(api::TaskPhase::kCancelled);
    const std::optional<TerminalTaskLookup> terminal =
        task_id.empty() ? std::nullopt : core->FindTerminalTask(task_id);
    return observed && terminal &&
           observed->revision == terminal->task_revision;
  })) << "The Core API and browser binding did not reach cancelled together.";
  ASSERT_FALSE(status_observer.malformed_payload_seen());
  ASSERT_FALSE(status_observer.permission_request_seen());
  const std::optional<ObservedTaskStatus> observed =
      status_observer.only_task_in_phase(api::TaskPhase::kCancelled);
  ASSERT_TRUE(observed);
  ASSERT_EQ(task_id, observed->task_id);

  const std::optional<TerminalTaskLookup> terminal =
      core->FindTerminalTask(task_id);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(core->service_generation(), terminal->service_generation);
  EXPECT_EQ(service::TerminalTaskKind::kCancelled, terminal->kind);
  EXPECT_EQ(observed->revision, terminal->task_revision);

  core->DeactivateTaskSourceWindow(window_token);
  core->UnregisterTaskSourceWindow(window_token);
}

IN_PROC_BROWSER_TEST_F(
    CoreServiceTaskControlVerticalBrowserTest,
    PausedTaskRequiresDurableResumeAfterCoreGenerationRestart) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  const GURL source_url =
      embedded_test_server()->GetURL("pause.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(tab, source_url));
  TaffyPageIntelligenceHost* const page_host =
      TaffyPageIntelligenceHost::FromWebContents(tab);
  ASSERT_TRUE(page_host);

  CoreServiceManager* const core = manager();
  ASSERT_TRUE(core);
  BindEmptyVaultPlatformAdapter(*core);
  base::test::TestFuture<bool> prepared;
  core->PrepareForCoreApi(prepared.GetCallback());
  ASSERT_TRUE(prepared.Get());
  EXPECT_GT(platform_inspection_count(), 0u);
  ASSERT_EQ(CoreServiceManager::Availability::kReady, core->availability());
  const uint64_t first_generation = core->service_generation();

  const uint64_t first_window_token = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, first_window_token);
  constexpr int kProductTabId = 45;
  ASSERT_TRUE(
      core->RegisterTaskSourceTab(first_window_token, kProductTabId, tab));
  ASSERT_TRUE(
      core->SelectTaskSourceTab(first_window_token, kProductTabId, tab));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(first_window_token));

  std::string task_id;
  std::string stale_action_id;
  uint64_t paused_revision = 0u;
  {
    ProfileCoreApiFacade facade(core);
    mojo::Remote<api::TaffyProfileCoreApi> facade_remote;
    mojo::Receiver<api::TaffyProfileCoreApi> facade_receiver(
        &facade, facade_remote.BindNewPipeAndPassReceiver());
    CoreApiStatusObserver status_observer;
    facade_remote->Observe(status_observer.BindNewPipeAndPassRemote());

    auto consent_intent = api::TaskConsentPreview::New();
    consent_intent->source_hosts = {std::string(source_url.host())};
    consent_intent->source_discovery_enabled = false;
    consent_intent->new_source_cap = 0u;
    consent_intent->provider_route = api::TaskProviderRoute::kNoModelRequired;
    base::test::TestFuture<api::CoreApiSubmissionStatus> start_admission;
    facade_remote->StartTask("Pause and resume the selected-page task",
                             api::TaskTemplateId::kBuildSourceTable,
                             std::nullopt, std::move(consent_intent),
                             std::nullopt, start_admission.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start_admission.Get());

    ASSERT_TRUE(base::test::RunUntil([&status_observer, core]() {
      const std::optional<ObservedTaskStatus> task =
          status_observer.only_task();
      return task && task->pending_action_id &&
             core->FindTaskControl(task->task_id,
                                   service::TaskControlKind::kPause);
    })) << "The task never reached its real approval and pause boundary.";
    const std::optional<ObservedTaskStatus> pending =
        status_observer.only_task();
    ASSERT_TRUE(pending);
    ASSERT_TRUE(pending->pending_action_id);
    task_id = pending->task_id;
    stale_action_id = *pending->pending_action_id;
    ASSERT_FALSE(task_id.empty());
    ASSERT_FALSE(stale_action_id.empty());
    EXPECT_EQ(
        1u, CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*core));
    EXPECT_EQ(0u, page_host->observations_submitted());

    base::test::TestFuture<api::CoreApiSubmissionStatus> pause_admission;
    facade_remote->PauseTask(task_id, pause_admission.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, pause_admission.Get());

    ASSERT_TRUE(base::test::RunUntil([&status_observer, core, &task_id]() {
      const std::optional<ObservedTaskStatus> task =
          status_observer.only_task_in_phase(api::TaskPhase::kPaused);
      const std::optional<TaskControlLookup> resume =
          core->FindTaskControl(task_id, service::TaskControlKind::kResume);
      return task && resume && task->task_id == task_id &&
             task->revision == resume->task_revision;
    })) << "The pause admission never reached its durable Paused state.";
    const std::optional<ObservedTaskStatus> paused =
        status_observer.only_task_in_phase(api::TaskPhase::kPaused);
    ASSERT_TRUE(paused);
    paused_revision = paused->revision;
    // The public phase deliberately folds Pausing and Paused together. The
    // two durable task revisions prove both shipping transitions occurred:
    // Pause commits Pausing, then the browser's settlement commits Paused.
    ASSERT_EQ(pending->revision + 2u, paused_revision);
    const std::optional<TaskControlLookup> paused_resume =
        core->FindTaskControl(task_id, service::TaskControlKind::kResume);
    ASSERT_TRUE(paused_resume);
    EXPECT_EQ(first_generation, paused_resume->service_generation);
    EXPECT_FALSE(paused->pending_action_id);
    EXPECT_FALSE(core->FindPendingApproval(task_id, stale_action_id));
    EXPECT_FALSE(core->FindTerminalTask(task_id));
    EXPECT_EQ(
        0u, CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*core));
    ASSERT_EQ(
        1u, CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*core));

    // The identifier was genuinely offered by the production CoreStatus, but
    // Pause revoked its one-use authority. Returning it through the shipping
    // facade must fail locally and must not let the page observation escape.
    base::test::TestFuture<api::CoreApiSubmissionStatus> stale_approval;
    facade_remote->ApproveAction(task_id, stale_action_id,
                                 stale_approval.GetCallback());
    EXPECT_EQ(api::CoreApiSubmissionStatus::kStaleRevision,
              stale_approval.Get());
    EXPECT_EQ(0u, page_host->observations_submitted());
    ASSERT_FALSE(status_observer.malformed_payload_seen());
    ASSERT_FALSE(status_observer.permission_request_seen());
  }

  // Kill only the sandboxed utility process. The regular browser tab and its
  // browser-issued source remain live while the successor core restores its
  // journal; no source identity enters from this test.
  base::Process first_core_process = OnlyRunningCoreProcess();
  ASSERT_TRUE(first_core_process.IsValid());
  // Android cannot synchronously wait on this non-child service process. The
  // generation transition below is the browser-observed exit barrier.
  ASSERT_TRUE(first_core_process.Terminate(/*exit_code=*/1, /*wait=*/false));
  ASSERT_TRUE(base::test::RunUntil([core, first_generation]() {
    return core->service_generation() == first_generation + 1u;
  })) << "The browser did not observe the isolated core process loss.";
  ASSERT_EQ(CoreServiceManager::Availability::kUnavailable,
            core->availability());
  ASSERT_EQ(1u,
            CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*core));

  base::test::TestFuture<bool> reprepare;
  core->PrepareForCoreApi(reprepare.GetCallback());
  ASSERT_TRUE(reprepare.Get());
  ASSERT_EQ(CoreServiceManager::Availability::kReady, core->availability());
  ASSERT_EQ(first_generation + 1u, core->service_generation());

  ProfileCoreApiFacade restored_facade(core);
  mojo::Remote<api::TaffyProfileCoreApi> restored_remote;
  mojo::Receiver<api::TaffyProfileCoreApi> restored_receiver(
      &restored_facade, restored_remote.BindNewPipeAndPassReceiver());
  CoreApiStatusObserver restored_observer;
  restored_remote->Observe(restored_observer.BindNewPipeAndPassRemote());
  ASSERT_TRUE(base::test::RunUntil([&restored_observer, core, &task_id,
                                    paused_revision]() {
    const std::optional<ObservedTaskStatus> task =
        restored_observer.only_task_in_phase(api::TaskPhase::kPaused);
    const std::optional<TaskControlLookup> resume =
        core->FindTaskControl(task_id, service::TaskControlKind::kResume);
    return task && resume && task->task_id == task_id &&
           task->revision == paused_revision &&
           resume->task_revision == paused_revision;
  })) << "The successor core did not restore the inert paused task.";
  const std::optional<TaskControlLookup> restored_resume =
      core->FindTaskControl(task_id, service::TaskControlKind::kResume);
  ASSERT_TRUE(restored_resume);
  EXPECT_EQ(first_generation + 1u, restored_resume->service_generation);
  EXPECT_FALSE(core->FindPendingApproval(task_id, stale_action_id));
  EXPECT_EQ(0u,
            CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*core));
  EXPECT_EQ(1u,
            CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*core));
  EXPECT_EQ(0u, page_host->observations_submitted());

  base::test::TestFuture<api::CoreApiSubmissionStatus> resume_admission;
  restored_remote->ResumeTask(task_id, resume_admission.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, resume_admission.Get());

  const bool resumed_authority_published =
      base::test::RunUntil([&restored_observer, &task_id, paused_revision]() {
        const std::optional<ObservedTaskStatus> task =
            restored_observer.only_task();
        return task && task->task_id == task_id &&
               task->revision > paused_revision &&
               task->phase != api::TaskPhase::kPaused &&
               task->pending_action_id.has_value();
      });
  const std::optional<ObservedTaskStatus> last_resumed_task =
      restored_observer.only_task();
  ASSERT_TRUE(resumed_authority_published)
      << "The storage-backed Resume did not restore runnable authority; "
      << "last_phase="
      << (last_resumed_task ? static_cast<int>(last_resumed_task->phase) : -1)
      << ", last_revision="
      << (last_resumed_task ? last_resumed_task->revision : 0u)
      << ", pending_action="
      << (last_resumed_task && last_resumed_task->pending_action_id)
      << ", state_sequence=" << restored_observer.state_sequence()
      << ", accepted_consents="
      << CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*core)
      << ", suspended_consents="
      << CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*core)
      << ", pending_host_task_effects="
      << CoreServiceManagerTaskEffectTestPeer::PendingHostTaskEffectCount(*core)
      << ", observations_seen=" << page_host->observations_seen()
      << ", observations_submitted=" << page_host->observations_submitted()
      << ", observations_refused=" << page_host->observations_refused()
      << ", pending_admissions=" << core->pending_admission_count_for_testing()
      << ", late_replies=" << core->late_reply_count_for_testing();
  const std::optional<ObservedTaskStatus> resumed =
      restored_observer.only_task();
  ASSERT_TRUE(resumed);
  ASSERT_TRUE(resumed->pending_action_id);
  EXPECT_NE(stale_action_id, *resumed->pending_action_id);
  const std::optional<PendingApprovalLookup> resumed_approval =
      core->FindPendingApproval(task_id, *resumed->pending_action_id);
  ASSERT_TRUE(resumed_approval);
  EXPECT_EQ(first_generation + 1u, resumed_approval->service_generation);
  EXPECT_EQ(1u,
            CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*core));
  EXPECT_EQ(0u,
            CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*core));
  EXPECT_EQ(0u, page_host->observations_submitted());

  base::test::TestFuture<api::CoreApiSubmissionStatus> approval_admission;
  restored_remote->ApproveAction(task_id, *resumed->pending_action_id,
                                 approval_admission.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approval_admission.Get());

  ASSERT_TRUE(base::test::RunUntil([&restored_observer, core, &task_id]() {
    const std::optional<ObservedTaskStatus> task =
        restored_observer.only_task_in_phase(api::TaskPhase::kCompleted);
    const std::optional<TerminalTaskLookup> terminal =
        core->FindTerminalTask(task_id);
    return task && terminal && task->task_id == task_id &&
           task->revision == terminal->task_revision;
  })) << "The resumed task did not finish through the isolated core.";
  const std::optional<TerminalTaskLookup> terminal =
      core->FindTerminalTask(task_id);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(first_generation + 1u, terminal->service_generation);
  EXPECT_EQ(service::TerminalTaskKind::kCompleted, terminal->kind);
  EXPECT_EQ(1u, page_host->observations_submitted());
  EXPECT_FALSE(restored_observer.malformed_payload_seen());
  EXPECT_FALSE(restored_observer.permission_request_seen());

  core->DeactivateTaskSourceWindow(first_window_token);
  core->UnregisterTaskSourceWindow(first_window_token);
}

}  // namespace
}  // namespace taffy
