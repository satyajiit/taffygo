// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/test/recovery/form_approval_vertical_test_support.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

class FormApprovalVerticalBrowserTest : public PlatformBrowserTest {
 protected:
  FormApprovalVerticalBrowserTest()
      : origins_(test::FixtureOriginMap::Scheme::kHttps) {}

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    origins_.Start();
  }

  content::WebContents* active_tab_contents() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  CoreServiceManager* manager() {
    content::WebContents* const contents = active_tab_contents();
    Profile* const profile =
        contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
                 : nullptr;
    return profile ? CoreServiceManagerFactory::GetForProfile(profile)
                   : nullptr;
  }

  test::FixtureOriginMap origins_;
};

IN_PROC_BROWSER_TEST_F(
    FormApprovalVerticalBrowserTest,
    ExactValueApprovalFillsOnceAndObservesThePostcondition) {
  const GURL fixture_url = origins_.FixtureUrl("search-results");
  content::WebContents* const tab = active_tab_contents();
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::NavigateToURL(tab, fixture_url));
  ASSERT_TRUE(TaffyPageIntelligenceHost::FromWebContents(tab));
  ASSERT_EQ("lumen arc desk lamp",
            content::EvalJs(tab, "document.getElementById('q').value"));

  CoreServiceManager* const core = manager();
  ASSERT_TRUE(core);
  test::FormApprovalVerticalHarness form(core, tab);
  ASSERT_TRUE(form.Initialize());
  ASSERT_TRUE(form.DiscoverForm());
  ASSERT_TRUE(form.StartWebErrand(std::string(fixture_url.host())));
  ASSERT_TRUE(form.OpenFieldValueRequest("form-values-1"));
  ASSERT_EQ(1u, form.field_ids().size());
  ASSERT_EQ(1u, form.field_labels().size());
  EXPECT_EQ("Search this fixture site", form.field_labels().front());
  EXPECT_GT(form.approval_lifetime_seconds(), 0u);

  // This is the exact value a person has already reviewed in Compose. Supply
  // is the only crossing into browser custody; the next approval confirms the
  // exact executable tuple and never re-carries these bytes.
  const std::string exact_value = "Aster Bloom 73";
  ASSERT_EQ(browser::field_values::mojom::FieldValueSupplyVerdict::kAccepted,
            form.Supply(std::vector<std::string>{exact_value}));
  ASSERT_TRUE(form.WaitForSuppliedCount(1u));
  ASSERT_TRUE(form.ConfirmFillProposal("fill-search-query", std::string(64u, 'a'),
                                       0u));
  ASSERT_EQ(service::PolicyEvaluationStatus::kGranted, form.AuthorizeFill());

  ASSERT_EQ(service::TaskEffectCompletionStatus::kSucceeded,
            form.DispatchFill());
  EXPECT_EQ(exact_value,
            content::EvalJs(tab, "document.getElementById('q').value"));
  EXPECT_EQ(fixture_url, tab->GetLastCommittedURL());

  // Both the one-use grant and the browser vault reference were spent. The
  // identical dispatch can no longer write, even before the fixture changes.
  EXPECT_NE(service::TaskEffectCompletionStatus::kSucceeded,
            form.ReplayDispatch());
  EXPECT_EQ(3u, form.submitted_command_count());
  EXPECT_EQ(1u, form.last_supplied_count());
  EXPECT_FALSE(form.CoreCommandsContain(exact_value));
  EXPECT_FALSE(origins_.sentinel().HasHits())
      << origins_.sentinel().DescribeHits();
}

}  // namespace
}  // namespace taffy
