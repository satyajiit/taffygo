// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_document_decisions.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {
namespace {

class FilteringDocumentDecisionsTest
    : public content::RenderViewHostTestHarness {
 protected:
  FilteringDocumentDecisionsTest()
      : content::RenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    RegisterFilteringPreferences(prefs_.registry());
  }

  FilteringRulesetService::ListReader ReaderReturning(std::string text) {
    return base::BindRepeating(
        [](std::string text,
           base::OnceCallback<void(std::optional<std::string>)> reply) {
          std::move(reply).Run(std::move(text));
        },
        std::move(text));
  }

  FilteringDocumentDecision Resolve(FilteringRulesetService& service) {
    scoped_refptr<const SharedRuleset> ruleset = service.ruleset();
    EXPECT_TRUE(ruleset);
    FilteringDocumentDecisions::CreateForWebContents(web_contents());
    return FilteringDocumentDecisions::FromWebContents(web_contents())
        ->Resolve(
            service, std::move(ruleset), web_contents()->GetLastCommittedURL(),
            web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin());
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(FilteringDocumentDecisionsTest,
       ReusesTheDocumentAnswerUntilPostureChanges) {
  FilteringRulesetService service(&prefs_, ReaderReturning("||ads.example^\n"));
  task_environment()->RunUntilIdle();
  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://news.example/article"));

  EXPECT_TRUE(Resolve(service).active);
  EXPECT_TRUE(Resolve(service).active);
  auto* decisions = FilteringDocumentDecisions::FromWebContents(web_contents());
  ASSERT_TRUE(decisions);
  EXPECT_EQ(1u, decisions->evaluation_count_for_testing());

  ASSERT_TRUE(service.SetSiteException("news.example", true));
  EXPECT_FALSE(Resolve(service).active);
  EXPECT_EQ(2u, decisions->evaluation_count_for_testing());
}

TEST_F(FilteringDocumentDecisionsTest, KeepsDocumentAllowlistSemantics) {
  FilteringRulesetService service(
      &prefs_, ReaderReturning("@@||trusted.example^$document\n"));
  task_environment()->RunUntilIdle();
  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://trusted.example/article"));

  EXPECT_FALSE(Resolve(service).active);
  EXPECT_FALSE(Resolve(service).active);
  EXPECT_EQ(1u, FilteringDocumentDecisions::FromWebContents(web_contents())
                    ->evaluation_count_for_testing());
}

}  // namespace
}  // namespace taffy::filtering
