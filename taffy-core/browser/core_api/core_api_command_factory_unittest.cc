// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_api/task_workflow_tools.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "taffy/browser/generated/product_capabilities.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class FixedEntropy final : public CoreApiEntropySource {
 public:
  std::string NewOpaqueId(std::string_view domain) override {
    return std::string(domain) + "-fixed";
  }

  std::array<uint8_t, 32> NewTaskSeed() override {
    std::array<uint8_t, 32> seed{};
    seed.fill(0x5a);
    return seed;
  }
};

core_api::mojom::TaskConsentPreviewPtr SelectionIntent() {
  return core_api::mojom::TaskConsentPreview::New(
      std::vector<std::string>{"example.test"}, false, 0u,
      core_api::mojom::TaskProviderRoute::kNoModelRequired, std::vector<core_api::mojom::TaskAttachedStore>{});
}

// What the caller asks this task for. It is the M3 reviewed workflow's own
// default, supplied here the way ProfileCoreApiFacade supplies it: the factory
// carries the allowlist it was asked for and holds none of its own (decision
// 0057).
std::vector<std::string> ReviewedTools() {
  return {"browser.dom.read"};
}

bool WorkflowHasTool(core_api::mojom::TaskTemplateId template_id,
                     std::string_view name) {
  const std::vector<std::string> tools = WorkflowToolsForStart(
      template_id, core_api::mojom::TaskProviderRoute::kManagedService, {});
  return std::find(tools.begin(), tools.end(), name) != tools.end();
}

core_service::mojom::TaskConsentPreviewPtr ResolvedConsent() {
  std::vector<core_service::mojom::TaskConsentSourcePtr> sources;
  sources.push_back(core_service::mojom::TaskConsentSource::New(
      "source-fixed", "tab-fixed", "https://example.test", std::nullopt));
  return core_service::mojom::TaskConsentPreview::New(
      std::move(sources), false, 0u,
      core_service::mojom::TaskProviderRoute::kNoModelRequired);
}

// The same selection, asked for over a different provider route.
//
// Both halves have to move: the factory reads the route from the caller's
// intent and `AdmitsStartConsent` then requires the resolved preview to agree,
// so changing one alone is refused at `build/consent-shape`.
core_api::mojom::TaskConsentPreviewPtr SelectionIntentForRoute(
    core_api::mojom::TaskProviderRoute route) {
  core_api::mojom::TaskConsentPreviewPtr intent = SelectionIntent();
  intent->provider_route = route;
  return intent;
}

core_service::mojom::TaskConsentPreviewPtr ResolvedConsentForRoute(
    core_service::mojom::TaskProviderRoute route) {
  core_service::mojom::TaskConsentPreviewPtr preview = ResolvedConsent();
  preview->provider_route = route;
  return preview;
}

// The limit a start states for `kind`, or nothing when it states none.
//
// "States none" is the case that matters: an unstated budget is not absent at
// the other end, it is zero, because the task factory fills a missing limit
// with the fail-closed default (decision 0218).
std::optional<uint64_t> StatedBudget(
    const core_service::mojom::StartTaskCommand& start,
    core_service::mojom::TaskBudgetKind kind) {
  for (const auto& budget : start.budgets) {
    if (budget->kind == kind) {
      return budget->limit;
    }
  }
  return std::nullopt;
}

TEST(CoreApiCommandFactoryTest,
     EveryTemplateAndRouteHasOneClosedToolVerdict) {
  constexpr std::array templates = {
      core_api::mojom::TaskTemplateId::kCompareProducts,
      core_api::mojom::TaskTemplateId::kSummarizeEvidence,
      core_api::mojom::TaskTemplateId::kBuildSourceTable,
      core_api::mojom::TaskTemplateId::kWebErrand,
  };
  for (const core_api::mojom::TaskTemplateId template_id : templates) {
    const std::vector<std::string> direct = WorkflowToolsForStart(
        template_id, core_api::mojom::TaskProviderRoute::kDirectUserKey, {});
    const std::vector<std::string> managed = WorkflowToolsForStart(
        template_id, core_api::mojom::TaskProviderRoute::kManagedService, {});
    EXPECT_EQ(direct, managed);
    EXPECT_FALSE(direct.empty());
    EXPECT_LE(direct.size(), core_service::mojom::kMaxToolAllowlistEntries);
    EXPECT_EQ(std::set<std::string>(direct.begin(), direct.end()).size(),
              direct.size());

    EXPECT_TRUE(WorkflowToolsForStart(
                    template_id,
                    core_api::mojom::TaskProviderRoute::kNotConfigured, {})
                    .empty());
    const std::vector<std::string> no_model = WorkflowToolsForStart(
        template_id, core_api::mojom::TaskProviderRoute::kNoModelRequired, {});
    if (template_id ==
        core_api::mojom::TaskTemplateId::kBuildSourceTable) {
      EXPECT_EQ(no_model, ReviewedTools());
    } else {
      EXPECT_TRUE(no_model.empty());
    }
  }
}

