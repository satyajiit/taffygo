// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

class RefusingBrowserActions final : public TaskBrowserActionPlatform {
 public:
  std::optional<std::string> ResolveSearchAddress(const std::string&) override {
    return std::nullopt;
  }
  content::WebContents* OpenTaskTab(const std::string&,
                                    const std::string&,
                                    const std::string&) override {
    return nullptr;
  }

  void OpenTaskDiscoveryTab(const std::string& task_id,
                            const std::string& effect_id,
                            TaskDiscoveryTabCallback callback) override {
    std::move(callback).Run(nullptr);
  }
  bool StartSearch(content::WebContents*,
                   const std::string&,
                   const std::string&) override {
    return false;
  }
  bool ActivateTaskTab(content::WebContents*) override { return false; }
  bool CloseTaskTab(content::WebContents*) override { return false; }
};

api::TaskConsentPreviewPtr ModelIntent(
    std::vector<std::string> hosts,
    api::TaskProviderRoute route = api::TaskProviderRoute::kDirectUserKey) {
  auto intent = api::TaskConsentPreview::New();
  intent->source_hosts = std::move(hosts);
  intent->source_discovery_enabled = false;
  intent->new_source_cap = 0u;
  intent->provider_route = route;
  return intent;
}

class TaskSourceSelectionRegistryResearchTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://selected.example/product"));
    TaffyPageIntelligenceHost::AttachWithAuthority(
        web_contents(), &host_leases_, &host_capabilities_, &host_values_);
  }

  std::unique_ptr<content::WebContents> CreateObservedPage(const GURL& url) {
    auto page = CreateTestWebContents();
    EXPECT_TRUE(page);
    content::WebContentsTester::For(page.get())->NavigateAndCommit(url);
    TaffyPageIntelligenceHost::AttachWithAuthority(
        page.get(), &host_leases_, &host_capabilities_, &host_values_);
    return page;
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

  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;
};

TEST_F(TaskSourceSelectionRegistryResearchTest,
       ResolvesEveryExactSelectedHostWithoutUsingSelectionOrder) {
  auto second = CreateObservedPage(GURL("https://second.example/product"));

  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = registry.RegisterProductWindow();
  ASSERT_NE(window, 0u);
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 18, second.get()));
  ASSERT_TRUE(registry.ActivateProductWindow(window));

  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kCompareProducts,
      *ModelIntent({"second.example", "selected.example"}));
  ASSERT_TRUE(resolved.has_value());
  ASSERT_EQ((*resolved)->sources.size(), 2u);
  EXPECT_LT((*resolved)->sources[0]->source_id,
            (*resolved)->sources[1]->source_id);
  std::vector<std::string> origins;
  for (const auto& source : (*resolved)->sources) {
    origins.push_back(source->normalized_origin);
    EXPECT_TRUE(registry.IsLiveIssuedSource(*source));
  }
  std::sort(origins.begin(), origins.end());
  EXPECT_EQ(origins, (std::vector<std::string>{"https://second.example",
                                               "https://selected.example"}));

  auto managed = registry.ResolveConsentPreview(
      api::TaskTemplateId::kCompareProducts,
      *ModelIntent({"selected.example", "second.example"},
                   api::TaskProviderRoute::kManagedService));
  ASSERT_TRUE(managed.has_value());
  ASSERT_EQ((*managed)->sources.size(), 2u);
  EXPECT_EQ((*managed)->provider_route,
            core_service::mojom::TaskProviderRoute::kManagedService);
  for (const auto& source : (*managed)->sources) {
    EXPECT_TRUE(registry.IsLiveIssuedSource(*source));
  }
}

TEST_F(TaskSourceSelectionRegistryResearchTest,
       RefusesDuplicateOrAmbiguousHostsRatherThanChoosingATab) {
  auto duplicate =
      CreateObservedPage(GURL("https://selected.example/other-product"));

  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = registry.RegisterProductWindow();
  ASSERT_NE(window, 0u);
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, web_contents()));
  ASSERT_TRUE(registry.RegisterProductTab(window, 18, duplicate.get()));
  ASSERT_TRUE(registry.ActivateProductWindow(window));

  EXPECT_FALSE(
      registry
          .ResolveConsentPreview(api::TaskTemplateId::kSummarizeEvidence,
                                 *ModelIntent({"selected.example"}))
          .has_value());
  EXPECT_FALSE(registry
                   .ResolveConsentPreview(
                       api::TaskTemplateId::kCompareProducts,
                       *ModelIntent({"selected.example", "selected.example"}))
                   .has_value());
  EXPECT_EQ(registry.issued_source_count_for_testing(), 0u);
}

