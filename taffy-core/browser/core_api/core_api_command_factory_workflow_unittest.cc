// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_api/core_api_command_factory_workflow_test_support.h"
#include "taffy/browser/core_api/task_workflow_tools.h"
#include "taffy/browser/generated/product_capabilities.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(CoreApiCommandFactoryWorkflowTest,
     ErrandAllowlistUsesExactRowsAndTheTabsGroup) {
  const std::vector<std::string> tools = WorkflowToolsForStart(
      core_api::mojom::TaskTemplateId::kWebErrand,
      core_api::mojom::TaskProviderRoute::kManagedService, {});
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.tabs"), tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.search"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.dom.read"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.link.open"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.form.fill"),
            tools.end());
  for (const std::string_view mutation : {"browser.form.select",
                                          "browser.form.toggle",
                                          "browser.form.submit"}) {
    EXPECT_EQ(std::find(tools.begin(), tools.end(), mutation), tools.end())
        << mutation;
  }
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.form.inspect"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "user.request_values"),
            tools.end());
  EXPECT_EQ(std::find(tools.begin(), tools.end(), "browser.tabs.open"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.dom.query"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.dom.click"),
            tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.dom.focus"),
            tools.end());
  EXPECT_TRUE(WorkflowToolsForStart(
                  core_api::mojom::TaskTemplateId::kWebErrand,
                  core_api::mojom::TaskProviderRoute::kNoModelRequired, {})
                  .empty());
}

TEST(CoreApiCommandFactoryWorkflowTest,
     DirectUserKeyStartCarriesABoundedModelBudget) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());
  auto intent = SelectionIntent();
  intent->provider_route = core_api::mojom::TaskProviderRoute::kDirectUserKey;
  auto consent = ResolvedConsent();
  consent->provider_route =
      core_service::mojom::TaskProviderRoute::kDirectUserKey;

  auto projected = factory.BuildStartTask(
      "Build the source table",
      core_api::mojom::TaskTemplateId::kBuildSourceTable, std::nullopt,
      std::move(intent), std::move(consent), ReviewedTools(),
      "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  const auto& start = *projected->core_service_command->start_task;
  EXPECT_EQ(start.provider_route_id, "direct_user_key");
  EXPECT_EQ(start.consent_preview->provider_route,
            core_service::mojom::TaskProviderRoute::kDirectUserKey);
  EXPECT_EQ(start.control_mode,
            core_service::mojom::TaskControlMode::kAssistant);
  bool found_model_budget = false;
  for (const auto& budget : start.budgets) {
    if (budget->kind ==
        core_service::mojom::TaskBudgetKind::kMaxModelRequests) {
      EXPECT_EQ(budget->limit, kDirectUserKeyMaxModelRequests);
      found_model_budget = true;
    }
  }
  EXPECT_TRUE(found_model_budget);
}

TEST(CoreApiCommandFactoryWorkflowTest,
     ComparisonCarriesEveryResolvedSourceAndItsExactBudget) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());
  auto projected = factory.BuildStartTask(
      "Compare the products", core_api::mojom::TaskTemplateId::kCompareProducts,
      std::nullopt, ComparisonIntent(), ResolvedComparison(), ReviewedTools(),
      "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  const auto& start = *projected->core_service_command->start_task;
  ASSERT_EQ(start.consent_preview->sources.size(), 2u);
  bool found_source_budget = false;
  for (const auto& budget : start.budgets) {
    if (budget->kind == core_service::mojom::TaskBudgetKind::kMaxSources) {
      EXPECT_EQ(budget->limit, 2u);
      found_source_budget = true;
    }
  }
  EXPECT_TRUE(found_source_budget);

  auto duplicate = ResolvedComparison();
  duplicate->sources[1]->tab_id = "tab-a";
  duplicate->sources[1]->normalized_origin = "https://other.example";
  EXPECT_FALSE(factory
                   .BuildStartTask(
                       "Compare the products",
                       core_api::mojom::TaskTemplateId::kCompareProducts,
                       std::nullopt, ComparisonIntent(), std::move(duplicate),
                       ReviewedTools(), "browser-session-fixed", 7, 10)
                   .has_value());
}

TEST(CoreApiCommandFactoryWorkflowTest,
     ManagedPageStartUsesAssistantControlAndExactModelBudget) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());
  auto intent = SelectionIntent();
  intent->provider_route = core_api::mojom::TaskProviderRoute::kManagedService;
  auto consent = ResolvedConsent();
  consent->provider_route =
      core_service::mojom::TaskProviderRoute::kManagedService;

  auto projected = factory.BuildStartTask(
      "Build the source table",
      core_api::mojom::TaskTemplateId::kBuildSourceTable, std::nullopt,
      std::move(intent), std::move(consent), ReviewedTools(),
      "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  const auto& start = *projected->core_service_command->start_task;
  EXPECT_EQ(start.provider_route_id, "managed_service");
  EXPECT_EQ(start.control_mode,
            core_service::mojom::TaskControlMode::kAssistant);
  auto model = std::find_if(
      start.budgets.begin(), start.budgets.end(), [](const auto& budget) {
        return budget->kind ==
               core_service::mojom::TaskBudgetKind::kMaxModelRequests;
      });
  ASSERT_NE(model, start.budgets.end());
  EXPECT_EQ((*model)->limit, kManagedServiceMaxModelRequests);
}