TEST(CoreApiCommandFactoryTest,
     HighValueToolsAreConfinedToTheirReviewedTemplates) {
  using Template = core_api::mojom::TaskTemplateId;

  for (const std::string_view name : {
           "page.pdf.inspect",
           "page.images",
           "page.screenshot.inspect",
           "page.video.inspect",
           "media.probe",
           "core.table.reshape",
           "artifact.xlsx.create",
           "artifact.pdf.create",
           "python.execute",
       }) {
    EXPECT_TRUE(WorkflowHasTool(Template::kCompareProducts, name)) << name;
  }
  EXPECT_FALSE(
      WorkflowHasTool(Template::kCompareProducts, "browser.form.fill"));

  for (const std::string_view name : {
           "page.pdf.inspect",
           "page.images",
           "page.screenshot.inspect",
           "page.video.inspect",
           "media.probe",
           "artifact.pdf.create",
           "python.execute",
       }) {
    EXPECT_TRUE(WorkflowHasTool(Template::kSummarizeEvidence, name)) << name;
  }
  for (const std::string_view name : {
           "core.table.reshape",
           "artifact.xlsx.create",
           "browser.form.fill",
       }) {
    EXPECT_FALSE(WorkflowHasTool(Template::kSummarizeEvidence, name)) << name;
  }

  for (const std::string_view name : {
           "page.pdf.inspect",
           "page.images",
           "page.screenshot.inspect",
           "core.table.reshape",
           "artifact.xlsx.create",
           "python.execute",
       }) {
    EXPECT_TRUE(WorkflowHasTool(Template::kBuildSourceTable, name)) << name;
  }
  for (const std::string_view name : {
           "page.video.inspect",
           "media.probe",
           "artifact.pdf.create",
           "browser.form.fill",
       }) {
    EXPECT_FALSE(WorkflowHasTool(Template::kBuildSourceTable, name)) << name;
  }

  for (const std::string_view name : {
           "page.pdf.inspect",
           "page.images",
           "page.screenshot.inspect",
           "browser.download.start",
           "browser.download.cancel",
           "browser.form.inspect",
           "browser.form.fill",
       }) {
    EXPECT_TRUE(WorkflowHasTool(Template::kWebErrand, name)) << name;
  }
  for (const std::string_view name : {
           "page.video.inspect",
           "media.probe",
           "core.table.reshape",
           "artifact.xlsx.create",
           "artifact.pdf.create",
           "python.execute",
           "browser.form.select",
           "browser.form.toggle",
           "browser.form.submit",
       }) {
    EXPECT_FALSE(WorkflowHasTool(Template::kWebErrand, name)) << name;
  }
}

TEST(CoreApiCommandFactoryTest, ActiveProfileControlsDelegatedTaskStart) {
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());
  auto projected = factory.BuildStartTask(
      "Build the source table",
      core_api::mojom::TaskTemplateId::kBuildSourceTable, std::nullopt,
      SelectionIntent(), ResolvedConsent(), ReviewedTools(),
      "browser-session-fixed", 7, 10);

  EXPECT_EQ(projected.has_value(),
            product_capabilities::Active().delegated_task_start);
  if (projected) {
    ASSERT_TRUE(product_capabilities::Active().task_milestone);
    EXPECT_EQ(projected->core_service_command->start_task->milestone,
              *product_capabilities::Active().task_milestone);
  }
}

