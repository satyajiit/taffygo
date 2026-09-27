// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/taffy_page_intelligence_host.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

// The production attach path, against a real Chrome Profile and WebContents.
// Content Shell does not own a Profile and therefore cannot construct the
// profile-keyed core service that the host requires.

namespace taffy {
namespace {

class TaffyPageIntelligenceHostBrowserTest : public PlatformBrowserTest {
 protected:
  std::unique_ptr<content::WebContents> CreateUnattachedTab() {
    content::WebContents::CreateParams params(GetProfile());
    return content::WebContents::Create(params);
  }
};

IN_PROC_BROWSER_TEST_F(TaffyPageIntelligenceHostBrowserTest,
                       AttachingGivesTheTabABroker) {
  std::unique_ptr<content::WebContents> tab = CreateUnattachedTab();
  ASSERT_TRUE(tab);

  ASSERT_EQ(nullptr, PageIntelligenceBroker::FromWebContents(tab.get()));
  TaffyPageIntelligenceHost::AttachIfEligible(tab.get());

  TaffyPageIntelligenceHost* const host =
      TaffyPageIntelligenceHost::FromWebContents(tab.get());
  ASSERT_TRUE(host);
  EXPECT_TRUE(PageIntelligenceBroker::FromWebContents(tab.get()));
  EXPECT_TRUE(host->service());
}

IN_PROC_BROWSER_TEST_F(TaffyPageIntelligenceHostBrowserTest,
                       AttachingTwiceProducesOneHost) {
  std::unique_ptr<content::WebContents> tab = CreateUnattachedTab();
  ASSERT_TRUE(tab);

  TaffyPageIntelligenceHost::AttachIfEligible(tab.get());
  TaffyPageIntelligenceHost* const first =
      TaffyPageIntelligenceHost::FromWebContents(tab.get());
  ASSERT_TRUE(first);

  TaffyPageIntelligenceHost::AttachIfEligible(tab.get());
  EXPECT_EQ(first, TaffyPageIntelligenceHost::FromWebContents(tab.get()));
}

IN_PROC_BROWSER_TEST_F(TaffyPageIntelligenceHostBrowserTest,
                       ANullWebContentsIsRefusedRatherThanCrashing) {
  EXPECT_FALSE(TaffyPageIntelligenceHost::IsEligible(nullptr));
  TaffyPageIntelligenceHost::AttachIfEligible(nullptr);
}

IN_PROC_BROWSER_TEST_F(TaffyPageIntelligenceHostBrowserTest,
                       AttachedTabCanBeDestroyed) {
  std::unique_ptr<content::WebContents> closing_tab = CreateUnattachedTab();
  ASSERT_TRUE(closing_tab);

  TaffyPageIntelligenceHost::AttachIfEligible(closing_tab.get());
  TaffyPageIntelligenceHost* const host =
      TaffyPageIntelligenceHost::FromWebContents(closing_tab.get());
  ASSERT_TRUE(host);
  ASSERT_TRUE(host->service());
  ASSERT_TRUE(PageIntelligenceBroker::FromWebContents(closing_tab.get()));

  // TaffyPageIntelligenceHost removes itself during WebContentsDestroyed, so
  // its service stops observing while the broker and profile authority are
  // still alive. The remaining unordered WebContentsUserData teardown must
  // not consult a dead broker.
  closing_tab.reset();
}


IN_PROC_BROWSER_TEST_F(TaffyPageIntelligenceHostBrowserTest,
                       BrokerCanBeDestroyedBeforeItsHost) {
  std::unique_ptr<content::WebContents> tab = CreateUnattachedTab();
  ASSERT_TRUE(tab);

  TaffyPageIntelligenceHost::AttachIfEligible(tab.get());
  TaffyPageIntelligenceHost* const host =
      TaffyPageIntelligenceHost::FromWebContents(tab.get());
  ASSERT_TRUE(host);
  ASSERT_TRUE(host->service());

  // SupportsUserData makes no ordering promise. AttachedTabCanBeDestroyed
  // covers the ordinary path, where the host removes itself first; this covers
  // the other one. Force the adverse order directly rather than relying on a
  // particular hash-table layout: the broker tells the longer-lived service to
  // detach while its ObserverList is alive.
  auto broker_data = tab->TakeUserData(PageIntelligenceBroker::UserDataKey());
  ASSERT_TRUE(broker_data);
  broker_data.reset();

  // The host still owns the service. Its later destruction must use the cached
  // tab identity and the already-reset observation, not the dead broker. This
  // is the case that regressed once: without the broker's explicit destruction
  // callback the service dereferences a freed broker twice, in its own
  // destructor and again when its ScopedObservation unregisters.
  ASSERT_TRUE(host->service());
  tab.reset();
}

}  // namespace
}  // namespace taffy
