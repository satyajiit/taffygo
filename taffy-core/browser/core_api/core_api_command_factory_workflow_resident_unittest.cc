// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_api/core_api_command_factory_workflow_test_support.h"
#include "taffy/browser/generated/product_capabilities.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

// The errand consents the factory refuses, and the commands a resident task
// carries its browser correlation on.

namespace taffy {
namespace {

TEST(CoreApiCommandFactoryWorkflowTest, MalformedErrandConsentBuildsNoCommand) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());
  auto expect_refused = [&factory](
                            core_api::mojom::TaskConsentPreviewPtr intent,
                            core_service::mojom::TaskConsentPreviewPtr resolved) {
    EXPECT_FALSE(factory
                     .BuildStartTask(
                         "Find and complete the errand",
                         core_api::mojom::TaskTemplateId::kWebErrand,
                         std::nullopt, std::move(intent), std::move(resolved),
                         ErrandTools(), "browser-session-fixed", 7, 10)
                     .has_value());
  };

  auto no_discovery =
      ErrandIntent(core_api::mojom::TaskProviderRoute::kDirectUserKey);
  no_discovery->source_discovery_enabled = false;
  expect_refused(std::move(no_discovery),
                 ResolvedErrand(
                     core_service::mojom::TaskProviderRoute::kDirectUserKey));

  auto zero_cap =
      ErrandIntent(core_api::mojom::TaskProviderRoute::kDirectUserKey);
  auto resolved_zero_cap = ResolvedErrand(
      core_service::mojom::TaskProviderRoute::kDirectUserKey);
  zero_cap->new_source_cap = 0u;
  resolved_zero_cap->new_source_cap = 0u;
  expect_refused(std::move(zero_cap), std::move(resolved_zero_cap));

  auto over_cap =
      ErrandIntent(core_api::mojom::TaskProviderRoute::kDirectUserKey);
  auto resolved_over_cap = ResolvedErrand(
      core_service::mojom::TaskProviderRoute::kDirectUserKey);
  over_cap->new_source_cap = 9u;
  resolved_over_cap->new_source_cap = 9u;
  expect_refused(std::move(over_cap), std::move(resolved_over_cap));

  expect_refused(
      ErrandIntent(core_api::mojom::TaskProviderRoute::kManagedService,
                   {"example.test"}),
      ResolvedErrand(core_service::mojom::TaskProviderRoute::kDirectUserKey,
                     true));

  expect_refused(
      ErrandIntent(core_api::mojom::TaskProviderRoute::kDirectUserKey,
                   {"other.example"}),
      ResolvedErrand(core_service::mojom::TaskProviderRoute::kDirectUserKey,
                     true));
}

TEST(CoreApiCommandFactoryWorkflowTest,
     ResidentTaskCommandsCarryBrowserCorrelation) {
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedWorkflowEntropy>());

  auto cancel = factory.BuildCancelTask("task-visible", 17, 9, 100);
  ASSERT_TRUE(cancel);
  EXPECT_EQ("trace-fixed", cancel->core_service_command->cancel_task->trace_id);

  auto handover = factory.BuildCompleteHandover(
      "task-visible", "turn-1-handover-1", "lease-before-fixed",
      "lease-resume-fixed", 2u, 17, 9, 100);
  ASSERT_TRUE(handover);
  EXPECT_EQ("task-visible",
            handover->core_api_command->complete_handover->task_id);
  EXPECT_EQ("turn-1-handover-1",
            handover->core_service_command->complete_handover->handover_id);
  EXPECT_EQ("lease-before-fixed",
            handover->core_service_command->complete_handover->lease_before);
  EXPECT_EQ("lease-resume-fixed",
            handover->core_service_command->complete_handover->resumed_with);
  EXPECT_EQ(2u,
            handover->core_service_command->complete_handover->person_input);
  EXPECT_EQ("trace-fixed",
            handover->core_service_command->complete_handover->trace_id);

  auto ask =
      factory.BuildSupplyUserInput("task-visible", "the blue one", 17, 9, 100);
  ASSERT_TRUE(ask);
  EXPECT_EQ("task-visible", ask->core_api_command->supply_user_input->task_id);
  EXPECT_EQ("the blue one", ask->core_api_command->supply_user_input->answer);
  EXPECT_EQ("the blue one",
            ask->core_service_command->supply_user_input->answer);
  EXPECT_EQ("trace-fixed",
            ask->core_service_command->supply_user_input->trace_id);
  EXPECT_FALSE(
      factory.BuildSupplyUserInput("task-visible", "123456", 17, 9, 100));
  EXPECT_FALSE(factory.BuildSupplyUserInput("task-visible", "", 17, 9, 100));

  // A follow-up crosses under the answer's bound and the answer's refusals
  // (decision 0137), and projects onto its own Core Service command.
  auto follow_up =
      factory.BuildFollowUp("task-visible", "and the fee?", 17, 9, 100);
  ASSERT_TRUE(follow_up);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kFollowUp,
            follow_up->core_api_command->kind);
  EXPECT_EQ("task-visible", follow_up->core_api_command->follow_up->task_id);
  EXPECT_EQ("and the fee?", follow_up->core_api_command->follow_up->question);
  EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kFollowUp,
            follow_up->core_service_command->kind);
  EXPECT_EQ("and the fee?",
            follow_up->core_service_command->follow_up->question);
  EXPECT_EQ("trace-fixed",
            follow_up->core_service_command->follow_up->trace_id);
  EXPECT_FALSE(factory.BuildFollowUp("task-visible", "123456", 17, 9, 100));
  EXPECT_FALSE(factory.BuildFollowUp("task-visible", "", 17, 9, 100));

  auto approval = factory.BuildApproveAction("task-visible", "action-visible",
                                             std::string(64u, 'a'), 17, 9, 100,
                                             1'000, "browser-session-fixed");
  ASSERT_TRUE(approval);
  EXPECT_EQ(std::string(64u, 'a'),
            approval->core_service_command->user_decision->approval_digest);
  EXPECT_EQ("approval-receipt-fixed",
            approval->core_service_command->user_decision->approval_receipt_id);
  EXPECT_EQ(31'000u, approval->core_service_command->user_decision
                         ->approval_expires_at_utc_ms);
  EXPECT_EQ("browser-session-fixed",
            approval->core_service_command->user_decision->browser_session_id);

  auto permission = factory.BuildPermissionResult(
      "task-visible", "request-visible",
      core_api::mojom::PlatformPermission::kCamera,
      core_api::mojom::PermissionDecision::kGranted, 17, 9, 100);
  ASSERT_TRUE(permission);
  EXPECT_EQ(core_service::mojom::PlatformPermission::kCamera,
            permission->core_service_command->permission_result->permission);
  EXPECT_EQ("trace-fixed",
            permission->core_service_command->permission_result->trace_id);

  EXPECT_FALSE(factory.BuildCancelTask("task-visible", 0, 9, 100));
  EXPECT_FALSE(factory.BuildApproveAction("task-visible", "action-visible",
                                          "digest-visible", 0, 9, 100, 1'000,
                                          "browser-session-fixed"));
  EXPECT_FALSE(factory.BuildApproveAction("task-visible", "action-visible",
                                          "digest-visible", 17, 9, 100, 1'000,
                                          "browser-session-fixed"));
  EXPECT_FALSE(factory.BuildPermissionResult(
      "task-visible", "request-visible",
      core_api::mojom::PlatformPermission::kReadUserFile,
      core_api::mojom::PermissionDecision::kGranted, 17, 9, 100));
}

}  // namespace
}  // namespace taffy