TEST(CoreApiCommandFactoryTest, BrowserMintsCompleteStartIdentity) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());

  auto projected = factory.BuildStartTask(
      "Build the source table",
      core_api::mojom::TaskTemplateId::kBuildSourceTable, "workspace-fixed",
      SelectionIntent(), ResolvedConsent(), ReviewedTools(),
      "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  ASSERT_TRUE(projected->core_api_command);
  ASSERT_TRUE(projected->core_service_command);
  EXPECT_EQ(projected->core_api_command->kind,
            core_api::mojom::CoreCommandKind::kStartTask);
  EXPECT_EQ(projected->core_api_command->operation->operation_id,
            "operation-fixed");
  EXPECT_EQ(projected->core_api_command->operation->service_generation, 7u);
  EXPECT_EQ(projected->core_api_command->start_task->request_id,
            "request-fixed");

  const auto& service = *projected->core_service_command;
  ASSERT_TRUE(service.start_task);
  EXPECT_EQ(service.operation->operation_id, "operation-fixed");
  EXPECT_EQ(service.operation->idempotency_key, "idempotency-fixed");
  EXPECT_EQ(service.start_task->task_id, "task-fixed");
  EXPECT_EQ(service.start_task->trace_id, "trace-fixed");
  EXPECT_EQ(service.start_task->browser_profile_id, "profile-fixed");
  EXPECT_EQ(service.start_task->browser_session_id, "browser-session-fixed");
  EXPECT_EQ(service.start_task->control_mode,
            core_service::mojom::TaskControlMode::kShared);
  ASSERT_TRUE(product_capabilities::Active().task_milestone);
  EXPECT_EQ(service.start_task->milestone,
            *product_capabilities::Active().task_milestone);
  ASSERT_EQ(service.start_task->consent_preview->sources.size(), 1u);
  EXPECT_EQ(service.start_task->consent_preview->sources.front()->tab_id,
            "tab-fixed");
  EXPECT_EQ(service.start_task->provider_route_id, "no_model_required");
  // The allowlist the caller asked for, unchanged. It used to be substituted
  // from a compiled-in array of one name, which made this factory a second
  // place the product's tool vocabulary was written down.
  EXPECT_EQ(service.start_task->tool_allowlist, ReviewedTools());
  bool found_zero_model_budget = false;
  for (const auto& budget : service.start_task->budgets) {
    if (budget->kind ==
        core_service::mojom::TaskBudgetKind::kMaxModelRequests) {
      EXPECT_EQ(budget->limit, 0u);
      found_zero_model_budget = true;
    }
  }
  EXPECT_TRUE(found_zero_model_budget);
  const std::vector<uint8_t> expected_seed(32, 0x5a);
  EXPECT_EQ(service.start_task->task_id_seed, expected_seed);
}

// A start that may pay a provider must say how often it may pay again.
//
// The retry budget was not stated at all, and an unstated budget is zero
// rather than absent, so `can_afford_model_attempt` answered "no" on the first
// attempt of every turn in every task. The model router's whole retry ladder —
// backoff, `Retry-After`, failover, `SEMANTIC_RETRY_ATTEMPTS` — was compiled in
// and unreachable, and one `503` from a provider that had just answered `200`
// eight times ended an errand outright (decision 0218). Nothing could see it:
// the Rust fixtures state the limit themselves, and the tests here read only
// the model-request budget.
TEST(CoreApiCommandFactoryTest, EveryStartStatesARetryBudgetBesideItsModelOne) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  constexpr std::array routes = {
      std::pair{core_api::mojom::TaskProviderRoute::kNoModelRequired,
                core_service::mojom::TaskProviderRoute::kNoModelRequired},
      std::pair{core_api::mojom::TaskProviderRoute::kDirectUserKey,
                core_service::mojom::TaskProviderRoute::kDirectUserKey},
      std::pair{core_api::mojom::TaskProviderRoute::kManagedService,
                core_service::mojom::TaskProviderRoute::kManagedService},
  };
  for (const auto& [asked, resolved] : routes) {
    CoreApiCommandFactory factory("profile-fixed",
                                  std::make_unique<FixedEntropy>());
    auto projected = factory.BuildStartTask(
        "Build the source table",
        core_api::mojom::TaskTemplateId::kBuildSourceTable, "workspace-fixed",
        SelectionIntentForRoute(asked), ResolvedConsentForRoute(resolved),
        ReviewedTools(), "browser-session-fixed", 7, 10);
    ASSERT_TRUE(projected.has_value()) << static_cast<int>(resolved);
    const auto& start = *projected->core_service_command->start_task;

    const std::optional<uint64_t> requests = StatedBudget(
        start, core_service::mojom::TaskBudgetKind::kMaxModelRequests);
    const std::optional<uint64_t> retries = StatedBudget(
        start, core_service::mojom::TaskBudgetKind::kMaxRetriesPerStep);
    ASSERT_TRUE(requests.has_value()) << static_cast<int>(resolved);
    ASSERT_TRUE(retries.has_value())
        << "route " << static_cast<int>(resolved)
        << " states no retry budget, so it silently gets zero";

    // The two move together: a retry budget over a zero request budget would
    // authorize an attempt the request budget already refuses, and a zero
    // retry budget over a real request budget is the defect above.
    if (*requests == 0u) {
      EXPECT_EQ(*retries, kNoModelMaxRetriesPerStep);
    } else {
      EXPECT_EQ(*retries, kMaxRetriesPerStep);
      EXPECT_GT(*retries, 0u);
    }
  }
}

TEST(CoreApiCommandFactoryTest, AWiderToolAllowlistIsCarriedNotNarrowed) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());
  const std::vector<std::string> asked = {"browser.dom.read",
                                          "browser.link.open"};

  auto projected =
      factory.BuildStartTask("Build the source table",
                             core_api::mojom::TaskTemplateId::kBuildSourceTable,
                             std::nullopt, SelectionIntent(), ResolvedConsent(),
                             asked, "browser-session-fixed", 7, 10);

  ASSERT_TRUE(projected.has_value());
  EXPECT_EQ(projected->core_service_command->start_task->tool_allowlist, asked);
}

