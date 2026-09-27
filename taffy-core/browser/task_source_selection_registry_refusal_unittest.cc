// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a refused start tells the person (decision 0231). Every clause of
// `ResolveConsentPreview` used to reach the phone as "Taffy could not read
// this request. Change it and try again." — including a start refused because
// every open tab was one Taffy had opened, which no change to the request can
// fix. Each case here provokes one clause and reads the verdict it writes.

#include <memory>
#include <optional>
#include <string>

#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

constexpr api::TaskTemplateId kDeterministicTemplate =
    api::TaskTemplateId::kBuildSourceTable;
constexpr char kHost[] = "selected.example";

api::TaskConsentPreviewPtr Intent(const std::string& host) {
  auto intent = api::TaskConsentPreview::New();
  intent->source_hosts.push_back(host);
  intent->source_discovery_enabled = false;
  intent->new_source_cap = 0u;
  intent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  return intent;
}

// An errand from the page the person is on: one named host, discovery on.
api::TaskConsentPreviewPtr ErrandIntent(const std::string& host) {
  auto intent = Intent(host);
  intent->source_discovery_enabled = true;
  intent->new_source_cap = 1u;
  intent->provider_route = api::TaskProviderRoute::kDirectUserKey;
  return intent;
}

class TaskSourceSelectionRegistryRefusalTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://selected.example/path"));
    TaffyPageIntelligenceHost::AttachWithAuthority(
        web_contents(), &host_leases_, &host_capabilities_, &host_values_);
  }

  TaskSourceSelectionRegistry::WindowToken RegisterSelected(
      TaskSourceSelectionRegistry* registry) {
    const auto window = registry->RegisterProductWindow();
    EXPECT_NE(window, 0u);
    EXPECT_TRUE(registry->RegisterProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->SelectProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->ActivateProductWindow(window));
    return window;
  }

  // A second tab of the person's own, on a page of their choosing.
  std::unique_ptr<content::WebContents> PersonsTab(const GURL& address) {
    std::unique_ptr<content::WebContents> tab = CreateTestWebContents();
    content::WebContentsTester::For(tab.get())->NavigateAndCommit(address);
    TaffyPageIntelligenceHost::AttachWithAuthority(
        tab.get(), &host_leases_, &host_capabilities_, &host_values_);
    return tab;
  }

  // The verdict one resolution writes, starting from a value no clause
  // answers, so a clause that writes nothing is seen as writing nothing.
  api::CoreApiSubmissionStatus VerdictOf(
      TaskSourceSelectionRegistry* registry,
      api::TaskTemplateId template_id,
      const api::TaskConsentPreview& intent) {
    api::CoreApiSubmissionStatus verdict =
        api::CoreApiSubmissionStatus::kDuplicate;
    EXPECT_FALSE(registry
                     ->ResolveConsentPreview(template_id, intent, std::nullopt,
                                             &verdict)
                     .has_value());
    return verdict;
  }

  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;
};

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       APageInATabTaffyOpenedIsNotOpenInThePersonsTabs) {
  // The phone's case: fifteen tabs, every one of them Taffy's, and the page
  // the person asked about was in one of them.
  TaskSourceSelectionRegistry registry(browser_context());
  TaskSourceSelectionRegistry::MarkAssistantCreatedTab(web_contents());
  RegisterSelected(&registry);

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent(kHost)),
            api::CoreApiSubmissionStatus::kSourceNotOpen);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       ASiteNoTabShowsIsNotOpenInThePersonsTabs) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate,
                      *Intent("elsewhere.example")),
            api::CoreApiSubmissionStatus::kSourceNotOpen);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       TwoOfThePersonsTabsOnOneSiteAreAmbiguous) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  std::unique_ptr<content::WebContents> second =
      PersonsTab(GURL("https://selected.example/other"));
  ASSERT_TRUE(registry.RegisterProductTab(window, 18, second.get()));

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent(kHost)),
            api::CoreApiSubmissionStatus::kSourceAmbiguous);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       NotExactlyOneWindowInFrontIsAWindowRefusal) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto first = RegisterSelected(&registry);
  const auto second = registry.RegisterProductWindow();
  ASSERT_NE(second, 0u);
  ASSERT_TRUE(registry.ActivateProductWindow(second));

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent(kHost)),
            api::CoreApiSubmissionStatus::kWindowUnavailable);

  registry.DeactivateProductWindow(first);
  registry.DeactivateProductWindow(second);
  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent(kHost)),
            api::CoreApiSubmissionStatus::kWindowUnavailable);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       AnErrandInAWindowThatCannotHoldTaffysTabsIsAWindowRefusal) {
  // No action platform is bound to this window, so Taffy has nowhere to
  // open the tab an errand moves in.
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  EXPECT_EQ(VerdictOf(&registry, api::TaskTemplateId::kWebErrand,
                      *ErrandIntent(kHost)),
            api::CoreApiSubmissionStatus::kWindowUnavailable);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       APrivateProfileIsSomewhereTaffyCannotWork) {
  content::TestBrowserContext private_context;
  private_context.set_is_off_the_record(true);
  TaskSourceSelectionRegistry registry(&private_context);

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent(kHost)),
            api::CoreApiSubmissionStatus::kCoreUnavailable);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       AFullSourceRegisterIsBackpressureNotABadRequest) {
  TaskSourceSelectionRegistry registry(browser_context());
  registry.set_issued_source_limit_for_testing(0u);
  RegisterSelected(&registry);

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent(kHost)),
            api::CoreApiSubmissionStatus::kBackpressure);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       ARequestThatDoesNotFitIsStillAnInvalidRequest) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  auto duplicated = Intent(kHost);
  duplicated->source_hosts.push_back(kHost);
  duplicated->provider_route = api::TaskProviderRoute::kDirectUserKey;
  EXPECT_EQ(VerdictOf(&registry, api::TaskTemplateId::kCompareProducts,
                      *duplicated),
            api::CoreApiSubmissionStatus::kInvalidRequest);

  auto discovery = Intent(kHost);
  discovery->source_discovery_enabled = true;
  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *discovery),
            api::CoreApiSubmissionStatus::kInvalidRequest);

  EXPECT_EQ(VerdictOf(&registry, kDeterministicTemplate, *Intent("a/b")),
            api::CoreApiSubmissionStatus::kInvalidRequest);
}

TEST_F(TaskSourceSelectionRegistryRefusalTest,
       AnAdmittedConsentWritesNoVerdict) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  api::CoreApiSubmissionStatus verdict =
      api::CoreApiSubmissionStatus::kDuplicate;
  auto resolved = registry.ResolveConsentPreview(
      kDeterministicTemplate, *Intent(kHost), std::nullopt, &verdict);
  ASSERT_TRUE(resolved.has_value());
  ASSERT_TRUE(*resolved);
  EXPECT_EQ(verdict, api::CoreApiSubmissionStatus::kDuplicate);
}

}  // namespace
}  // namespace taffy
