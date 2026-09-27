// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
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
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "taffy/test/recovery/library_memory_vertical_test_support.h"
#include "taffy/test/recovery/workspace_vertical_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
using test::CoreApiStatusObserver;
using test::EmptyVaultProfilePlatformAdapter;
using test::LibraryMemoryTaskModelEndpoint;
using test::ObservedLibraryStatus;
using test::ObservedMemoryStatus;
using test::ObservedTaskStatus;
using test::ObservedWorkspaceStatus;

class LibraryMemoryVerticalBrowserTest : public PlatformBrowserTest {
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

IN_PROC_BROWSER_TEST_F(LibraryMemoryVerticalBrowserTest,
                       TaskWritesReadsAndRestoresAcrossCoreRestart) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  const GURL source_url =
      embedded_test_server()->GetURL("library-memory.test", "/title1.html");
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
  const uint64_t first_generation = core->service_generation();
  const uint32_t first_inspection_count = platform_inspection_count();

  // Library promotion accepts only a cited fact from an exact saved
  // workspace. Produce that prerequisite through the real selected-page task
  // flow, including the approval identity published by CoreStatus.
  const uint64_t window_token = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, window_token);
  constexpr int kSourceTabId = 52;
  ASSERT_TRUE(core->RegisterTaskSourceTab(window_token, kSourceTabId, tab));
  ASSERT_TRUE(core->SelectTaskSourceTab(window_token, kSourceTabId, tab));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(window_token));

  std::optional<ObservedLibraryStatus> expected_library;
  std::optional<ObservedMemoryStatus> expected_memory;
  std::string library_query;
  constexpr char kMemoryStatement[] =
      "Prefer concise source tables with visible citations";
  constexpr char kMemoryQuery[] = "concise citations";
  {
    ProfileCoreApiFacade facade(core);
    mojo::Remote<api::TaffyProfileCoreApi> remote;
    mojo::Receiver<api::TaffyProfileCoreApi> receiver(
        &facade, remote.BindNewPipeAndPassReceiver());
    CoreApiStatusObserver observer;
    remote->Observe(observer.BindNewPipeAndPassRemote());
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return observer.snapshot_seen() &&
             observer.availability() == api::CoreAvailability::kReady;
    }));
    ASSERT_FALSE(observer.malformed_payload_seen());
    ASSERT_EQ(api::LibraryAvailability::kAvailable,
              observer.library().availability);
    ASSERT_EQ(api::MemoryAvailability::kAvailable,
              observer.memory().availability);
    ASSERT_EQ(0u, observer.library().revision);
    ASSERT_EQ(0u, observer.memory().revision);
    ASSERT_TRUE(observer.library().entries.empty());
    ASSERT_TRUE(observer.memory().records.empty());

    auto consent = api::TaskConsentPreview::New();
    consent->source_hosts = {std::string(source_url.host())};
    consent->source_discovery_enabled = false;
    consent->new_source_cap = 0u;
    consent->provider_route = api::TaskProviderRoute::kNoModelRequired;
    base::test::TestFuture<api::CoreApiSubmissionStatus> start;
    remote->StartTask("Build a cited table to keep in Library",
                      api::TaskTemplateId::kBuildSourceTable, std::nullopt,
                      std::move(consent), std::nullopt, start.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start.Get());
    ASSERT_TRUE(base::test::RunUntil([&observer]() {
      const std::optional<ObservedTaskStatus> task = observer.only_task();
      return task && task->pending_action_id.has_value();
    }));
    const std::optional<ObservedTaskStatus> pending = observer.only_task();
    ASSERT_TRUE(pending && pending->pending_action_id);
    base::test::TestFuture<api::CoreApiSubmissionStatus> approval;
    remote->ApproveAction(pending->task_id, *pending->pending_action_id,
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
             !workspace->facts.empty();
    }));
    ASSERT_EQ(1u, page_host->observations_submitted());

    const std::optional<ObservedWorkspaceStatus> draft =
        observer.only_workspace();
    ASSERT_TRUE(draft);
    test::ExpectSingleDomSourceWorkspace(*draft,
                                         std::string(source_url.host()));
    base::test::TestFuture<api::CoreApiSubmissionStatus> save_workspace;
    remote->SaveWorkspace(draft->workspace_id, draft->revision,
                          save_workspace.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, save_workspace.Get());
    ASSERT_TRUE(base::test::RunUntil([&observer, &draft]() {
      const std::optional<ObservedWorkspaceStatus> workspace =
          observer.only_workspace();
      return workspace && workspace->workspace_id == draft->workspace_id &&
             workspace->revision > draft->revision && workspace->saved;
    }));
    const std::optional<ObservedWorkspaceStatus> saved =
        observer.only_workspace();
    ASSERT_TRUE(saved);
    const std::optional<ObservedTaskStatus> source_task = observer.only_task();
    ASSERT_TRUE(source_task);
    const auto& fact = saved->facts.front();
    library_query = fact.field;

    // Configure an ordinary keyless endpoint a person could run themselves.
    // Only the external provider response is deterministic; the request goes
    // through the shipping model broker and the reply is decoded and executed
    // by the isolated Rust task runtime.
    LibraryMemoryTaskModelEndpoint model_endpoint(
        saved->workspace_id, saved->revision, fact.fact_id, library_query,
        kMemoryStatement, kMemoryQuery);
    std::vector<api::CustomModelSpecViewPtr> models;
    models.push_back(api::CustomModelSpecView::New(
        "library-memory-model", "Library and Memory test model", 16'384u,
        2'048u, /*reasoning=*/true, /*tool_calling=*/true));
    base::test::TestFuture<api::CoreApiSubmissionStatus> save_provider;
    remote->SaveCustomProvider(
        "library-memory-test", "Library and Memory test provider",
        model_endpoint.base_url(), api::ProviderWireApiView::kOpenAiCompletions,
        std::nullopt, std::move(models), api::DetectedServerViewPtr(),
        save_provider.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, save_provider.Get());
    base::test::TestFuture<api::CoreApiSubmissionStatus> choose_model;
    remote->SetProviderModelPreference(
        "library-memory-test",
        std::optional<std::string>("library-memory-model"),
        api::ThinkingPreferenceView::New(api::ThinkingLevelView::kMedium),
        choose_model.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, choose_model.Get());

    auto model_consent = api::TaskConsentPreview::New();
    model_consent->source_hosts = {std::string(source_url.host())};
    model_consent->source_discovery_enabled = false;
    model_consent->new_source_cap = 0u;
    model_consent->provider_route = api::TaskProviderRoute::kDirectUserKey;
    base::test::TestFuture<api::CoreApiSubmissionStatus> model_start;
    remote->StartTask(
        "Keep the cited fact and remember how I want source tables presented",
        api::TaskTemplateId::kSummarizeEvidence,
        std::optional<std::string>(saved->workspace_id),
        std::move(model_consent), std::nullopt, model_start.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, model_start.Get());

    // The first provider turn asks to keep the exact saved fact. The action
    // must become visible for approval before browser storage changes.
    ASSERT_TRUE(base::test::RunUntil([&]() {
      const std::optional<ObservedTaskStatus> task =
          observer.task_other_than(source_task->task_id);
      return model_endpoint.request_count() == 1u && task &&
             task->pending_action_id.has_value();
    }));
    const std::optional<ObservedTaskStatus> library_pending =
        observer.task_other_than(source_task->task_id);
    ASSERT_TRUE(library_pending && library_pending->pending_action_id);
    EXPECT_EQ(0u, observer.library().revision);
    base::test::TestFuture<api::CoreApiSubmissionStatus> approve_library;
    remote->ApproveAction(library_pending->task_id,
                          *library_pending->pending_action_id,
                          approve_library.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approve_library.Get());
    ASSERT_TRUE(base::test::RunUntil([&]() {
      const std::optional<ObservedTaskStatus> task =
          observer.task_other_than(source_task->task_id);
      return observer.library().revision == 1u &&
             observer.library().entries.size() == 1u &&
             model_endpoint.request_count() == 2u && task &&
             task->pending_action_id &&
             task->pending_action_id != library_pending->pending_action_id;
    }));

    // The second provider turn proposes the Memory sentence. It too remains
    // absent until the distinct approval published by CoreStatus is returned.
    const std::optional<ObservedTaskStatus> memory_pending =
        observer.task_other_than(source_task->task_id);
    ASSERT_TRUE(memory_pending && memory_pending->pending_action_id);
    EXPECT_EQ(0u, observer.memory().revision);
    base::test::TestFuture<api::CoreApiSubmissionStatus> approve_memory;
    remote->ApproveAction(memory_pending->task_id,
                          *memory_pending->pending_action_id,
                          approve_memory.GetCallback());
    ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approve_memory.Get());
    ASSERT_TRUE(base::test::RunUntil([&]() {
      const std::optional<ObservedTaskStatus> task =
          observer.task_other_than(source_task->task_id);
      return observer.memory().revision == 1u &&
             observer.memory().records.size() == 1u &&
             model_endpoint.request_count() == 5u && task &&
             task->phase == api::TaskPhase::kCompleted;
    }));
    ASSERT_EQ(5u, model_endpoint.request_count());
    ASSERT_FALSE(model_endpoint.invalid_request_seen());

    const auto& kept = observer.library().entries.front();
    EXPECT_EQ(saved->workspace_id, kept.collection_id);
    EXPECT_EQ(saved->workspace_id, kept.source_workspace_id);
    EXPECT_EQ(saved->revision, kept.source_workspace_revision);
    EXPECT_EQ(fact.fact_id, kept.source_fact_id);
    EXPECT_EQ(fact.field, kept.field);
    EXPECT_EQ(fact.value, kept.original_value);
    EXPECT_EQ(fact.kind, kept.kind);
    ASSERT_FALSE(kept.sources.empty());
    EXPECT_EQ(saved->sources.front().source_id, kept.sources.front().source_id);
    EXPECT_EQ(source_url.host(), kept.sources.front().host);
    EXPECT_GT(kept.captured_at_epoch_ms, 0u);
    const std::string kept_entry_id = kept.entry_id;
    const auto& remembered = observer.memory().records.front();
    EXPECT_EQ(kMemoryStatement, remembered.statement);
    EXPECT_EQ(api::MemorySourceKind::kTaffySuggested, remembered.source_kind);
    ASSERT_TRUE(remembered.source_task_id);
    EXPECT_EQ(memory_pending->task_id, *remembered.source_task_id);
    ASSERT_TRUE(remembered.source_workspace);
    EXPECT_EQ(saved->workspace_id, remembered.source_workspace->workspace_id);
    EXPECT_EQ(saved->display_name, remembered.source_workspace->display_name);
    EXPECT_EQ(api::MemoryScopeKind::kAllTasks, remembered.scope_kind);
    EXPECT_FALSE(remembered.scope_workspace);
    EXPECT_EQ(api::MemorySensitivity::kStandard, remembered.sensitivity);
    EXPECT_GT(remembered.created_at_epoch_ms, 0u);
    const std::string remembered_id = remembered.memory_id;

    ASSERT_TRUE(test::RequestObservedLibrarySearch(
        &remote, &observer, "library-before-restart", library_query,
        observer.library().revision, kept_entry_id));
    ASSERT_TRUE(test::RequestObservedMemorySearch(
        &remote, &observer, "memory-before-restart", kMemoryQuery,
        observer.memory().revision, remembered_id));
    expected_library = observer.library();
    expected_memory = observer.memory();
    ASSERT_FALSE(observer.malformed_payload_seen());
    ASSERT_FALSE(observer.permission_request_seen());
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
  ASSERT_TRUE(expected_library && expected_memory);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return restored_observer.snapshot_seen() &&
           restored_observer.library().revision == expected_library->revision &&
           restored_observer.memory().revision == expected_memory->revision &&
           restored_observer.library().entries.size() ==
               expected_library->entries.size() &&
           restored_observer.memory().records.size() ==
               expected_memory->records.size();
  }));
  test::ExpectRestoredLibrary(*expected_library, restored_observer.library());
  test::ExpectRestoredMemory(*expected_memory, restored_observer.memory());
  EXPECT_FALSE(restored_observer.library().search);
  EXPECT_FALSE(restored_observer.memory().search);

  const std::string restored_entry_id =
      restored_observer.library().entries.front().entry_id;
  const std::string restored_memory_id =
      restored_observer.memory().records.front().memory_id;
  ASSERT_TRUE(test::RequestObservedLibrarySearch(
      &restored_remote, &restored_observer, "library-after-restart",
      library_query, restored_observer.library().revision, restored_entry_id));
  ASSERT_TRUE(test::RequestObservedMemorySearch(
      &restored_remote, &restored_observer, "memory-after-restart",
      kMemoryQuery, restored_observer.memory().revision, restored_memory_id));
  EXPECT_EQ(first_generation + 1u, restored_observer.service_generation());
  EXPECT_FALSE(restored_observer.malformed_payload_seen());
  EXPECT_FALSE(restored_observer.permission_request_seen());
}

}  // namespace
}  // namespace taffy