TEST(CoreApiCommandFactoryTest, AMalformedToolAllowlistBuildsNoCommand) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());
  // Empty first, and it is the case that matters: an empty allowlist is read
  // downstream as everything the milestone has reached, so composing one here
  // would be composing a wider task than any explicit list could ask for.
  //
  // The over-cap list carries distinct names on purpose. Filled with one
  // repeated name it would be refused by the duplicate clause as readily as by
  // the cap, so deleting the cap entirely would leave this test green — a case
  // that cannot say which rule refused it is not evidence for either.
  std::vector<std::string> over_cap;
  for (uint64_t index = 0;
       index <= core_service::mojom::kMaxToolAllowlistEntries; ++index) {
    over_cap.push_back("browser.dom.read." + base::NumberToString(index));
  }
  const std::vector<std::vector<std::string>> malformed = {
      {},
      {"browser.dom.read", "browser.dom.read"},
      {""},
      over_cap,
  };

  for (size_t index = 0; index < malformed.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_FALSE(
        factory
            .BuildStartTask("Build the source table",
                            core_api::mojom::TaskTemplateId::kBuildSourceTable,
                            std::nullopt, SelectionIntent(), ResolvedConsent(),
                            malformed[index], "browser-session-fixed", 7, 10)
            .has_value());
  }
}

TEST(CoreApiCommandFactoryTest, RejectsOversizedGoal) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());

  EXPECT_FALSE(factory
                   .BuildStartTask(
                       std::string(core_api::mojom::kMaxTaskGoalBytes + 1, 'x'),
                       core_api::mojom::TaskTemplateId::kBuildSourceTable,
                       std::nullopt, SelectionIntent(), ResolvedConsent(),
                       ReviewedTools(), "browser-session-fixed", 1, 1)
                   .has_value());
}

TEST(CoreApiCommandFactoryTest, AccountAuthorizationsUseFiveMinuteLifetime) {
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());

  auto oauth = factory.BuildStartAuth(core_api::mojom::AuthProvider::kGithub,
                                      7u, 1'000u);
  ASSERT_TRUE(oauth);
  EXPECT_EQ(301'000u,
            oauth->core_api_command->operation->deadline_monotonic_ms);
  EXPECT_EQ(301'000u,
            oauth->core_service_command->operation->deadline_monotonic_ms);

  auto email = factory.BuildRequestEmailLink("person@example.test", 7u, 2'000u);
  ASSERT_TRUE(email);
  EXPECT_EQ(302'000u,
            email->core_api_command->operation->deadline_monotonic_ms);
  EXPECT_EQ(302'000u,
            email->core_service_command->operation->deadline_monotonic_ms);
}

// The composer's withdrawal (decision 0097 section 3): one identity, bounded
// on both sides, crossing both contracts as itself.
//
// It is a command in its own right rather than a newer request with an empty
// prefix, because those are different facts — a newer request supersedes an
// older one and asks for something, and this asks for nothing. So the two are
// asserted to be different kinds rather than assumed to be.
TEST(CoreApiCommandFactoryTest, AComposerWithdrawalCrossesAsItself) {
  CoreApiCommandFactory factory("profile-fixed",
                                std::make_unique<FixedEntropy>());
  const std::optional<ProjectedCoreCommand> projected =
      factory.BuildCancelComposerCompletion("composer-1", 9, 100);

  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->core_api_command->kind,
            core_api::mojom::CoreCommandKind::kCancelComposerCompletion);
  ASSERT_TRUE(projected->core_api_command->cancel_composer_completion);
  EXPECT_EQ(projected->core_api_command->cancel_composer_completion->request_id,
            "composer-1");
  EXPECT_EQ(
      projected->core_service_command->kind,
      core_service::mojom::CoreServiceCommandKind::kCancelComposerCompletion);
  ASSERT_TRUE(projected->core_service_command->cancel_composer_completion);
  EXPECT_EQ(
      projected->core_service_command->cancel_composer_completion->request_id,
      "composer-1");

  EXPECT_FALSE(
      CoreApiCommandFactory("profile-fixed", std::make_unique<FixedEntropy>())
          .BuildCancelComposerCompletion("", 9, 100)
          .has_value());
  EXPECT_FALSE(
      CoreApiCommandFactory("profile-fixed", std::make_unique<FixedEntropy>())
          .BuildCancelComposerCompletion(
              std::string(core_api::mojom::kMaxIdentifierBytes + 1u, 'r'), 9,
              100)
          .has_value());
}

}  // namespace
}  // namespace taffy
