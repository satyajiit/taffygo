// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
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

class DiscoveryBrowserActionPlatform final : public TaskBrowserActionPlatform {
 public:
  std::optional<std::string> ResolveSearchAddress(
      const std::string& query) override {
    return query.empty() ? std::nullopt
                         : std::optional<std::string>("https://search.test/");
  }

  content::WebContents* OpenTaskTab(
      const std::string& task_id,
      const std::string& action_id,
      const std::string& destination_address) override {
    return nullptr;
  }

  void OpenTaskDiscoveryTab(const std::string& task_id,
                            const std::string& effect_id,
                            TaskDiscoveryTabCallback callback) override {
    ++open_calls;
    if (!registry || !returned_tab || window_token == 0u ||
        !registry->ClaimAssistantCreatedTab(window_token, task_id, effect_id,
                                            returned_tab) ||
        !registry->RegisterProductTab(window_token, next_product_tab_id++,
                                      returned_tab)) {
      std::move(callback).Run(nullptr);
      return;
    }
    pending_tab = returned_tab;
    pending_callback = std::move(callback);
  }

  void CompleteOpen() {
    ASSERT_TRUE(pending_callback);
    std::move(pending_callback).Run(pending_tab);
    pending_tab = nullptr;
  }

  bool StartSearch(content::WebContents* web_contents,
                   const std::string& query,
                   const std::string& destination_address) override {
    return false;
  }

  bool ActivateTaskTab(content::WebContents* web_contents) override {
    return false;
  }

  bool CloseTaskTab(content::WebContents* web_contents) override {
    ++close_calls;
    return false;
  }

  raw_ptr<TaskSourceSelectionRegistry> registry = nullptr;
  raw_ptr<content::WebContents> returned_tab = nullptr;
  raw_ptr<content::WebContents> pending_tab = nullptr;
  TaskSourceSelectionRegistry::WindowToken window_token = 0u;
  TaskDiscoveryTabCallback pending_callback;
  int next_product_tab_id = 100;
  int open_calls = 0;
  int close_calls = 0;
};

class TaskSourceSelectionRegistryDiscoveryTest
    : public content::RenderViewHostTestHarness {
 protected:
  void AttachHost(content::WebContents* contents) {
    TaffyPageIntelligenceHost::AttachWithAuthority(
        contents, &host_leases_, &host_capabilities_, &host_values_);
  }

  TaskSourceSelectionRegistry::WindowToken RegisterWindow(
      TaskSourceSelectionRegistry* registry,
      DiscoveryBrowserActionPlatform* platform) {
    const auto window = registry->RegisterProductWindow();
    EXPECT_NE(window, 0u);
    EXPECT_TRUE(registry->BindBrowserActionPlatform(window, platform));
    platform->registry = registry;
    platform->window_token = window;
    return window;
  }

  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;
};

TEST_F(TaskSourceSelectionRegistryDiscoveryTest,
       BootstrapIsExactIdempotentAndSessionBound) {
  TaskSourceSelectionRegistry registry(browser_context());
  DiscoveryBrowserActionPlatform platform;
  const auto window = RegisterWindow(&registry, &platform);
  ASSERT_TRUE(registry.ActivateProductWindow(window));
  auto blank = CreateTestWebContents();
  ASSERT_TRUE(blank);
  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("about:blank"));
  AttachHost(blank.get());
  platform.returned_tab = blank.get();

  std::optional<std::string> prepared;
  std::optional<std::string> repeated;
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result) { prepared = result; }));
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result) { repeated = result; }));
  EXPECT_FALSE(prepared);
  EXPECT_FALSE(repeated);
  EXPECT_EQ(platform.open_calls, 1);
  platform.CompleteOpen();
  ASSERT_TRUE(prepared);
  EXPECT_EQ(repeated, prepared);
  EXPECT_TRUE(registry.IsExactDiscoveryTab(
      "task-discovery", *prepared, "browser-session-1", 3u, blank.get()));

  std::optional<std::string> completed_replay;
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting([&](std::optional<std::string> result) {
        completed_replay = result;
      }));
  EXPECT_EQ(completed_replay, prepared);
  EXPECT_EQ(platform.open_calls, 1);

  auto expect_refused = [&](const std::string& effect_id,
                            const std::string& session_id, uint32_t cap) {
    bool called = false;
    registry.PrepareDiscoveryTab(
        "task-discovery", effect_id, session_id, cap,
        base::BindLambdaForTesting([&](std::optional<std::string> result) {
          called = true;
          EXPECT_FALSE(result);
        }));
    EXPECT_TRUE(called);
  };
  expect_refused("effect-bootstrap", "stale-session", 3u);
  expect_refused("effect-bootstrap", "browser-session-1", 2u);
  expect_refused("different-effect", "browser-session-1", 3u);

  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://moved.example/"));
  expect_refused("effect-bootstrap", "browser-session-1", 3u);
  EXPECT_EQ(platform.open_calls, 1);
}

