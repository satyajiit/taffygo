// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
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
#include "taffy/browser/task_source_selection_registry.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "taffy/test/recovery/errand_download_test_page.h"
#include "taffy/test/recovery/errand_task_model_endpoint.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

// Tab creation and its embedder delegate replace Android's TabModel bridge.
// OpenURL is forwarded unchanged to Chromium's NavigationController. The
// fixture uses real renderers, ownership, policy, navigation and observation.
// Model replies are supplied exclusively at the network boundary.
class ErrandBootstrapVerticalBrowserTest : public PlatformBrowserTest,
                                           public TaskBrowserActionPlatform,
                                           public content::WebContentsDelegate {
 protected:
  void SetUpOnMainThread() override {
    PlatformBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    server_.RegisterRequestHandler(
        base::BindRepeating(&test::ServeErrandDownloadFixture));
    ASSERT_TRUE(server_.Start());
  }

  content::WebContents* person_tab() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  GURL url(const std::string& path) { return server_.GetURL("a.test", path); }

  void OpenTaskDiscoveryTab(const std::string& task_id,
                            const std::string& effect_id,
                            TaskDiscoveryTabCallback callback) override {
    ++created_tabs_;
    if (discovery_tab_) {
      ADD_FAILURE() << "Bootstrap attempted a second task tab";
      std::move(callback).Run(nullptr);
      return;
    }
    discovery_tab_ = content::WebContents::Create(
        content::WebContents::CreateParams(person_tab()->GetBrowserContext()));
    // Without an embedder delegate Chromium deliberately defers OpenURL on
    // a new WebContents. The real Android tab installs its delegate at
    // creation.
    discovery_tab_->SetDelegate(this);
    TaffyPageIntelligenceHost::AttachIfEligible(discovery_tab_.get());
    if (!core_->ClaimAssistantCreatedTaskTab(window_, task_id, effect_id,
                                             discovery_tab_.get()) ||
        !core_->RegisterTaskSourceTab(window_, 92, discovery_tab_.get()) ||
        !content::NavigateToURL(discovery_tab_.get(), GURL("about:blank"))) {
      ADD_FAILURE() << "Real task-owned blank document was not ready";
      std::move(callback).Run(nullptr);
      return;
    }
    EXPECT_TRUE(core_->SelectTaskSourceTab(window_, 92, discovery_tab_.get()));
    std::move(callback).Run(discovery_tab_.get());
  }

  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> callback) override {
    if (source != discovery_tab_.get() ||
        params.disposition != WindowOpenDisposition::CURRENT_TAB ||
        params.is_renderer_initiated) {
      ADD_FAILURE() << "Bootstrap delegate received an unrelated navigation";
      return nullptr;
    }
    ++open_url_calls_;
    auto navigation = source->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(params));
    if (navigation && callback) {
      std::move(callback).Run(*navigation);
    }
    return navigation ? source : nullptr;
  }

  std::optional<std::string> ResolveSearchAddress(const std::string&) override {
    ADD_FAILURE() << "A known address must not resolve a search";
    return std::nullopt;
  }

  bool StartSearch(content::WebContents*,
                   const std::string&,
                   const std::string&) override {
    ADD_FAILURE() << "A known address must not start a search";
    return false;
  }

  content::WebContents* OpenTaskTab(const std::string&,
                                    const std::string&,
                                    const std::string&) override {
    ADD_FAILURE() << "Bootstrap must use its one dedicated blank tab";
    return nullptr;
  }

  bool ActivateTaskTab(content::WebContents*) override {
    ADD_FAILURE() << "This task does not request a tab switch";
    return false;
  }

  bool CloseTaskTab(content::WebContents* tab) override {
    if (tab != discovery_tab_.get()) {
      ADD_FAILURE() << "Task cleanup named a different tab";
      return false;
    }
    core_->UnregisterTaskSourceTab(window_, 92);
    discovery_tab_.reset();
    return true;
  }

  test::EmptyVaultProfilePlatformAdapter platform_adapter_;
  raw_ptr<CoreServiceManager> core_ = nullptr;
  uint64_t window_ = 0u;
  uint32_t created_tabs_ = 0u;
  uint32_t open_url_calls_ = 0u;
  std::unique_ptr<content::WebContents> discovery_tab_;

 private:
  net::EmbeddedTestServer server_{net::EmbeddedTestServer::TYPE_HTTPS};
};

