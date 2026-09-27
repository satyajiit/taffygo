// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a restored tab is allowed to be (decision 0151).
//
// The rule under test has three answers and they are not two: a register that
// cannot be read says nothing, an empty register says every restored tab is
// the person's, and a register that names a tab says Taffy opened it. Before
// the register there was one answer for all three — refused — and it reached a
// person as "Taffy could not read this request" on every tab they had open
// before the last restart.
//
// The fixture installs a real preference store on a `TestBrowserContext`
// rather than a profile, because the registry reads the register through
// `UserPrefs` and never asks whether its browser context is a `Profile`. That
// is deliberate and this suite is the reason: `Profile::FromBrowserContext`
// aborts on a context that is not one.

#include <memory>
#include <optional>
#include <string>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

constexpr api::TaskTemplateId kDeterministicTemplate =
    api::TaskTemplateId::kBuildSourceTable;
constexpr char kHost[] = "restored.example";
constexpr char kAddress[] = "https://restored.example/page";
constexpr int kProductTabId = 41;

api::TaskConsentPreviewPtr Intent() {
  auto intent = api::TaskConsentPreview::New();
  intent->source_hosts.push_back(kHost);
  intent->source_discovery_enabled = false;
  intent->new_source_cap = 0u;
  intent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  return intent;
}

class TaskSourceSelectionRestoredTabsTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL(kAddress));
    TaffyPageIntelligenceHost::AttachWithAuthority(
        web_contents(), &host_leases_, &host_capabilities_, &host_values_);
  }

  // Gives this browser context the preference store the register lives in.
  // Called by the cases that have one; the case that does not is the case
  // where the register cannot be read.
  void InstallPreferences() {
    prefs_ = std::make_unique<TestingPrefServiceSimple>();
    prefs_->registry()->RegisterListPref(
        profile_preferences::kAssistantCreatedTaskTabs);
    user_prefs::UserPrefs::Set(browser_context(), prefs_.get());
  }

  // A tab the person's session restored: no marker, because the marker died
  // with the process that set it.
  std::unique_ptr<content::WebContents> RestoredTab() {
    std::unique_ptr<content::WebContents> tab = CreateTestWebContents();
    content::WebContentsTester::For(tab.get())->NavigateAndCommit(
        GURL(kAddress));
    TaffyPageIntelligenceHost::AttachWithAuthority(
        tab.get(), &host_leases_, &host_capabilities_, &host_values_);
    return tab;
  }

  // Whether a restored tab is offered as a consent source, which is the whole
  // of what its provenance decides.
  bool RestoredTabIsOfferedAsASource(TaskSourceSelectionRegistry* registry,
                                     content::WebContents* tab) {
    const auto window = registry->RegisterProductWindow();
    EXPECT_NE(window, 0u);
    EXPECT_TRUE(registry->RegisterProductTab(window, kProductTabId, tab, true));
    EXPECT_TRUE(registry->SelectProductTab(window, kProductTabId, tab));
    EXPECT_TRUE(registry->ActivateProductWindow(window));
    auto resolved =
        registry->ResolveConsentPreview(kDeterministicTemplate, *Intent());
    return resolved.has_value() && *resolved && !(*resolved)->sources.empty();
  }

  std::unique_ptr<TestingPrefServiceSimple> prefs_;
  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;
};

TEST_F(TaskSourceSelectionRestoredTabsTest,
       ARegisterThatCannotBeReadRefusesARestoredTab) {
  TaskSourceSelectionRegistry registry(browser_context());
  EXPECT_EQ(registry.RememberedAssistantCreatedTab(kProductTabId),
            std::nullopt);
  std::unique_ptr<content::WebContents> tab = RestoredTab();
  EXPECT_FALSE(RestoredTabIsOfferedAsASource(&registry, tab.get()));
}

