// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
namespace taffy {
namespace {
class SavedFlowStartSelectionTest : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://example.test/start"));
  }
  uint64_t RegisterSelected(TaskSourceSelectionRegistry* registry) {
    const auto window = registry->RegisterProductWindow();
    EXPECT_NE(window, 0u);
    EXPECT_TRUE(registry->RegisterProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->SelectProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->ActivateProductWindow(window));
    return window;
  }
};
TEST_F(SavedFlowStartSelectionTest, RequiresTheExactSelectedUserTab) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  EXPECT_EQ(registry.SelectedUserOwnedTab(), web_contents());
  registry.ClearProductSelection(window);
  EXPECT_EQ(registry.SelectedUserOwnedTab(), nullptr);
  ASSERT_TRUE(registry.SelectProductTab(window, 17, web_contents()));
  EXPECT_EQ(registry.SelectedUserOwnedTab(), web_contents());
  TaskSourceSelectionRegistry::MarkAssistantCreatedTab(web_contents());
  EXPECT_EQ(registry.SelectedUserOwnedTab(), nullptr);
}
TEST_F(SavedFlowStartSelectionTest,
       RefusesAmbiguousActiveWindowsAndPrivateProfiles) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  const auto other = registry.RegisterProductWindow();
  ASSERT_TRUE(registry.ActivateProductWindow(other));
  EXPECT_EQ(registry.SelectedUserOwnedTab(), nullptr);
  registry.DeactivateProductWindow(other);
  EXPECT_EQ(registry.SelectedUserOwnedTab(), web_contents());
  content::TestBrowserContext private_context;
  private_context.set_is_off_the_record(true);
  TaskSourceSelectionRegistry private_registry(&private_context);
  EXPECT_EQ(private_registry.SelectedUserOwnedTab(), nullptr);
}
TEST_F(SavedFlowStartSelectionTest,
       RepeatedSelectionIsStableButLeavingAndReturningInvalidates) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  const auto revision = registry.selection_revision();
  ASSERT_TRUE(registry.SelectProductTab(window, 17, web_contents()));
  EXPECT_EQ(registry.selection_revision(), revision);
  registry.ClearProductSelection(window);
  ASSERT_TRUE(registry.SelectProductTab(window, 17, web_contents()));
  EXPECT_EQ(registry.SelectedUserOwnedTab(), web_contents());
  EXPECT_GT(registry.selection_revision(), revision);
  const auto reselected = registry.selection_revision();
  registry.DeactivateProductWindow(window);
  ASSERT_TRUE(registry.ActivateProductWindow(window));
  EXPECT_GT(registry.selection_revision(), reselected);
}

TEST_F(SavedFlowStartSelectionTest,
       SwitchingTabsAndBackInvalidatesTheSelectionLifetime) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  auto other = CreateTestWebContents();
  ASSERT_TRUE(registry.RegisterProductTab(window, 18, other.get()));
  const auto revision = registry.selection_revision();
  ASSERT_TRUE(registry.SelectProductTab(window, 18, other.get()));
  ASSERT_TRUE(registry.SelectProductTab(window, 17, web_contents()));
  EXPECT_EQ(registry.SelectedUserOwnedTab(), web_contents());
  EXPECT_GT(registry.selection_revision(), revision);
}

TEST_F(SavedFlowStartSelectionTest,
       ReplacingAndRestoringSelectedContentsInvalidates) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  auto replacement = CreateTestWebContents();
  const auto revision = registry.selection_revision();
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, replacement.get()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  EXPECT_EQ(registry.SelectedUserOwnedTab(), web_contents());
  EXPECT_GT(registry.selection_revision(), revision);
}
}  // namespace
}  // namespace taffy