IN_PROC_BROWSER_TEST_F(ErrandBootstrapVerticalBrowserTest,
                       KnownAddressStartsWithoutSourceAndReachesHandover) {
  ASSERT_TRUE(content::NavigateToURL(person_tab(), url("/start")));
  auto* profile =
      Profile::FromBrowserContext(person_tab()->GetBrowserContext());
  core_ = CoreServiceManagerFactory::GetForProfile(profile);
  ASSERT_TRUE(core_);
  core_->BindPlatformAdapter(platform_adapter_.BindNewPipeAndPassRemote());
  base::test::TestFuture<bool> prepared;
  core_->PrepareForCoreApi(prepared.GetCallback());
  ASSERT_TRUE(prepared.Get());
  window_ = core_->RegisterTaskSourceWindow();
  ASSERT_NE(0u, window_);
  base::ScopedClosureRunner release_window(base::BindLambdaForTesting([this] {
    core_->UnbindTaskBrowserActionPlatform(window_, this);
    core_->DeactivateTaskSourceWindow(window_);
    core_->UnregisterTaskSourceWindow(window_);
    discovery_tab_.reset();
    core_ = nullptr;
  }));
  ASSERT_TRUE(core_->BindTaskBrowserActionPlatform(window_, this));
  ASSERT_TRUE(core_->RegisterTaskSourceTab(window_, 91, person_tab()));
  ASSERT_TRUE(core_->ActivateTaskSourceWindow(window_));
  // No selected tab and no requested source hosts: the existing page cannot
  // become task input. The real core must prepare its own opaque document.
  ProfileCoreApiFacade facade(core_);
  mojo::Remote<api::TaffyProfileCoreApi> remote;
  mojo::Receiver<api::TaffyProfileCoreApi> receiver(
      &facade, remote.BindNewPipeAndPassReceiver());
  test::CoreApiStatusObserver observer;
  remote->Observe(observer.BindNewPipeAndPassRemote());
  ASSERT_TRUE(base::test::RunUntil([&] { return observer.snapshot_seen(); }));
  test::ErrandTaskModelEndpoint endpoint(url("/download-document").spec());
  std::vector<api::CustomModelSpecViewPtr> models;
  models.push_back(api::CustomModelSpecView::New(
      "errand-model", "Errand test model", 16'384u, 2'048u, true, true));
  base::test::TestFuture<api::CoreApiSubmissionStatus> provider;
  remote->SaveCustomProvider(
      "errand-test", "Errand test provider", endpoint.base_url(),
      api::ProviderWireApiView::kOpenAiCompletions, std::nullopt,
      std::move(models), api::DetectedServerViewPtr(), provider.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, provider.Get());
  base::test::TestFuture<api::CoreApiSubmissionStatus> choose;
  remote->SetProviderModelPreference(
      "errand-test", std::optional<std::string>("errand-model"),
      api::ThinkingPreferenceView::New(api::ThinkingLevelView::kMedium),
      choose.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, choose.Get());
  auto consent = api::TaskConsentPreview::New();
  consent->source_discovery_enabled = true;
  consent->new_source_cap = 1u;
  consent->provider_route = api::TaskProviderRoute::kDirectUserKey;
  base::test::TestFuture<api::CoreApiSubmissionStatus> start;
  remote->StartTask("Download my document; I will enter the verification code",
                    api::TaskTemplateId::kWebErrand, std::nullopt,
                    std::move(consent), std::nullopt, start.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto task = observer.only_task();
    return endpoint.invalid_request_seen() ||
           observer.availability() != api::CoreAvailability::kReady ||
           (task && (task->waiting_for_handover ||
                     task->phase == api::TaskPhase::kFailed ||
                     task->phase == api::TaskPhase::kOutcomeUnknown ||
                     task->pending_action_id));
  })) << "model_requests="
      << endpoint.request_count() << " created_tabs=" << created_tabs_
      << " open_url_calls=" << open_url_calls_
      << " state_sequence=" << observer.state_sequence() << " task_revision="
      << (observer.only_task() ? observer.only_task()->revision : 0u)
      << " task_phase="
      << (observer.only_task() ? static_cast<int>(observer.only_task()->phase)
                               : -1)
      << " still_blank="
      << (discovery_tab_ &&
          discovery_tab_->GetLastCommittedURL() == GURL("about:blank"));
  ASSERT_EQ(api::CoreAvailability::kReady, observer.availability())
      << "model_requests=" << endpoint.request_count()
      << " created_tabs=" << created_tabs_
      << " open_url_calls=" << open_url_calls_
      << " manager_availability=" << static_cast<int>(core_->availability())
      << " generation=" << observer.service_generation()
      << " state_sequence=" << observer.state_sequence()
      << " malformed_payload=" << observer.malformed_payload_seen()
      << " task_revision="
      << (observer.only_task() ? observer.only_task()->revision : 0u)
      << " task_phase="
      << (observer.only_task() ? static_cast<int>(observer.only_task()->phase)
                               : -1);
  ASSERT_FALSE(observer.malformed_payload_seen());
  ASSERT_FALSE(endpoint.invalid_request_seen());
  auto task = observer.only_task();
  ASSERT_TRUE(task);
  ASSERT_TRUE(task->waiting_for_handover)
      << "phase=" << static_cast<int>(task->phase) << " failure_code="
      << (task->failure_code ? static_cast<int>(*task->failure_code) : -1)
      << " model_requests=" << endpoint.request_count()
      << " created_tabs=" << created_tabs_
      << " open_url_calls=" << open_url_calls_
      << " pending_approval=" << task->pending_action_id.has_value()
      << " state_sequence=" << observer.state_sequence()
      << " task_revision=" << task->revision << " landed="
      << (discovery_tab_ &&
          discovery_tab_->GetLastCommittedURL() == url("/download-document"));
  EXPECT_EQ(api::TaskPhase::kWaitingForUser, task->phase);
  EXPECT_EQ(2u, endpoint.request_count());
  EXPECT_EQ(1u, created_tabs_);
  EXPECT_EQ(1u, open_url_calls_);
  ASSERT_TRUE(discovery_tab_);
  EXPECT_EQ(url("/download-document"), discovery_tab_->GetLastCommittedURL());
  EXPECT_EQ(url("/start"), person_tab()->GetLastCommittedURL());
  // This view comes from the published, accepted standing source consent,
  // after verified landing and the durable source-cap spend. Creating a tab
  // or merely navigating it does not make this membership true.
  EXPECT_TRUE(
      core_->IsTaskSourceTabForDisplay(task->task_id, discovery_tab_.get()));
  EXPECT_FALSE(core_->IsTaskSourceTabForDisplay(task->task_id, person_tab()));
  auto* host = TaffyPageIntelligenceHost::FromWebContents(discovery_tab_.get());
  ASSERT_TRUE(host);
  EXPECT_GT(host->observations_submitted(), 0u);
  // Stop at the person's handover. Download and handback behavior are proved
  // separately by ErrandDownloadVerticalBrowserTest with this same endpoint.
  base::test::TestFuture<api::CoreApiSubmissionStatus> cancel;
  remote->CancelTask(task->task_id, cancel.GetCallback());
  EXPECT_EQ(api::CoreApiSubmissionStatus::kAccepted, cancel.Get());
  EXPECT_TRUE(base::test::RunUntil([&] {
    const auto current = observer.only_task();
    return current && current->phase == api::TaskPhase::kCancelled;
  }));
}

}  // namespace
}  // namespace taffy
