// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "taffy/test/recovery/workspace_vertical_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;
using test::CoreApiStatusObserver;
using test::EmptyVaultProfilePlatformAdapter;
using test::ObservedTaskStatus;
using test::ObservedWorkspaceExports;
using test::ObservedWorkspaceStatus;

class CoreServiceTaskVerticalBrowserTest : public PlatformBrowserTest {
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

IN_PROC_BROWSER_TEST_F(CoreServiceTaskVerticalBrowserTest,
                       BuildSourceTableCompletesThroughIsolatedCore) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  const GURL source_url =
      embedded_test_server()->GetURL("source.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(tab, source_url));

  // The shipping tab assembly owns this attachment. The test does not create
  // a broker or synthesize a renderer result: absence here means the product
  // attach patch is not wired into this browser-test composition.
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
  ASSERT_FALSE(core->browser_profile_id().empty());
  ASSERT_FALSE(core->browser_session_id().empty());

  // These are the browser-owned facts the Android TabModel bridge publishes.
  // No tab id, source id, origin, receipt, or provenance enters from the UI.
  const uint64_t window_token = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, window_token);
  constexpr int kProductTabId = 41;
  ASSERT_TRUE(core->RegisterTaskSourceTab(window_token, kProductTabId, tab));
  ASSERT_TRUE(core->SelectTaskSourceTab(window_token, kProductTabId, tab));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(window_token));

  auto consent_intent = api::TaskConsentPreview::New();
  consent_intent->source_hosts = {std::string(source_url.host())};
  consent_intent->source_discovery_enabled = false;
  consent_intent->new_source_cap = 0u;
  consent_intent->provider_route = api::TaskProviderRoute::kNoModelRequired;

  // Enter through the shipping generated Core API Mojo facade. The facade's
  // production entropy source owns task/operation/receipt identities; the
  // test learns the task identity only from a later immutable CoreStatus.
  ProfileCoreApiFacade facade(core);
  mojo::Remote<api::TaffyProfileCoreApi> facade_remote;
  mojo::Receiver<api::TaffyProfileCoreApi> facade_receiver(
      &facade, facade_remote.BindNewPipeAndPassReceiver());
  CoreApiStatusObserver status_observer;
  facade_remote->Observe(status_observer.BindNewPipeAndPassRemote());

  // The first task a surface sees. The open commit's durable state is
  // published the moment it lands, before the walk's next command runs, so
  // the first publication after an admitted start carries the task while it
  // is still queued: no proposal yet, and a phase that says so. Publishing
  // only once a commit yielded browser-visible effects is the shape a phone
  // showed as "Taffy did not accept Start before its deadline".
  std::optional<ObservedTaskStatus> first_task;
  status_observer.SetFirstTaskCallback(base::BindOnce(
      [](std::optional<ObservedTaskStatus>* first_task,
         ObservedTaskStatus task) { *first_task = std::move(task); },
      &first_task));

  base::test::TestFuture<api::CoreApiSubmissionStatus> admission;
  facade_remote->StartTask("Build a source table from the selected page",
                           api::TaskTemplateId::kBuildSourceTable, std::nullopt,
                           std::move(consent_intent), std::nullopt,
                           admission.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, admission.Get());

  ASSERT_TRUE(base::test::RunUntil(
      [&first_task]() { return first_task.has_value(); }))
      << "No task was published after an admitted start; availability="
      << static_cast<int>(core->availability())
      << ", generation=" << core->service_generation()
      << ", malformed=" << status_observer.malformed_payload_seen()
      << ", state_sequence=" << status_observer.state_sequence()
      << ", pending_admissions=" << core->pending_admission_count_for_testing()
      << ", late_replies=" << core->late_reply_count_for_testing();
  EXPECT_EQ(api::TaskPhase::kPlanning, first_task->phase)
      << "The first publication carried a task already past its open commit; "
      << "phase=" << static_cast<int>(first_task->phase)
      << ", revision=" << first_task->revision;
  EXPECT_FALSE(first_task->pending_action_id.has_value())
      << "The first publication already carried a proposal, so the queued "
      << "task was never shown";

  // Shared control deliberately asks the person before each step. Wait for
  // the real projected approval view, then return its browser-provided opaque
  // identities through the same generated Core API method the Compose surface
  // uses. Skipping this would turn the vertical into an impossible automatic
  // approval path rather than the shipping workflow.
  const bool approval_published = base::test::RunUntil([&status_observer]() {
    const std::optional<ObservedTaskStatus> task = status_observer.only_task();
    return task && task->pending_action_id.has_value();
  });
  const std::optional<ObservedTaskStatus> last_task_before_approval =
      status_observer.only_task();
  ASSERT_TRUE(approval_published)
      << "The Core API observer did not receive the step approval; "
      << "availability=" << static_cast<int>(core->availability())
      << ", generation=" << core->service_generation()
      << ", malformed=" << status_observer.malformed_payload_seen()
      << ", last_phase="
      << (last_task_before_approval
              ? static_cast<int>(last_task_before_approval->phase)
              : -1)
      << ", last_revision="
      << (last_task_before_approval ? last_task_before_approval->revision : 0u)
      << ", observations_seen=" << page_host->observations_seen()
      << ", observations_submitted=" << page_host->observations_submitted()
      << ", observations_refused=" << page_host->observations_refused()
      << ", pending_admissions=" << core->pending_admission_count_for_testing()
      << ", late_replies=" << core->late_reply_count_for_testing();
  const std::optional<ObservedTaskStatus> awaiting_approval =
      status_observer.only_task();
  ASSERT_TRUE(awaiting_approval);
  ASSERT_TRUE(awaiting_approval->pending_action_id);
  base::test::TestFuture<api::CoreApiSubmissionStatus> approval_admission;
  facade_remote->ApproveAction(awaiting_approval->task_id,
                               *awaiting_approval->pending_action_id,
                               approval_admission.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approval_admission.Get());

  const bool completed = base::test::RunUntil([&status_observer]() {
    return status_observer.only_task_in_phase(api::TaskPhase::kCompleted)
        .has_value();
  });
  const std::optional<ObservedTaskStatus> last_task =
      status_observer.only_task();
  ASSERT_TRUE(completed)
      << "The Core API observer did not receive a completed task; malformed="
      << status_observer.malformed_payload_seen() << ", last_phase="
      << (last_task ? static_cast<int>(last_task->phase) : -1)
      << ", last_revision=" << (last_task ? last_task->revision : 0u)
      << ", observations_seen=" << page_host->observations_seen()
      << ", observations_submitted=" << page_host->observations_submitted()
      << ", observations_refused=" << page_host->observations_refused()
      << ", pending_admissions=" << core->pending_admission_count_for_testing()
      << ", late_replies=" << core->late_reply_count_for_testing();
  ASSERT_FALSE(status_observer.malformed_payload_seen());
  ASSERT_FALSE(status_observer.permission_request_seen());
  const std::optional<ObservedTaskStatus> observed =
      status_observer.only_task_in_phase(api::TaskPhase::kCompleted);
  ASSERT_TRUE(observed);
  ASSERT_FALSE(observed->task_id.empty());
  ASSERT_GT(observed->revision, 0u);

  const std::string& task_id = observed->task_id;
  const std::optional<TerminalTaskLookup> terminal =
      core->FindTerminalTask(task_id);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(core->service_generation(), terminal->service_generation);
  EXPECT_EQ(service::TerminalTaskKind::kCompleted, terminal->kind);
  EXPECT_GT(terminal->task_revision, 0u);
  EXPECT_EQ(1u, page_host->observations_submitted());

  core->DeactivateTaskSourceWindow(window_token);
  core->UnregisterTaskSourceWindow(window_token);
}