TEST_F(TaskSourceSelectionRegistryDiscoveryTest,
       OwnedDiscoverySurvivesSameOriginNavigationWithoutCrossOriginAuthority) {
  TaskSourceSelectionRegistry registry(browser_context());
  DiscoveryBrowserActionPlatform platform;
  const auto window = RegisterWindow(&registry, &platform);
  ASSERT_TRUE(registry.ActivateProductWindow(window));
  auto blank = CreateTestWebContents();
  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("about:blank"));
  AttachHost(blank.get());
  platform.returned_tab = blank.get();
  std::optional<std::string> prepared;
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result) { prepared = result; }));
  platform.CompleteOpen();
  ASSERT_TRUE(prepared);

  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://official.example/entry"));
  auto source =
      registry.IssueDiscoveredSourceForTab("task-discovery", *prepared);
  ASSERT_TRUE(source);
  EXPECT_EQ((*source)->canonical_locator, "https://official.example/entry");
  EXPECT_FALSE(registry.IssueDiscoveredSourceForTab("other-task", *prepared));
  EXPECT_TRUE(registry.IsLiveIssuedSource(**source));

  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(
          GURL("https://official.example/verification?session=fixture"));
  EXPECT_TRUE(registry.IsLiveIssuedSource(**source));
  // Liveness does not rewrite the landing page recorded from its verified
  // action, nor does a second page grant a different origin permission.
  EXPECT_EQ((*source)->canonical_locator, "https://official.example/entry");
  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://other.example/entry"));
  EXPECT_FALSE(registry.IsLiveIssuedSource(**source));
}

// Decision 0190: a site that sends its own tab to another host of its own
// kept a live source only until the next sweep, which compared origins where
// liveness compares sites, erased it, and so destroyed the task's consent.
TEST_F(TaskSourceSelectionRegistryDiscoveryTest,
       ASweepKeepsASourceTheSiteMovedToAHostOfItsOwn) {
  TaskSourceSelectionRegistry registry(browser_context());
  DiscoveryBrowserActionPlatform platform;
  const auto window = RegisterWindow(&registry, &platform);
  ASSERT_TRUE(registry.ActivateProductWindow(window));
  auto blank = CreateTestWebContents();
  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("about:blank"));
  AttachHost(blank.get());
  platform.returned_tab = blank.get();
  std::optional<std::string> prepared;
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result) { prepared = result; }));
  platform.CompleteOpen();
  ASSERT_TRUE(prepared);

  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://www.example.com/download"));
  auto source =
      registry.IssueDiscoveredSourceForTab("task-discovery", *prepared);
  ASSERT_TRUE(source);

  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://portal.example.com/"));
  // Issuing sweeps first, which is where the source used to go.
  ASSERT_TRUE(registry.IssueDiscoveredSourceForTab("task-discovery", *prepared));
  EXPECT_TRUE(registry.IsLiveIssuedSource(**source));

  // Another site is still another site.
  content::WebContentsTester::For(blank.get())
      ->NavigateAndCommit(GURL("https://other.test/"));
  registry.IssueDiscoveredSourceForTab("task-discovery", *prepared);
  EXPECT_FALSE(registry.IsLiveIssuedSource(**source));
}

TEST_F(TaskSourceSelectionRegistryDiscoveryTest,
       BootstrapRefusesAmbiguousActiveWindowsBeforePlatform) {
  TaskSourceSelectionRegistry registry(browser_context());
  DiscoveryBrowserActionPlatform first_platform;
  DiscoveryBrowserActionPlatform second_platform;
  const auto first = RegisterWindow(&registry, &first_platform);
  const auto second = RegisterWindow(&registry, &second_platform);
  ASSERT_TRUE(registry.ActivateProductWindow(first));
  ASSERT_TRUE(registry.ActivateProductWindow(second));

  bool called = false;
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting([&](std::optional<std::string> result) {
        called = true;
        EXPECT_FALSE(result);
      }));
  EXPECT_TRUE(called);
  EXPECT_EQ(first_platform.open_calls, 0);
  EXPECT_EQ(second_platform.open_calls, 0);
}

TEST_F(TaskSourceSelectionRegistryDiscoveryTest,
       BootstrapRevokesClaimWhenPlatformReturnsANonBlankDocument) {
  TaskSourceSelectionRegistry registry(browser_context());
  DiscoveryBrowserActionPlatform platform;
  const auto window = RegisterWindow(&registry, &platform);
  ASSERT_TRUE(registry.ActivateProductWindow(window));
  NavigateAndCommit(GURL("https://unexpected.example/"));
  AttachHost(web_contents());
  platform.returned_tab = web_contents();

  bool called = false;
  registry.PrepareDiscoveryTab(
      "task-discovery", "effect-bootstrap", "browser-session-1", 3u,
      base::BindLambdaForTesting([&](std::optional<std::string> result) {
        called = true;
        EXPECT_FALSE(result);
      }));
  platform.CompleteOpen();

  EXPECT_TRUE(called);
  EXPECT_EQ(platform.close_calls, 1);
  EXPECT_FALSE(registry.IsTaskOwnedTab("task-discovery", web_contents()));
  EXPECT_FALSE(
      registry
          .IssueDiscoveredSourceForAction("task-discovery", "effect-bootstrap")
          .has_value());
}

}  // namespace
}  // namespace taffy
