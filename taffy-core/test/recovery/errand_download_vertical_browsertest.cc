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

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
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
#include "taffy/browser/task_source_selection_registry.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "taffy/test/recovery/errand_download_test_page.h"
#include "taffy/test/recovery/errand_task_model_endpoint.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
constexpr char kDownloadGoal[] =
    "Download my document; I will enter the verification code";

// This fixture covers real native actions on one selected page. Android
// TabModel creation, search and tab controls are outside its scope; reaching
// any of those platform methods fails the test instead of simulating success.
class ErrandDownloadVerticalBrowserTest : public PlatformBrowserTest,
                                          public TaskBrowserActionPlatform {
 protected:
  void SetUpOnMainThread() override {
    PlatformBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    server_.RegisterRequestHandler(
        base::BindRepeating(&test::ServeErrandDownloadFixture));
    ASSERT_TRUE(server_.Start());
    Profile* profile = Profile::FromBrowserContext(tab()->GetBrowserContext());
    ASSERT_NO_FATAL_FAILURE(
        test::ConfigureErrandDownloadDirectory(*profile, download_directory_));
  }

  content::WebContents* tab() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  GURL url(const std::string& path) { return server_.GetURL("a.test", path); }

  download::DownloadItem* only_download() {
    download::SimpleDownloadManager::DownloadVector downloads;
    tab()->GetBrowserContext()->GetDownloadManager()->GetAllDownloads(
        &downloads);
    return downloads.size() == 1u ? downloads.front() : nullptr;
  }

  std::optional<std::string> ResolveSearchAddress(const std::string&) override {
    ADD_FAILURE() << "Selected-page fixture cannot resolve a search";
    return std::nullopt;
  }

  content::WebContents* OpenTaskTab(const std::string&,
                                    const std::string&,
                                    const std::string&) override {
    ADD_FAILURE() << "Selected-page fixture cannot create a task tab";
    return nullptr;
  }

  void OpenTaskDiscoveryTab(const std::string&,
                            const std::string&,
                            TaskDiscoveryTabCallback callback) override {
    ADD_FAILURE() << "Selected-page fixture cannot create a discovery tab";
    std::move(callback).Run(nullptr);
  }

  bool StartSearch(content::WebContents*,
                   const std::string&,
                   const std::string&) override {
    ADD_FAILURE() << "Selected-page fixture cannot start a search";
    return false;
  }

  bool ActivateTaskTab(content::WebContents*) override {
    ADD_FAILURE() << "Selected-page fixture cannot switch tabs";
    return false;
  }

  bool CloseTaskTab(content::WebContents*) override {
    ADD_FAILURE() << "Selected-page fixture cannot close tabs";
    return false;
  }

  test::EmptyVaultProfilePlatformAdapter platform_adapter_;

 private:
  net::EmbeddedTestServer server_{net::EmbeddedTestServer::TYPE_HTTPS};
  base::ScopedTempDir download_directory_;
};

