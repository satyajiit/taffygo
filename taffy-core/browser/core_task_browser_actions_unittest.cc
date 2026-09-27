// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_browser_actions.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/test/bind.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

class TestCoreTaskBrowserActions final : public CoreTaskBrowserActions {
 public:
  explicit TestCoreTaskBrowserActions(content::BrowserContext* context)
      : CoreTaskBrowserActions(context) {}

  void set_available(bool available) { available_ = available; }
  int activations() const { return activations_; }

 private:
  bool IsTaskBrowserAvailable() const override { return available_; }
  void OnTaskSourceWindowActivated() override { ++activations_; }

  bool available_ = true;
  int activations_ = 0;
};

class FakeTaskBrowserPlatform final : public TaskBrowserActionPlatform {
 public:
  FakeTaskBrowserPlatform(TestCoreTaskBrowserActions* actions,
                          uint64_t window_token)
      : actions_(actions), window_token_(window_token) {}

  std::optional<std::string> ResolveSearchAddress(
      const std::string& query) override {
    ++resolve_calls;
    return resolved_search_address;
  }

  content::WebContents* OpenTaskTab(
      const std::string& task_id,
      const std::string& action_id,
      const std::string& destination_address) override {
    ++open_calls;
    if (!returned_tab) {
      return nullptr;
    }
    if (claim_returned_tab &&
        !actions_->ClaimAssistantCreatedTaskTab(window_token_, task_id,
                                                action_id, returned_tab)) {
      return nullptr;
    }
    if (publish_returned_tab &&
        !actions_->RegisterTaskSourceTab(window_token_, next_tab_id++,
                                         returned_tab)) {
      return nullptr;
    }
    return returned_tab;
  }

  void OpenTaskDiscoveryTab(const std::string& task_id,
                            const std::string& effect_id,
                            TaskDiscoveryTabCallback callback) override {
    ++discovery_open_calls;
    if (returned_tab && claim_returned_tab &&
        !actions_->ClaimAssistantCreatedTaskTab(window_token_, task_id,
                                                effect_id, returned_tab)) {
      std::move(callback).Run(nullptr);
      return;
    }
    if (returned_tab && publish_returned_tab &&
        !actions_->RegisterTaskSourceTab(window_token_, next_tab_id++,
                                         returned_tab)) {
      std::move(callback).Run(nullptr);
      return;
    }
    std::move(callback).Run(returned_tab);
  }

  bool StartSearch(content::WebContents* web_contents,
                   const std::string& query,
                   const std::string& destination_address) override {
    ++search_calls;
    searched_tab = web_contents;
    searched_query = query;
    searched_address = destination_address;
    return search_succeeds;
  }

  bool ActivateTaskTab(content::WebContents* web_contents) override {
    return false;
  }

  bool CloseTaskTab(content::WebContents* web_contents) override {
    ++close_calls;
    closed_tab = web_contents;
    return close_succeeds;
  }

  raw_ptr<content::WebContents> returned_tab = nullptr;
  std::optional<std::string> resolved_search_address;
  bool claim_returned_tab = true;
  bool publish_returned_tab = true;
  bool search_succeeds = true;
  bool close_succeeds = true;
  int open_calls = 0;
  int discovery_open_calls = 0;
  int resolve_calls = 0;
  int search_calls = 0;
  int close_calls = 0;
  raw_ptr<content::WebContents> searched_tab = nullptr;
  raw_ptr<content::WebContents> closed_tab = nullptr;
  std::string searched_query;
  std::string searched_address;

 private:
  const raw_ptr<TestCoreTaskBrowserActions> actions_;
  const uint64_t window_token_;
  int next_tab_id = 100;
};

class CoreTaskBrowserActionsTest : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://opener.example/"));
    actions_ = std::make_unique<TestCoreTaskBrowserActions>(browser_context());
    TaffyPageIntelligenceHost::AttachWithAuthority(
        web_contents(), &host_leases_, &host_capabilities_, &host_values_,
        actions_.get());
    TaffyPageIntelligenceHost* host =
        TaffyPageIntelligenceHost::FromWebContents(web_contents());
    ASSERT_TRUE(host);
    const std::optional<DirectObservationContext> context =
        host->BuildDirectObservationContext();
    ASSERT_TRUE(context);
    opener_tab_id_ = TabId{context->tab_id};

    window_token_ = actions_->RegisterTaskSourceWindow();
    ASSERT_NE(window_token_, 0u);
    platform_ = std::make_unique<FakeTaskBrowserPlatform>(actions_.get(),
                                                          window_token_);
    ASSERT_TRUE(actions_->BindTaskBrowserActionPlatform(window_token_,
                                                        platform_.get()));
    ASSERT_TRUE(
        actions_->RegisterTaskSourceTab(window_token_, 17, web_contents()));
  }

  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;
  std::unique_ptr<TestCoreTaskBrowserActions> actions_;
  std::unique_ptr<FakeTaskBrowserPlatform> platform_;
  uint64_t window_token_ = 0u;
  TabId opener_tab_id_;
};