TEST_F(TaskSourceSelectionRegistryResearchTest,
       DirectAndManagedRoutesUseTheSameExactPageResolver) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  auto direct =
      registry.ResolveConsentPreview(api::TaskTemplateId::kBuildSourceTable,
                                     *ModelIntent({"selected.example"}));
  ASSERT_TRUE(direct.has_value());
  ASSERT_EQ((*direct)->sources.size(), 1u);
  EXPECT_EQ((*direct)->sources.front()->normalized_origin,
            "https://selected.example");
  EXPECT_EQ((*direct)->provider_route,
            service::TaskProviderRoute::kDirectUserKey);

  auto managed = registry.ResolveConsentPreview(
      api::TaskTemplateId::kBuildSourceTable,
      *ModelIntent({"selected.example"},
                   api::TaskProviderRoute::kManagedService));
  ASSERT_TRUE(managed.has_value());
  ASSERT_EQ((*managed)->sources.size(), 1u);
  EXPECT_EQ((*managed)->sources.front()->normalized_origin,
            "https://selected.example");
  EXPECT_EQ((*managed)->provider_route,
            service::TaskProviderRoute::kManagedService);
}

TEST_F(TaskSourceSelectionRegistryResearchTest,
       WebErrandAdmitsZeroOrOneExactSourceAndBoundedDiscovery) {
  RefusingBrowserActions platform;
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);

  auto zero = api::TaskConsentPreview::New();
  zero->source_discovery_enabled = true;
  zero->new_source_cap = 4u;
  zero->provider_route = api::TaskProviderRoute::kDirectUserKey;
  // A zero-source task needs an exact active product window through which its
  // first task-owned tab can be created. A window with no bound browser action
  // platform is not enough authority.
  EXPECT_FALSE(
      registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *zero)
          .has_value());
  ASSERT_TRUE(registry.BindBrowserActionPlatform(window, &platform));

  auto resolved_zero =
      registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *zero);
  ASSERT_TRUE(resolved_zero.has_value());
  EXPECT_TRUE((*resolved_zero)->sources.empty());
  EXPECT_TRUE((*resolved_zero)->source_discovery_enabled);
  EXPECT_EQ((*resolved_zero)->new_source_cap, 4u);
  EXPECT_EQ((*resolved_zero)->provider_route,
            service::TaskProviderRoute::kDirectUserKey);

  auto one = ModelIntent({"selected.example"},
                         api::TaskProviderRoute::kManagedService);
  one->source_discovery_enabled = true;
  one->new_source_cap = 8u;
  auto resolved_one =
      registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *one);
  ASSERT_TRUE(resolved_one.has_value());
  ASSERT_EQ((*resolved_one)->sources.size(), 1u);
  EXPECT_EQ((*resolved_one)->sources.front()->normalized_origin,
            "https://selected.example");
  EXPECT_EQ((*resolved_one)->provider_route,
            service::TaskProviderRoute::kManagedService);

  for (const uint32_t cap : {0u, 9u}) {
    auto malformed = zero.Clone();
    malformed->new_source_cap = cap;
    EXPECT_FALSE(
        registry
            .ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *malformed)
            .has_value());
  }
  auto no_discovery = zero.Clone();
  no_discovery->source_discovery_enabled = false;
  EXPECT_FALSE(
      registry
          .ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *no_discovery)
          .has_value());
  auto no_model = zero.Clone();
  no_model->provider_route = api::TaskProviderRoute::kNoModelRequired;
  EXPECT_FALSE(
      registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *no_model)
          .has_value());
  auto too_many = zero.Clone();
  too_many->source_hosts = {"selected.example", "other.example"};
  EXPECT_FALSE(
      registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *too_many)
          .has_value());

  registry.UnbindBrowserActionPlatform(window, &platform);
}

TEST_F(TaskSourceSelectionRegistryResearchTest,
       LocalSavedFlowCandidateNeedsAnOfferAndExactlyOnePage) {
  RefusingBrowserActions platform;
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  ASSERT_TRUE(registry.BindBrowserActionPlatform(window, &platform));
  auto intent = ModelIntent({"selected.example"}, api::TaskProviderRoute::kNoModelRequired);
  EXPECT_FALSE(registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *intent));
  auto resolved = registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *intent, "opaque-offer");
  ASSERT_TRUE(resolved);
  ASSERT_EQ((*resolved)->sources.size(), 1u);
  EXPECT_FALSE((*resolved)->source_discovery_enabled);
  EXPECT_EQ((*resolved)->new_source_cap, 0u);
  intent->source_hosts.clear();
  EXPECT_FALSE(registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *intent, "opaque-offer"));
  intent->source_hosts = {"selected.example"};
  intent->source_discovery_enabled = true;
  intent->new_source_cap = 1u;
  EXPECT_FALSE(registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand, *intent, "opaque-offer"));
  registry.UnbindBrowserActionPlatform(window, &platform);
}

}  // namespace
}  // namespace taffy
