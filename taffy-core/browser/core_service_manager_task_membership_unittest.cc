// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

class CoreServiceManagerTaskMembershipTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://first.example/product"));
    auto tools = std::make_unique<ProfileToolSupervisor>(
        1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        browser_context(), nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(browser_context()),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    CoreServiceManagerTaskEffectTestPeer::BeginBrowserAuthorityGeneration(
        *manager_, "profile-1");
    Observe(web_contents());
    window_ = manager_->RegisterTaskSourceWindow();
    ASSERT_NE(window_, 0u);
    ASSERT_TRUE(manager_->RegisterTaskSourceTab(window_, 1, web_contents()));
    ASSERT_TRUE(manager_->ActivateTaskSourceWindow(window_));
    auto empty = service::CoreStateBrowserBindings::New();
    empty->service_generation = 1u;
    empty->state_sequence = 1u;
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, std::move(empty)),
              service::PendingApprovalRegistrationStatus::kRegistered);
    auto state = service::CoreStateUpdate::New();
    state->service_generation = 1u;
    state->sequence = 1u;
    state->core_status_schema_version = 1u;
    state->payload = {1u};
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, std::move(state));
  }

  void TearDown() override {
    DeleteContents();
    manager_->Shutdown();
    manager_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  void Observe(content::WebContents* contents) {
    TaffyPageIntelligenceHost::AttachWithAuthority(
        contents, manager_->actor_leases(), manager_->capabilities(),
        manager_->value_references());
  }

  service::CoreServiceCommandPtr StartComparison() {
    auto intent = api::TaskConsentPreview::New();
    intent->source_hosts = {"first.example", "second.example"};
    intent->provider_route = api::TaskProviderRoute::kDirectUserKey;
    auto preview = manager_->ResolveStartTaskConsent(
        api::TaskTemplateId::kCompareProducts, *intent);
    EXPECT_TRUE(preview);
    auto command = service::CoreServiceCommand::New();
    command->operation = service::OperationEnvelope::New(
        "start-comparison", 1u, 0u, Now() + 30'000u, "comparison-key");
    command->kind = service::CoreServiceCommandKind::kStartTask;
    command->start_task = service::StartTaskCommand::New();
    auto& start = *command->start_task;
    start.task_id = "task-comparison";
    start.browser_profile_id = "profile-1";
    start.browser_session_id = manager_->browser_session_id();
    start.template_id = service::TaskTemplateId::kCompareProducts;
    start.provider_route_id = "direct_user_key";
    start.tool_allowlist = {"browser.dom.read"};
    start.initial_consent_receipt_id = "comparison-consent";
    start.consent_preview = preview ? std::move(*preview) : nullptr;
    return command;
  }

  service::CoreStateBrowserBindingsPtr Accepted(
      const service::CoreServiceCommand& command) {
    auto bindings = service::CoreStateBrowserBindings::New();
    bindings->service_generation = 1u;
    bindings->state_sequence = 2u;
    bindings->task_revisions.push_back(service::TaskRevisionBinding::New(
        "task-comparison", 1u, 3u, std::vector<service::TaskControlKind>()));
    auto consent = service::AcceptedTaskConsentBinding::New();
    consent->task_id = "task-comparison";
    consent->service_generation = 1u;
    consent->current_task_revision = 3u;
    consent->accepted_revision = 3u;
    consent->browser_session_id = manager_->browser_session_id();
    consent->receipt_id = "comparison-consent";
    consent->consent_preview = command.start_task->consent_preview.Clone();
    bindings->accepted_task_consents.push_back(std::move(consent));
    return bindings;
  }

  uint64_t Now() {
    return static_cast<uint64_t>(
        base::TimeTicks::Now().since_origin().InMilliseconds());
  }

  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<service::CoreSession> session_;
  uint64_t window_ = 0u;
};

TEST_F(CoreServiceManagerTaskMembershipTest,
       SelectedPersonPagesNeedExactCommittedConsentAndRemainPersonOwned) {
  auto second = CreateTestWebContents();
  content::WebContentsTester::For(second.get())
      ->NavigateAndCommit(GURL("https://second.example/product"));
  Observe(second.get());
  ASSERT_TRUE(manager_->RegisterTaskSourceTab(window_, 2, second.get()));
  auto start = StartComparison();
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::StageSubmittedCommand(
                *manager_, *start, Now(),
                static_cast<uint64_t>(
                    base::Time::Now().InMillisecondsSinceUnixEpoch())),
            AuthoritySubmissionStage::kStaged);
  EXPECT_FALSE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", web_contents()));
  ASSERT_TRUE(CoreServiceManagerTaskEffectTestPeer::CompleteStagedStorageCommit(
      *manager_, *start, "task-comparison", 3u, Now()));
  EXPECT_FALSE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", second.get()));
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager_, Accepted(*start)),
            service::PendingApprovalRegistrationStatus::kRegistered);
  EXPECT_TRUE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", web_contents()));
  EXPECT_TRUE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", second.get()));
  EXPECT_FALSE(
      manager_->IsTaskSourceTabForDisplay("another-task", second.get()));
  EXPECT_TRUE(
      TaskSourceSelectionRegistry::BrowserOwnedTaskId(web_contents()).empty());
  EXPECT_TRUE(
      TaskSourceSelectionRegistry::BrowserOwnedTaskId(second.get()).empty());

  auto unrelated = CreateTestWebContents();
  content::WebContentsTester::For(unrelated.get())
      ->NavigateAndCommit(GURL("https://first.example/product"));
  Observe(unrelated.get());
  ASSERT_TRUE(manager_->RegisterTaskSourceTab(window_, 3, unrelated.get()));
  EXPECT_FALSE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", unrelated.get()));

  content::WebContentsTester::For(second.get())
      ->NavigateAndCommit(GURL("https://second.example/another-product"));
  EXPECT_FALSE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", second.get()));
  EXPECT_TRUE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", web_contents()));
  manager_->UnregisterTaskSourceTab(window_, 1);
  EXPECT_FALSE(
      manager_->IsTaskSourceTabForDisplay("task-comparison", web_contents()));
}

}  // namespace
}  // namespace taffy