TEST_F(CoreTaskBrowserActionsTest,
       OpenSearchAndReleaseStayBoundToExactTaskAndTab) {
  auto created = CreateTestWebContents();
  ASSERT_TRUE(created);
  platform_->returned_tab = created.get();
  const BrowserActionStart opened = actions_->OpenTaskTab(
      TaskId{"task-a"}, ActionId{"action-open"}, opener_tab_id_, web_contents(),
      "https://destination.example/path");
  ASSERT_TRUE(opened.started);
  EXPECT_EQ(opened.created_web_contents, created.get());
  content::WebContentsTester::For(created.get())
      ->NavigateAndCommit(GURL("https://destination.example/path"));
  TaffyPageIntelligenceHost::AttachWithAuthority(created.get(), &host_leases_,
                                                 &host_capabilities_,
                                                 &host_values_, actions_.get());
  auto discovered =
      actions_->IssueDiscoveredTaskSourceForAction("task-a", "action-open");
  ASSERT_TRUE(discovered.has_value());
  EXPECT_EQ((*discovered)->normalized_origin, "https://destination.example");
  auto same_source = actions_->IssueDiscoveredTaskSourceForTab(
      "task-a", (*discovered)->tab_id);
  ASSERT_TRUE(same_source.has_value());
  EXPECT_EQ((*same_source)->source_id, (*discovered)->source_id);
  EXPECT_FALSE(
      actions_->IssueDiscoveredTaskSourceForAction("task-b", "action-open")
          .has_value());

  platform_->resolved_search_address =
      "https://search.example/?q=toffee+recipe";
  EXPECT_TRUE(actions_->StartBrowserSearch(
      TaskId{"task-a"}, opener_tab_id_, web_contents(), "toffee recipe",
      "https://search.example/?q=toffee+recipe", std::nullopt));
  EXPECT_EQ(platform_->searched_tab, web_contents());
  EXPECT_EQ(platform_->searched_query, "toffee recipe");
  EXPECT_EQ(platform_->searched_address,
            "https://search.example/?q=toffee+recipe");

  EXPECT_FALSE(actions_->StartBrowserSearch(
      TaskId{"task-a"}, opener_tab_id_, web_contents(), "toffee recipe",
      "https://different.example/?q=toffee+recipe", std::nullopt));
  EXPECT_EQ(platform_->search_calls, 1);
  EXPECT_TRUE(actions_->ReleaseOwnedTaskTabs("task-a"));
  EXPECT_EQ(platform_->close_calls, 1);
  EXPECT_EQ(platform_->closed_tab, created.get());
  EXPECT_TRUE(actions_->ReleaseOwnedTaskTabs("task-a"));
  EXPECT_EQ(platform_->close_calls, 1);
}

TEST_F(CoreTaskBrowserActionsTest,
       PlatformCannotMakeCoreCloseAnUnclaimedUserTab) {
  auto user_tab = CreateTestWebContents();
  ASSERT_TRUE(user_tab);
  ASSERT_TRUE(
      actions_->RegisterTaskSourceTab(window_token_, 18, user_tab.get()));
  platform_->returned_tab = user_tab.get();
  platform_->claim_returned_tab = false;
  platform_->publish_returned_tab = false;

  const BrowserActionStart opened = actions_->OpenTaskTab(
      TaskId{"task-a"}, ActionId{"action-open"}, opener_tab_id_, web_contents(),
      "https://destination.example/path");
  EXPECT_FALSE(opened.started);
  EXPECT_EQ(opened.created_web_contents, nullptr);
  EXPECT_EQ(platform_->close_calls, 0);
}

TEST_F(CoreTaskBrowserActionsTest,
       TheFirstActivationIsAnnouncedOnceAndARepeatOrSecondWindowIsNot) {
  EXPECT_EQ(actions_->activations(), 0);
  EXPECT_FALSE(actions_->ActivateTaskSourceWindow(9999u));
  EXPECT_EQ(actions_->activations(), 0);

  ASSERT_TRUE(actions_->ActivateTaskSourceWindow(window_token_));
  EXPECT_EQ(actions_->activations(), 1);
  ASSERT_TRUE(actions_->ActivateTaskSourceWindow(window_token_));
  EXPECT_EQ(actions_->activations(), 1);

  const uint64_t second = actions_->RegisterTaskSourceWindow();
  ASSERT_NE(second, 0u);
  ASSERT_TRUE(actions_->ActivateTaskSourceWindow(second));
  EXPECT_EQ(actions_->activations(), 1);

  actions_->DeactivateTaskSourceWindow(window_token_);
  actions_->DeactivateTaskSourceWindow(second);
  ASSERT_TRUE(actions_->ActivateTaskSourceWindow(second));
  EXPECT_EQ(actions_->activations(), 2);
  actions_->UnregisterTaskSourceWindow(second);
}