IN_PROC_BROWSER_TEST_F(
    ErrandDownloadVerticalBrowserTest,
    DownloadIsReviewedAndReplayedAfterCoreRestartWithoutModel) {
  ASSERT_TRUE(content::NavigateToURL(tab(), url("/start")));
  auto* profile = Profile::FromBrowserContext(tab()->GetBrowserContext());
  auto* core = CoreServiceManagerFactory::GetForProfile(profile);
  ASSERT_TRUE(core);
  core->BindPlatformAdapter(platform_adapter_.BindNewPipeAndPassRemote());
  base::test::TestFuture<bool> prepared;
  core->PrepareForCoreApi(prepared.GetCallback());
  ASSERT_TRUE(prepared.Get());
  const uint64_t window = core->RegisterTaskSourceWindow();
  ASSERT_NE(0u, window);
  base::ScopedClosureRunner release_window(base::BindOnce(
      [](CoreServiceManager* manager, uint64_t token,
         TaskBrowserActionPlatform* platform) {
        manager->UnbindTaskBrowserActionPlatform(token, platform);
        manager->DeactivateTaskSourceWindow(token);
        manager->UnregisterTaskSourceWindow(token);
      },
      base::Unretained(core), window,
      base::Unretained(static_cast<TaskBrowserActionPlatform*>(this))));
  ASSERT_TRUE(core->BindTaskBrowserActionPlatform(window, this));
  ASSERT_TRUE(core->RegisterTaskSourceTab(window, 71, tab()));
  ASSERT_TRUE(core->SelectTaskSourceTab(window, 71, tab()));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(window));

  ProfileCoreApiFacade facade(core);
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
  consent->source_hosts = {"a.test"};
  consent->source_discovery_enabled = true;
  consent->new_source_cap = 1u;
  consent->provider_route = api::TaskProviderRoute::kDirectUserKey;
  base::test::TestFuture<api::CoreApiSubmissionStatus> start;
  remote->StartTask(kDownloadGoal, api::TaskTemplateId::kWebErrand,
                    std::nullopt, std::move(consent), std::nullopt,
                    start.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start.Get());

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto task = observer.only_task();
    return observer.availability() != api::CoreAvailability::kReady ||
           observer.malformed_payload_seen() ||
           (task && (task->waiting_for_handover || task->pending_action_id ||
                     task->phase == api::TaskPhase::kFailed));
  })) << "model_requests="
      << endpoint.request_count()
      << " invalid_model_request=" << endpoint.invalid_request_seen()
      << " permission_request=" << observer.permission_request_seen()
      << " availability=" << static_cast<int>(observer.availability())
      << " sequence=" << observer.state_sequence() << " phase="
      << (observer.only_task() ? static_cast<int>(observer.only_task()->phase)
                               : -1);
  ASSERT_EQ(api::CoreAvailability::kReady, observer.availability())
      << "model requests=" << endpoint.request_count()
      << ", malformed=" << observer.malformed_payload_seen();
  auto task = observer.only_task();
  ASSERT_TRUE(task);
  ASSERT_TRUE(task->waiting_for_handover) << static_cast<int>(task->phase);
  EXPECT_EQ(api::TaskPhase::kWaitingForUser, task->phase);
  EXPECT_EQ(2u, endpoint.request_count());
  EXPECT_EQ(url("/download-document"), tab()->GetLastCommittedURL());
  EXPECT_EQ(nullptr, only_download());
  const std::string task_id = task->task_id;

  // This is the person's side of the fixture. The assistant receives neither
  // the input value nor the private query argument in its next observation.
  ASSERT_TRUE(content::ExecJs(tab(), R"JS(
    document.getElementById('code').value = 'private-fixture-entry-42';
    document.getElementById('verify').click();
  )JS"));
  base::test::TestFuture<api::CoreApiSubmissionStatus> handback;
  remote->CompleteHandover(task_id, handback.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, handback.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto current = observer.only_task();
    return endpoint.invalid_request_seen() ||
           observer.availability() != api::CoreAvailability::kReady ||
           (current && (current->phase == api::TaskPhase::kCompleted ||
                        current->phase == api::TaskPhase::kPartial ||
                        current->phase == api::TaskPhase::kFailed ||
                        current->pending_action_id));
  }));
  task = observer.only_task();
  ASSERT_TRUE(task);
  ASSERT_TRUE(task->pending_action_id)
      << "phase=" << static_cast<int>(task->phase) << " failure="
      << (task->failure_code ? static_cast<int>(*task->failure_code) : -1)
      << " revision=" << task->revision
      << " model_requests=" << endpoint.request_count()
      << " invalid_model_request=" << endpoint.invalid_request_seen()
      << " private_input_seen=" << endpoint.private_input_seen();
  EXPECT_EQ(3u, endpoint.request_count());
  // The fresh link's private address has resolved through the real browser
  // registry, but only the person's next action may start its download.
  EXPECT_FALSE(endpoint.private_input_seen());
  EXPECT_EQ(nullptr, only_download());
  base::test::TestFuture<api::CoreApiSubmissionStatus> approve_download;
  remote->ApproveAction(task_id, *task->pending_action_id,
                        approve_download.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approve_download.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto current = observer.only_task();
    return endpoint.invalid_request_seen() ||
           observer.availability() != api::CoreAvailability::kReady ||
           (current && (current->phase == api::TaskPhase::kCompleted ||
                        current->phase == api::TaskPhase::kPartial ||
                        current->phase == api::TaskPhase::kFailed));
  })) << "model_requests="
      << endpoint.request_count();
  task = observer.only_task();
  ASSERT_TRUE(task);
  ASSERT_EQ(api::TaskPhase::kCompleted, task->phase)
      << test::ErrandDownloadDiagnostic(only_download())
      << " model_requests=" << endpoint.request_count()
      << " invalid_model_request=" << endpoint.invalid_request_seen();
  const uint32_t first_task_model_count = endpoint.request_count();
  ASSERT_GE(first_task_model_count, 5u);
  ASSERT_LE(first_task_model_count,
            test::ErrandTaskModelEndpoint::kMaxRequests);
  EXPECT_FALSE(endpoint.invalid_request_seen());
  EXPECT_FALSE(endpoint.private_input_seen());
  auto* file = only_download();
  ASSERT_TRUE(file);
  ASSERT_NO_FATAL_FAILURE(test::ExpectErrandFixturePdf(*file));
  EXPECT_TRUE(core->CanOpenTaskDownloadForPerson(task_id, file->GetGuid()));
  EXPECT_FALSE(
      core->CanOpenTaskDownloadForPerson("unrelated-task", file->GetGuid()));
  EXPECT_FALSE(observer.malformed_payload_seen());

  ASSERT_TRUE(base::test::RunUntil([&] {
    return observer.availability() != api::CoreAvailability::kReady ||
           observer.malformed_payload_seen() ||
           (observer.site_skills_complete() &&
            observer.site_skills().size() == 1u);
  })) << "availability="
      << static_cast<int>(observer.availability())
      << " malformed=" << observer.malformed_payload_seen()
      << " sequence=" << observer.state_sequence()
      << " skills_complete=" << observer.site_skills_complete()
      << " skills=" << observer.site_skills().size();
  ASSERT_EQ(api::CoreAvailability::kReady, observer.availability());
  ASSERT_EQ(1u, observer.site_skills().size());
  const auto draft = observer.site_skills().front();
  ASSERT_TRUE(draft.review_complete);
  EXPECT_EQ(api::SiteSkillStatusView::kDraft, draft.status);
  EXPECT_EQ(api::SiteSkillProvenanceView::kRecordedFromTask, draft.provenance);
  EXPECT_EQ(task_id, draft.recorded_from_task_id);
  EXPECT_EQ(url("/download-document").spec(), draft.starting_address);
  std::vector<std::string> verbs;
  size_t reads = 0u;
  for (const auto& step : draft.reviewed_steps) {
    // The review retains automatic fresh-page reads too; only the assertion
    // groups those reads to compare the ordered consequential steps.
    if (step.verb == "browser.dom.read") {
      ++reads;
    } else {
      verbs.push_back(step.verb);
    }
    EXPECT_FALSE(step.has_fill);
    for (const auto& argument : step.arguments) {
      if (argument.public_address) {
        EXPECT_EQ(std::string::npos,
                  argument.public_address->find("private-fixture-entry-42"));
      }
    }
  }
  EXPECT_GE(reads, 2u);
  EXPECT_EQ(reads + verbs.size(), draft.step_count);
  ASSERT_EQ(first_task_model_count - 1u, verbs.size());
  EXPECT_EQ("browser.navigate", verbs[0]);
  EXPECT_EQ("user.handover", verbs[1]);
  EXPECT_EQ("browser.download.from_link", verbs[2]);
  for (size_t index = 3u; index < verbs.size(); ++index) {
    EXPECT_EQ("browser.download.list", verbs[index]);
  }
  base::test::TestFuture<api::SavedFlowQueryResultPtr> review;
  remote->GetSavedFlowReview("review-download", draft.skill_id,
                             draft.active_version, review.GetCallback());
  auto reviewed = review.Take();
  ASSERT_TRUE(reviewed);
  ASSERT_EQ(api::SavedFlowQueryAvailability::kAvailable,
            reviewed->availability);
  ASSERT_EQ(1u, reviewed->flows.size());
  EXPECT_EQ(draft.step_count, reviewed->flows.front()->reviewed_steps.size());
  EXPECT_EQ(draft.active_version, reviewed->flows.front()->active_version);
  // Acceptance returns the immutable version the person reviewed, exactly as
  // the Compose review does; the fixture never inserts a saved definition.
  base::test::TestFuture<api::CoreApiSubmissionStatus> accept;
  remote->MutateSiteSkill(api::SiteSkillMutationKind::kSetEnabled,
                          draft.skill_id, draft.active_version, "", {}, {}, 0u,
                          true, accept.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, accept.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return observer.site_skills().size() == 1u &&
           observer.site_skills().front().status ==
               api::SiteSkillStatusView::kActive;
  }));
  ASSERT_NO_FATAL_FAILURE(test::RestartErrandCore(*core, observer));
  ASSERT_TRUE(content::NavigateToURL(tab(), url("/start")));
  base::test::TestFuture<api::SavedFlowQueryResultPtr> find;
  remote->FindSavedFlows("repeat-download", kDownloadGoal, find.GetCallback());
  auto found = find.Take();
  ASSERT_TRUE(found);
  EXPECT_EQ("repeat-download", found->request_id);
  EXPECT_EQ(core->service_generation(), found->service_generation);
  ASSERT_EQ(api::SavedFlowQueryAvailability::kAvailable, found->availability);
  ASSERT_EQ(1u, found->flows.size());
  EXPECT_EQ(draft.skill_id, found->flows.front()->skill_id);
  EXPECT_EQ(draft.active_version, found->flows.front()->active_version);
  base::test::TestFuture<api::CoreApiSubmissionStatus> open_start;
  remote->OpenSavedFlowStart("open-download-start", draft.skill_id,
                             draft.active_version, open_start.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, open_start.Get());
  EXPECT_EQ(url("/download-document"), tab()->GetLastCommittedURL());
  EXPECT_TRUE(observer.only_task());
  // Opening acknowledges the exact navigation commit. Request a fresh offer
  // only after that real document finishes loading.
  ASSERT_TRUE(content::WaitForLoadStop(tab()));
  base::test::TestFuture<api::PageInspectorSnapshotResultPtr> inspect;
  core->ObservePageForInspector(tab(), inspect.GetCallback());
  auto page = inspect.Take();
  ASSERT_TRUE(page);
  ASSERT_EQ(api::PageInspectorAvailability::kAvailable, page->availability);
  ASSERT_TRUE(page->snapshot);
  ASSERT_EQ(1u, page->snapshot->site_skill_offers.size());
  const auto& offer = page->snapshot->site_skill_offers.front();
  ASSERT_TRUE(offer);
  auto replay_consent = api::TaskConsentPreview::New();
  replay_consent->source_hosts = {"a.test"};
  replay_consent->source_discovery_enabled = false;
  replay_consent->new_source_cap = 0u;
  replay_consent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  base::test::TestFuture<api::CoreApiSubmissionStatus> replay;
  remote->StartTask("Download my document again",
                    api::TaskTemplateId::kWebErrand, std::nullopt,
                    std::move(replay_consent), offer->offer_id,
                    replay.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, replay.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto current = observer.task_other_than(task_id);
    return current && (current->waiting_for_handover ||
                       current->phase == api::TaskPhase::kFailed);
  }));
  const auto waiting = observer.task_other_than(task_id);
  ASSERT_TRUE(waiting && waiting->waiting_for_handover);
  EXPECT_EQ(first_task_model_count, endpoint.request_count());
  ASSERT_TRUE(content::ExecJs(tab(), R"JS(
    document.getElementById('code').value = 'private-fixture-entry-42';
    document.getElementById('verify').click();
  )JS"));
  base::test::TestFuture<api::CoreApiSubmissionStatus> return_replay;
  remote->CompleteHandover(waiting->task_id, return_replay.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, return_replay.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto current = observer.task_other_than(task_id);
    return current && (current->pending_action_id ||
                       current->phase == api::TaskPhase::kFailed);
  }));
  const auto pending_replay = observer.task_other_than(task_id);
  ASSERT_TRUE(pending_replay && pending_replay->pending_action_id);
  base::test::TestFuture<api::CoreApiSubmissionStatus> approve_replay;
  remote->ApproveAction(waiting->task_id, *pending_replay->pending_action_id,
                        approve_replay.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, approve_replay.Get());
  ASSERT_NO_FATAL_FAILURE(test::WaitForErrandReplayCompletion(
      observer, task_id, profile->GetDownloadManager(), endpoint,
      first_task_model_count));
  EXPECT_EQ(first_task_model_count, endpoint.request_count());
  EXPECT_FALSE(endpoint.invalid_request_seen());
  ASSERT_NO_FATAL_FAILURE(test::ExpectErrandTaskDownloads(
      *core, *profile->GetDownloadManager(), task_id, waiting->task_id));
  ASSERT_NO_FATAL_FAILURE(
      test::MaybeOpenErrandTaskPdf(*tab(), *core, waiting->task_id));
  ASSERT_NO_FATAL_FAILURE(test::DisableErrandFlowAndRestart(
      *core, *remote.get(), observer, task_id));
}

}  // namespace
}  // namespace taffy