TEST(CoreApiCommandFactoryWorkflowTest,
     ZeroSourceWebErrandCarriesBoundedDiscoveryAndSixtyFourTurns) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());
  auto projected = factory.BuildStartTask(
      "Find and complete the errand",
      core_api::mojom::TaskTemplateId::kWebErrand, std::nullopt,
      ErrandIntent(core_api::mojom::TaskProviderRoute::kDirectUserKey),
      ResolvedErrand(core_service::mojom::TaskProviderRoute::kDirectUserKey),
      ErrandTools(), "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  const auto& start = *projected->core_service_command->start_task;
  EXPECT_TRUE(start.consent_preview->sources.empty());
  EXPECT_TRUE(start.consent_preview->source_discovery_enabled);
  EXPECT_EQ(start.consent_preview->new_source_cap, 4u);
  EXPECT_EQ(start.control_mode,
            core_service::mojom::TaskControlMode::kAssistant);
  for (const auto& budget : start.budgets) {
    if (budget->kind == core_service::mojom::TaskBudgetKind::kMaxSources) {
      EXPECT_EQ(budget->limit, 4u);
    }
    if (budget->kind ==
        core_service::mojom::TaskBudgetKind::kMaxModelRequests) {
      EXPECT_EQ(budget->limit, kWebErrandMaxModelRequests);
    }
  }
}

TEST(CoreApiCommandFactoryWorkflowTest,
     OneSourceManagedErrandBindsThatSourceAndAddsItsDiscoveryCap) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());
  auto projected = factory.BuildStartTask(
      "Find and complete the errand",
      core_api::mojom::TaskTemplateId::kWebErrand, std::nullopt,
      ErrandIntent(core_api::mojom::TaskProviderRoute::kManagedService,
                   {"example.test"}),
      ResolvedErrand(core_service::mojom::TaskProviderRoute::kManagedService,
                     true),
      ErrandTools(), "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  const auto& start = *projected->core_service_command->start_task;
  ASSERT_EQ(start.consent_preview->sources.size(), 1u);
  EXPECT_EQ(start.provider_route_id, "managed_service");
  EXPECT_EQ(start.control_mode,
            core_service::mojom::TaskControlMode::kAssistant);
  for (const auto& budget : start.budgets) {
    if (budget->kind == core_service::mojom::TaskBudgetKind::kMaxSources) {
      EXPECT_EQ(budget->limit, 5u);
    }
    if (budget->kind ==
        core_service::mojom::TaskBudgetKind::kMaxModelRequests) {
      EXPECT_EQ(budget->limit, kWebErrandMaxModelRequests);
    }
  }
}

TEST(CoreApiCommandFactoryWorkflowTest,
     SelectedSavedFlowHasZeroModelBudgetAndNoDiscovery) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                               std::make_unique<FixedWorkflowEntropy>());
  const auto template_id = core_api::mojom::TaskTemplateId::kWebErrand;
  const auto route = core_api::mojom::TaskProviderRoute::kNoModelRequired;
  const std::optional<std::string> version = "recorded-flow@1";
  auto tools = WorkflowToolsForStart(template_id, route, {}, version);
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.navigate"), tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "user.handover"), tools.end());
  EXPECT_NE(std::find(tools.begin(), tools.end(), "browser.download.from_link"), tools.end());
  EXPECT_EQ(std::find(tools.begin(), tools.end(), "browser.search"), tools.end());
  EXPECT_TRUE(WorkflowToolsForStart(template_id, route, {}).empty());
  EXPECT_FALSE(factory.BuildStartTask(
      "Replay", template_id, std::nullopt, SelectionIntent(), ResolvedConsent(),
      tools, "browser-session-fixed", 7, 10));
  auto projected = factory.BuildStartTask(
      "Replay", template_id, std::nullopt, SelectionIntent(), ResolvedConsent(),
      tools, "browser-session-fixed", 7, 10, "opaque-page-offer", version);
  ASSERT_TRUE(projected);
  const auto& start = *projected->core_service_command->start_task;
  EXPECT_EQ(start.kind, core_service::mojom::TaskKind::kErrand);
  EXPECT_EQ(start.control_mode, core_service::mojom::TaskControlMode::kAssistant);
  EXPECT_EQ(start.skill_version_id, version);
  EXPECT_FALSE(start.consent_preview->source_discovery_enabled);
  EXPECT_EQ(start.consent_preview->new_source_cap, 0u);
  for (const auto& budget : start.budgets) {
    if (budget->kind == core_service::mojom::TaskBudgetKind::kMaxModelRequests) {
      EXPECT_EQ(budget->limit, 0u);
    }
  }
  auto widened = SelectionIntent();
  auto consent = ResolvedConsent();
  widened->source_discovery_enabled = consent->source_discovery_enabled = true;
  widened->new_source_cap = consent->new_source_cap = 1u;
  EXPECT_FALSE(factory.BuildStartTask(
      "Replay", template_id, std::nullopt, std::move(widened), std::move(consent),
      tools, "browser-session-fixed", 7, 10, "opaque-page-offer", version));
}

}  // namespace
}  // namespace taffy