IN_PROC_BROWSER_TEST_F(
    CoreServiceTaskVerticalBrowserTest,
    CompletedWorkspaceSavesAndExportsIdenticallyAfterCoreRestart) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  const GURL source_url =
      embedded_test_server()->GetURL("workspace.test", "/title1.html");
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
  ASSERT_EQ(CoreServiceManager::Availability::kReady, core->availability());
  const uint32_t first_inspection_count = platform_inspection_count();

  const uint64_t window_token = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, window_token);
  constexpr int kProductTabId = 44;
  ASSERT_TRUE(core->RegisterTaskSourceTab(window_token, kProductTabId, tab));
  ASSERT_TRUE(core->SelectTaskSourceTab(window_token, kProductTabId, tab));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(window_token));

  std::optional<ObservedTaskStatus> completed_task;
  std::optional<ObservedWorkspaceStatus> saved_workspace;
  std::string markdown_before_restart;
  std::string csv_before_restart;
  const uint64_t first_generation = core->service_generation();
  {
    ProfileCoreApiFacade facade(core);
    mojo::Remote<api::TaffyProfileCoreApi> facade_remote;
    mojo::Receiver<api::TaffyProfileCoreApi> facade_receiver(
        &facade, facade_remote.BindNewPipeAndPassReceiver());
    CoreApiStatusObserver observer;
    facade_remote->Observe(observer.BindNewPipeAndPassRemote());

    auto consent = api::TaskConsentPreview::New();
    consent->source_hosts = {std::string(source_url.host())};
    consent->source_discovery_enabled = false;
    consent->new_source_cap = 0u;
    consent->provider_route = api::TaskProviderRoute::kNoModelRequired;
    base::test::TestFuture<api::CoreApiSubmissionStatus> start;
    facade_remote->StartTask(
        "Build and retain a source table from the selected page",
        api::TaskTemplateId::kBuildSourceTable, std::nullopt,
        std::move(consent), std::nullopt, start.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start.Get());
    ASSERT_TRUE(base::test::RunUntil([&observer]() {
      const std::optional<ObservedTaskStatus> task = observer.only_task();
      return task && task->pending_action_id.has_value();
    }));
    const std::optional<ObservedTaskStatus> pending = observer.only_task();
    ASSERT_TRUE(pending);
    ASSERT_TRUE(pending->pending_action_id);
    base::test::TestFuture<api::CoreApiSubmissionStatus> approval;
    facade_remote->ApproveAction(pending->task_id, *pending->pending_action_id,
                                 approval.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approval.Get());

    ASSERT_TRUE(base::test::RunUntil([&observer]() {
      const std::optional<ObservedTaskStatus> task = observer.only_task();
      const std::optional<ObservedWorkspaceStatus> workspace =
          observer.only_workspace();
      return task && task->phase == api::TaskPhase::kCompleted &&
             task->workspace_id && workspace &&
             workspace->workspace_id == *task->workspace_id &&
             workspace->phase == api::WorkspacePhase::kDone &&
             !workspace->sources.empty() && !workspace->facts.empty();
    }));
    completed_task = observer.only_task();
    ASSERT_TRUE(completed_task);
    ASSERT_EQ(1u, page_host->observations_submitted());
    const std::optional<ObservedWorkspaceStatus> draft =
        observer.only_workspace();
    ASSERT_TRUE(draft);
    EXPECT_FALSE(draft->saved);
    test::ExpectSingleDomSourceWorkspace(*draft,
                                         std::string(source_url.host()));

    ASSERT_TRUE(test::RequestObservedWorkspaceExports(
        &facade_remote, &observer, "draft", *draft,
        /*require_source_backed_content=*/true));

    base::test::TestFuture<api::CoreApiSubmissionStatus> save;
    facade_remote->SaveWorkspace(draft->workspace_id, draft->revision,
                                 save.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, save.Get());
    ASSERT_TRUE(base::test::RunUntil([&observer, &draft]() {
      const std::optional<ObservedWorkspaceStatus> workspace =
          observer.only_workspace();
      return workspace && workspace->workspace_id == draft->workspace_id &&
             workspace->revision > draft->revision && workspace->saved;
    }));
    saved_workspace = observer.only_workspace();
    ASSERT_TRUE(saved_workspace);
    const std::optional<ObservedWorkspaceExports> saved_exports =
        test::RequestObservedWorkspaceExports(
            &facade_remote, &observer, "saved", *saved_workspace,
            /*require_source_backed_content=*/true);
    ASSERT_TRUE(saved_exports);
    markdown_before_restart = saved_exports->markdown.content;
    csv_before_restart = saved_exports->csv.content;
    ASSERT_FALSE(observer.malformed_payload_seen());
  }

  core->DeactivateTaskSourceWindow(window_token);
  core->UnregisterTaskSourceWindow(window_token);
  ASSERT_TRUE(base::test::RunUntil(
      [core]() { return core->is_quiescent_for_testing(); }));
  core->TearDownForIdle();
  ASSERT_EQ(first_generation + 1u, core->service_generation());
  ASSERT_EQ(CoreServiceManager::Availability::kStopped, core->availability());

  base::test::TestFuture<bool> reprepare;
  core->PrepareForCoreApi(reprepare.GetCallback());
  ASSERT_TRUE(reprepare.Get());
  ASSERT_EQ(CoreServiceManager::Availability::kReady, core->availability());
  ASSERT_EQ(first_generation + 1u, core->service_generation());
  ASSERT_GT(platform_inspection_count(), first_inspection_count);

  ProfileCoreApiFacade restored_facade(core);
  mojo::Remote<api::TaffyProfileCoreApi> restored_remote;
  mojo::Receiver<api::TaffyProfileCoreApi> restored_receiver(
      &restored_facade, restored_remote.BindNewPipeAndPassReceiver());
  CoreApiStatusObserver restored_observer;
  restored_remote->Observe(restored_observer.BindNewPipeAndPassRemote());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    const std::optional<ObservedTaskStatus> task =
        restored_observer.only_task();
    const std::optional<ObservedWorkspaceStatus> workspace =
        restored_observer.only_workspace();
    return restored_observer.snapshot_seen() && task &&
           task->phase == api::TaskPhase::kCompleted && task->workspace_id &&
           workspace && saved_workspace &&
           workspace->workspace_id == *task->workspace_id &&
           workspace->workspace_id == saved_workspace->workspace_id &&
           workspace->revision == saved_workspace->revision && workspace->saved;
  }));
  const std::optional<ObservedWorkspaceStatus> restored =
      restored_observer.only_workspace();
  ASSERT_TRUE(restored && saved_workspace);
  test::ExpectRestoredCompletedTask(*completed_task, restored_observer);
  test::ExpectRestoredWorkspace(*saved_workspace, *restored);
  EXPECT_FALSE(restored_observer.workspace_export());

  const std::optional<ObservedWorkspaceExports> after_restart =
      test::RequestObservedWorkspaceExports(
          &restored_remote, &restored_observer, "restored", *restored,
          /*require_source_backed_content=*/true);
  ASSERT_TRUE(after_restart);
  EXPECT_EQ(markdown_before_restart, after_restart->markdown.content);
  EXPECT_EQ(csv_before_restart, after_restart->csv.content);
  EXPECT_EQ(first_generation + 1u, restored_observer.service_generation());
  EXPECT_FALSE(restored_observer.malformed_payload_seen());
}

}  // namespace
}  // namespace taffy
