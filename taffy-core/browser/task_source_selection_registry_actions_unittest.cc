// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "taffy/browser/task_source_selection_registry_actions_test_support.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

using task_source_selection_test::TaskSourceSelectionRegistryActionsTest;

TEST_F(TaskSourceSelectionRegistryActionsTest,
       AssistantOwnershipMustPrecedeSynchronousProductRegistration) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);

  EXPECT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(web_contents()),
            TaskSourceTabProvenance::kAssistantCreated);
  // Projection can run synchronously during TabModel insertion, before the
  // product registration below. Attribution must already exist at that point.
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedTaskId(web_contents()),
            "task-a");
  EXPECT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  EXPECT_TRUE(registry.IsTaskOwnedTab("task-a", web_contents()));

  auto user_tab = CreateTestWebContents();
  ASSERT_TRUE(user_tab);
  EXPECT_TRUE(registry.RegisterProductTab(window, 18, user_tab.get()));
  EXPECT_FALSE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-too-late", user_tab.get()));
  EXPECT_FALSE(registry.HasExactTaskTabClaim("task-a", "action-too-late",
                                             user_tab.get()));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(user_tab.get()),
            TaskSourceTabProvenance::kUserOwned);
  EXPECT_TRUE(TaskSourceSelectionRegistry::BrowserOwnedTaskId(user_tab.get())
                  .empty());
  EXPECT_TRUE(TaskSourceSelectionRegistry::BrowserOwnedTaskId(nullptr).empty());
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       CreatingTaskAttributionCannotBeReassignedAfterAnUnpublishedClaim) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-a", web_contents()));
  registry.ForgetUnpublishedTaskTab("task-a", web_contents());
  EXPECT_FALSE(registry.ClaimAssistantCreatedTab(
      window, "task-b", "action-b", web_contents()));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedTaskId(web_contents()),
            "task-a");
  EXPECT_FALSE(registry.IsTaskOwnedTab("task-a", web_contents()));
  EXPECT_FALSE(registry.IsTaskOwnedTab("task-b", web_contents()));
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       DiscoveryIssuesOnlyTheExactLiveOwnedTabAndReusesItsBinding) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));

  auto issued =
      registry.IssueDiscoveredSourceForAction("task-a", "action-open-a");
  ASSERT_TRUE(issued.has_value());
  ASSERT_TRUE(*issued);
  EXPECT_EQ((*issued)->normalized_origin, "https://user.example");
  EXPECT_TRUE(registry.IsLiveIssuedSource(**issued));

  auto repeated =
      registry.IssueDiscoveredSourceForTab("task-a", (*issued)->tab_id);
  ASSERT_TRUE(repeated.has_value());
  EXPECT_EQ((*repeated)->source_id, (*issued)->source_id);
  EXPECT_EQ(registry.issued_source_count_for_testing(), 1u);

  EXPECT_FALSE(
      registry.IssueDiscoveredSourceForAction("task-b", "action-open-a")
          .has_value());
  EXPECT_FALSE(
      registry.IssueDiscoveredSourceForAction("task-a", "action-open-b")
          .has_value());
  EXPECT_FALSE(registry.IssueDiscoveredSourceForTab("task-a", "different-tab")
                   .has_value());
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       OriginChangeRequiresANewExactDiscoveryBinding) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  auto first =
      registry.IssueDiscoveredSourceForAction("task-a", "action-open-a");
  ASSERT_TRUE(first.has_value());
  const std::string first_source_id = (*first)->source_id;

  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://different.example/path"));
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents());
  ASSERT_TRUE(host);
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  ASSERT_TRUE(live);
  auto replacement =
      registry.IssueDiscoveredSourceForTab("task-a", live->tab_id);
  ASSERT_TRUE(replacement.has_value());
  EXPECT_EQ((*replacement)->normalized_origin, "https://different.example");
  EXPECT_NE((*replacement)->source_id, first_source_id);
  EXPECT_FALSE(registry.IsLiveIssuedSource(**first));
  EXPECT_TRUE(registry.IsLiveIssuedSource(**replacement));
  EXPECT_EQ(registry.issued_source_count_for_testing(), 1u);
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       DiscoveryRefusesUserTabsAndTheProfileSourceCeiling) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents());
  ASSERT_TRUE(host);
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  ASSERT_TRUE(live);
  EXPECT_FALSE(
      registry.IssueDiscoveredSourceForTab("task-a", live->tab_id).has_value());

  auto owned = CreateTestWebContents();
  ASSERT_TRUE(owned);
  content::WebContentsTester::For(owned.get())
      ->NavigateAndCommit(GURL("https://owned.example/"));
  AttachHost(owned.get());
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(window, "task-a",
                                                "action-open-a", owned.get()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 18, owned.get()));
  registry.set_issued_source_limit_for_testing(0u);
  EXPECT_FALSE(
      registry.IssueDiscoveredSourceForAction("task-a", "action-open-a")
          .has_value());
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       DiscoveryNeverIssuesAuthorityForANonHttpDocument) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("ftp://owned.example/path"));
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));

  EXPECT_FALSE(
      registry.IssueDiscoveredSourceForAction("task-a", "action-open-a")
          .has_value());
  EXPECT_EQ(registry.issued_source_count_for_testing(), 0u);
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       SelectedAssistantAndRestoredTabsCannotBecomeConsentSources) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  EXPECT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  EXPECT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  EXPECT_TRUE(registry.SelectProductTab(window, 17, web_contents()));
  EXPECT_TRUE(registry.ActivateProductWindow(window));

  auto intent = core_api::mojom::TaskConsentPreview::New();
  intent->source_hosts.push_back("user.example");
  intent->provider_route = core_api::mojom::TaskProviderRoute::kNoModelRequired;
  EXPECT_FALSE(
      registry
          .ResolveConsentPreview(
              core_api::mojom::TaskTemplateId::kBuildSourceTable, *intent)
          .has_value());

  auto restored = CreateTestWebContents();
  ASSERT_TRUE(restored);
  content::WebContentsTester::For(restored.get())
      ->NavigateAndCommit(GURL("https://user.example/"));
  AttachHost(restored.get());
  EXPECT_TRUE(registry.RegisterProductTab(window, 18, restored.get(), true));
  EXPECT_TRUE(registry.SelectProductTab(window, 18, restored.get()));
  EXPECT_FALSE(registry.CanReconcileTaskTabs("restored-task"));
  EXPECT_TRUE(registry.CanReconcileTaskTabs("task-a"));
  EXPECT_FALSE(
      registry
          .ResolveConsentPreview(
              core_api::mojom::TaskTemplateId::kBuildSourceTable, *intent)
          .has_value());
  registry.UnregisterProductTab(window, 18);
  EXPECT_TRUE(registry.CanReconcileTaskTabs("restored-task"));
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       TaskTabListAndActivateUseTheExactLiveDocument) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  SetGraphRevision(web_contents(), 7u);
  ASSERT_TRUE(registry.ActivateProductWindow(window));

  auto listed = registry.ListOwnedTaskTabs("task-a", "browser-session-1");
  ASSERT_TRUE(listed.has_value());
  ASSERT_TRUE(*listed);
  ASSERT_EQ((*listed)->tabs.size(), 1u);
  EXPECT_FALSE((*listed)->state_was_already_satisfied);
  EXPECT_FALSE((*listed)->tabs.front()->active);
  auto target = (*listed)->tabs.front()->target.Clone();
  ASSERT_TRUE(target);

  platform_.activate = [&](content::WebContents* contents) {
    return registry.SelectProductTab(window, 17, contents);
  };
  auto activated = registry.ActivateOwnedTaskTab(
      "task-a", "browser-session-1", *target);
  ASSERT_TRUE(activated.has_value());
  ASSERT_TRUE(*activated);
  EXPECT_FALSE((*activated)->state_was_already_satisfied);
  EXPECT_EQ(platform_.activate_calls, 1u);

  auto repeated = registry.ActivateOwnedTaskTab(
      "task-a", "browser-session-1", *target);
  ASSERT_TRUE(repeated.has_value());
  EXPECT_TRUE((*repeated)->state_was_already_satisfied);
  EXPECT_EQ(platform_.activate_calls, 1u);

  auto stale = target.Clone();
  ++stale->graph_revision;
  EXPECT_FALSE(registry
                   .ActivateOwnedTaskTab("task-a", "browser-session-1",
                                         *stale)
                   .has_value());
  EXPECT_EQ(platform_.activate_calls, 1u);
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       TaskTabCloseSettlesOnlyTheSameSessionAndDocument) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  SetGraphRevision(web_contents(), 11u);
  auto listed = registry.ListOwnedTaskTabs("task-a", "browser-session-1");
  ASSERT_TRUE(listed.has_value());
  ASSERT_EQ((*listed)->tabs.size(), 1u);
  auto target = (*listed)->tabs.front()->target.Clone();
  ASSERT_TRUE(target);

  platform_.close = [&](content::WebContents*) {
    registry.UnregisterProductTab(window, 17);
    return true;
  };
  auto closed =
      registry.CloseOwnedTaskTab("task-a", "browser-session-1", *target);
  ASSERT_TRUE(closed.has_value());
  ASSERT_TRUE(*closed);
  EXPECT_FALSE((*closed)->state_was_already_satisfied);
  EXPECT_EQ(platform_.close_calls, 1u);

  auto repeated =
      registry.CloseOwnedTaskTab("task-a", "browser-session-1", *target);
  ASSERT_TRUE(repeated.has_value());
  EXPECT_TRUE((*repeated)->state_was_already_satisfied);
  EXPECT_EQ(platform_.close_calls, 1u);
  EXPECT_FALSE(
      registry.CloseOwnedTaskTab("task-a", "browser-session-2", *target)
          .has_value());
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       ReleaseIsTaskIsolatedIdempotentAndNeverIncludesUserTabs) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterWindow(&registry);
  auto second_task_tab = CreateTestWebContents();
  auto user_tab = CreateTestWebContents();
  ASSERT_TRUE(second_task_tab);
  ASSERT_TRUE(user_tab);

  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-a", "action-open-a", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      window, "task-b", "action-open-b", second_task_tab.get()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 18, second_task_tab.get()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 19, user_tab.get()));

  EXPECT_EQ(registry.BeginReleaseTaskTabs("unknown"),
            std::vector<content::WebContents*>());
  EXPECT_EQ(registry.BeginReleaseTaskTabs("task-a"),
            std::vector<content::WebContents*>({web_contents()}));
  EXPECT_TRUE(registry.BeginReleaseTaskTabs("task-a").empty());
  registry.CompleteTaskTabRelease(web_contents(), true);
  EXPECT_FALSE(registry.IsTaskOwnedTab("task-a", web_contents()));
  EXPECT_TRUE(registry.BeginReleaseTaskTabs("task-a").empty());

  EXPECT_EQ(registry.BeginReleaseTaskTabs("task-b"),
            std::vector<content::WebContents*>({second_task_tab.get()}));
  registry.CompleteTaskTabRelease(second_task_tab.get(), false);
  EXPECT_EQ(registry.BeginReleaseTaskTabs("task-b"),
            std::vector<content::WebContents*>({second_task_tab.get()}));
  EXPECT_FALSE(registry.IsTaskOwnedTab("task-a", user_tab.get()));
  EXPECT_FALSE(registry.IsTaskOwnedTab("task-b", user_tab.get()));
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       DestroyedAndMovedTabsNeverWidenReleaseOwnership) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto first_window = RegisterWindow(&registry);
  const auto second_window = RegisterWindow(&registry);
  auto destroyed = CreateTestWebContents();
  ASSERT_TRUE(destroyed);

  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(first_window, "task-destroyed",
                                                "action-open-destroyed",
                                                destroyed.get()));
  ASSERT_TRUE(registry.RegisterProductTab(first_window, 17, destroyed.get()));
  destroyed.reset();
  EXPECT_TRUE(registry.BeginReleaseTaskTabs("task-destroyed").empty());
  EXPECT_FALSE(registry.HasTaskTabsPendingRelease("task-destroyed"));

  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(
      first_window, "task-moved", "action-open-moved", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(first_window, 18, web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(second_window, 29, web_contents()));
  EXPECT_TRUE(registry.IsTaskOwnedTab("task-moved", web_contents()));
  EXPECT_EQ(registry.BeginReleaseTaskTabs("task-moved"),
            std::vector<content::WebContents*>({web_contents()}));
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       WindowRecreationRebindsOnlyTheExistingAssistantClaim) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto first_window = RegisterWindow(&registry);
  ASSERT_TRUE(registry.ClaimAssistantCreatedTab(first_window, "task-rotation",
                                                "action-open", web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(first_window, 17, web_contents()));
  registry.UnregisterProductWindow(first_window);

  const auto replacement_window = RegisterWindow(&registry);
  EXPECT_TRUE(
      registry.RegisterProductTab(replacement_window, 29, web_contents()));
  EXPECT_TRUE(registry.IsTaskOwnedTab("task-rotation", web_contents()));
  EXPECT_EQ(registry.BeginReleaseTaskTabs("task-rotation"),
            std::vector<content::WebContents*>({web_contents()}));
}

TEST_F(TaskSourceSelectionRegistryActionsTest,
       MissingPlatformAndInvalidIdentitiesAreRefusedBeforeMarking) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = registry.RegisterProductWindow();
  ASSERT_NE(window, 0u);
  EXPECT_FALSE(registry.ClaimAssistantCreatedTab(window, "task", "action",
                                                 web_contents()));
  ASSERT_TRUE(registry.BindBrowserActionPlatform(window, &platform_));
  EXPECT_FALSE(
      registry.ClaimAssistantCreatedTab(window, "", "action", web_contents()));
  EXPECT_FALSE(registry.ClaimAssistantCreatedTab(
      window, "task",
      std::string(core_service::mojom::kMaxIdentifierBytes + 1, 'a'),
      web_contents()));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(web_contents()),
            TaskSourceTabProvenance::kUserOwned);
  EXPECT_TRUE(TaskSourceSelectionRegistry::BrowserOwnedTaskId(web_contents())
                  .empty());
}

}  // namespace
}  // namespace taffy