TEST_F(CoreTaskBrowserActionsTest,
       DiscoveryBlankAndSearchStayBoundToExactSessionDocumentAndTask) {
  ASSERT_TRUE(actions_->ActivateTaskSourceWindow(window_token_));
  auto blank = CreateTestWebContents();
  ASSERT_TRUE(blank);
  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("about:blank"));
  TaffyPageIntelligenceHost::AttachWithAuthority(blank.get(), &host_leases_,
                                                 &host_capabilities_,
                                                 &host_values_, actions_.get());
  platform_->returned_tab = blank.get();

  std::optional<std::string> tab_id;
  actions_->PrepareTaskDiscoveryTab(
      "task-discovery", "bootstrap-effect", "browser-session-1", 3u,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result) { tab_id = result; }));
  ASSERT_TRUE(tab_id);
  EXPECT_EQ(platform_->discovery_open_calls, 1);
  std::optional<std::string> replay;
  actions_->PrepareTaskDiscoveryTab(
      "task-discovery", "bootstrap-effect", "browser-session-1", 3u,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result) { replay = result; }));
  EXPECT_EQ(replay, tab_id);
  EXPECT_EQ(platform_->discovery_open_calls, 1);

  const auto live = ResolveTaskDiscoveryDocument(browser_context(), *tab_id);
  ASSERT_TRUE(live);
  TaskDiscoveryCapabilityBinding discovery{
      .tab_id = TabId{*tab_id},
      .frame_id = FrameId{live->frame_id},
      .page_epoch = PageEpoch{live->page_epoch},
      .opaque_origin_id = live->opaque_origin_id,
      .browser_session_id = "browser-session-1",
      .remaining_new_source_cap = 3u,
  };
  platform_->resolved_search_address = "https://search.example/?q=tea";
  EXPECT_TRUE(actions_->StartBrowserSearch(
      TaskId{"task-discovery"}, TabId{*tab_id}, blank.get(), "tea",
      "https://search.example/?q=tea", discovery));
  EXPECT_EQ(platform_->search_calls, 1);

  auto stale_session = discovery;
  stale_session.browser_session_id = "stale-session";
  EXPECT_FALSE(actions_->StartBrowserSearch(
      TaskId{"task-discovery"}, TabId{*tab_id}, blank.get(), "tea",
      "https://search.example/?q=tea", stale_session));
  auto wrong_opaque = discovery;
  wrong_opaque.opaque_origin_id = "different-opaque";
  EXPECT_FALSE(actions_->StartBrowserSearch(
      TaskId{"task-discovery"}, TabId{*tab_id}, blank.get(), "tea",
      "https://search.example/?q=tea", wrong_opaque));
  EXPECT_EQ(platform_->search_calls, 1);

  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://already-moved.example/"));
  EXPECT_FALSE(actions_->StartBrowserSearch(
      TaskId{"task-discovery"}, TabId{*tab_id}, blank.get(), "tea",
      "https://search.example/?q=tea", discovery));
  EXPECT_EQ(platform_->search_calls, 1);
}

TEST_F(CoreTaskBrowserActionsTest,
       MissingWindowDelegateAndUnavailableBrowserRefuseWithoutCallingJava) {
  actions_->UnbindTaskBrowserActionPlatform(window_token_, platform_.get());
  EXPECT_FALSE(actions_->StartBrowserSearch(
      TaskId{"task-a"}, opener_tab_id_, web_contents(), "query",
      "https://search.example/?q=query", std::nullopt));
  EXPECT_EQ(platform_->resolve_calls, 0);

  ASSERT_TRUE(
      actions_->BindTaskBrowserActionPlatform(window_token_, platform_.get()));
  actions_->set_available(false);
  EXPECT_FALSE(actions_->StartBrowserSearch(
      TaskId{"task-a"}, opener_tab_id_, web_contents(), "query",
      "https://search.example/?q=query", std::nullopt));
  EXPECT_EQ(platform_->resolve_calls, 0);
  EXPECT_FALSE(actions_
                   ->OpenTaskTab(TaskId{"task-a"}, ActionId{"action-open"},
                                 opener_tab_id_, web_contents(),
                                 "https://destination.example/path")
                   .started);
  EXPECT_EQ(platform_->open_calls, 0);
}

}  // namespace
}  // namespace taffy
