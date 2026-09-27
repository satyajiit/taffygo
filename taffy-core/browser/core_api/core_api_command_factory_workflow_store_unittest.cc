// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/task_workflow_tools.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

// The one reviewed row of a shape that needs no model.
std::vector<std::string> ReviewedTools() {
  return {"browser.dom.read"};
}

bool Admits(const std::vector<std::string>& tools, std::string_view name) {
  return std::find(tools.begin(), tools.end(), name) != tools.end();
}

TEST(CoreApiCommandFactoryWorkflowTest,
     AnAttachedStoreAddsItsOneGroupOnAModelRouteOnly) {
  using core_api::mojom::TaskAttachedStore;
  const std::vector<std::string> bare = WorkflowToolsForStart(
      core_api::mojom::TaskTemplateId::kWebErrand,
      core_api::mojom::TaskProviderRoute::kManagedService, {});
  EXPECT_FALSE(Admits(bare, "person.history"));
  EXPECT_FALSE(Admits(bare, "person.bookmarks"));
  EXPECT_FALSE(Admits(bare, "person.open_tabs"));

  const std::vector<std::string> history = WorkflowToolsForStart(
      core_api::mojom::TaskTemplateId::kWebErrand,
      core_api::mojom::TaskProviderRoute::kManagedService,
      {TaskAttachedStore::kHistory, TaskAttachedStore::kHistory});
  EXPECT_EQ(history.size(), bare.size() + 1u);
  EXPECT_TRUE(Admits(history, "person.history"));
  EXPECT_FALSE(Admits(history, "person.bookmarks"));
  EXPECT_FALSE(Admits(history, "person.open_tabs"));

  const std::vector<std::string> all = WorkflowToolsForStart(
      core_api::mojom::TaskTemplateId::kSummarizeEvidence,
      core_api::mojom::TaskProviderRoute::kDirectUserKey,
      {TaskAttachedStore::kOpenTabs, TaskAttachedStore::kBookmarks,
       TaskAttachedStore::kHistory});
  EXPECT_TRUE(Admits(all, "person.history"));
  EXPECT_TRUE(Admits(all, "person.bookmarks"));
  EXPECT_TRUE(Admits(all, "person.open_tabs"));
  EXPECT_LE(all.size(), core_service::mojom::kMaxToolAllowlistEntries);

  EXPECT_TRUE(WorkflowToolsForStart(
                  core_api::mojom::TaskTemplateId::kWebErrand,
                  core_api::mojom::TaskProviderRoute::kNotConfigured,
                  {TaskAttachedStore::kHistory})
                  .empty());
  const std::vector<std::string> no_model = WorkflowToolsForStart(
      core_api::mojom::TaskTemplateId::kBuildSourceTable,
      core_api::mojom::TaskProviderRoute::kNoModelRequired,
      {TaskAttachedStore::kHistory});
  EXPECT_EQ(no_model, ReviewedTools());
}

}  // namespace
}  // namespace taffy