TEST_F(TaskSourceSelectionRestoredTabsTest,
       AnEmptyRegisterMakesEveryRestoredTabThePersons) {
  InstallPreferences();
  TaskSourceSelectionRegistry registry(browser_context());
  EXPECT_EQ(registry.RememberedAssistantCreatedTab(kProductTabId), false);
  std::unique_ptr<content::WebContents> tab = RestoredTab();
  EXPECT_TRUE(RestoredTabIsOfferedAsASource(&registry, tab.get()));
}

TEST_F(TaskSourceSelectionRestoredTabsTest,
       ARegisterThatNamesTheTabStillRefusesItAfterARestart) {
  InstallPreferences();
  {
    // The session that opened it: Taffy created the tab, and registering it is
    // what writes the id down.
    TaskSourceSelectionRegistry opening(browser_context());
    const auto window = opening.RegisterProductWindow();
    TaskSourceSelectionRegistry::MarkAssistantCreatedTab(web_contents());
    ASSERT_TRUE(
        opening.RegisterProductTab(window, kProductTabId, web_contents()));
    EXPECT_EQ(opening.RememberedAssistantCreatedTab(kProductTabId), true);
  }

  // The next launch. Every registry in the process is gone and so is the
  // WebContents marker, which is exactly the state that used to leave a tab
  // Taffy opened indistinguishable from one a person opened. Only the
  // register survives, and it is enough.
  TaskSourceSelectionRegistry restarted(browser_context());
  EXPECT_EQ(restarted.RememberedAssistantCreatedTab(kProductTabId), true);
  std::unique_ptr<content::WebContents> tab = RestoredTab();
  EXPECT_FALSE(RestoredTabIsOfferedAsASource(&restarted, tab.get()));
}

// The register's answer is the one every reader gets (decision 0232). The tab
// switcher, the Ask list and time on sites ask the WebContents marker rather
// than this registry, so a restored tab the register named used to be Taffy's
// to the start refusal and the person's to all three. The marker comes back
// for exactly the tab the register names, with no creating task, because
// nothing restored can prove which task that was, and for no other tab.
TEST_F(TaskSourceSelectionRestoredTabsTest,
       ARestoredTabIsTaffysToEveryReaderOnlyWhenTheRegisterNamesIt) {
  // A register that cannot be read says nothing, and nothing is not Taffy's.
  std::unique_ptr<content::WebContents> unverified = RestoredTab();
  {
    TaskSourceSelectionRegistry registry(browser_context());
    const auto window = registry.RegisterProductWindow();
    ASSERT_NE(window, 0u);
    ASSERT_TRUE(registry.RegisterProductTab(window, kProductTabId,
                                            unverified.get(), true));
  }
  EXPECT_EQ(
      TaskSourceSelectionRegistry::BrowserOwnedProvenance(unverified.get()),
      TaskSourceTabProvenance::kUserOwned);

  InstallPreferences();
  TaskSourceSelectionRegistry registry(browser_context());
  registry.RememberAssistantCreatedTab(kProductTabId);
  const auto window = registry.RegisterProductWindow();
  ASSERT_NE(window, 0u);

  std::unique_ptr<content::WebContents> persons = RestoredTab();
  ASSERT_TRUE(registry.RegisterProductTab(window, kProductTabId + 1,
                                          persons.get(), true));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(persons.get()),
            TaskSourceTabProvenance::kUserOwned);

  std::unique_ptr<content::WebContents> taffys = RestoredTab();
  ASSERT_TRUE(registry.RegisterProductTab(window, kProductTabId, taffys.get(),
                                          true));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(taffys.get()),
            TaskSourceTabProvenance::kAssistantCreated);
  EXPECT_TRUE(
      TaskSourceSelectionRegistry::BrowserOwnedTaskId(taffys.get()).empty());

  // A content change registers the tab again, and the answer does not move.
  ASSERT_TRUE(registry.RegisterProductTab(window, kProductTabId, taffys.get(),
                                          true));
  EXPECT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(taffys.get()),
            TaskSourceTabProvenance::kAssistantCreated);
}

}  // namespace
}  // namespace taffy
