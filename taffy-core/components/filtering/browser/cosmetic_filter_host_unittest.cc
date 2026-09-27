// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/cosmetic_filter_host.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {
namespace {

bool Has(const std::vector<std::string>& values, const char* needle) {
  return std::find(values.begin(), values.end(), needle) != values.end();
}

class CosmeticFilterHostTest : public testing::Test {
 protected:
  CosmeticFilterHostTest() { RegisterFilteringPreferences(prefs_.registry()); }

  FilteringRulesetService::ListReader ReaderReturning(
      std::optional<std::string> text) {
    return base::BindRepeating(
        [](std::optional<std::string> text,
           base::OnceCallback<void(std::optional<std::string>)> reply) {
          std::move(reply).Run(text);
        },
        std::move(text));
  }

  url::Origin OriginOf(const char* spec) {
    return url::Origin::Create(GURL(spec));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple prefs_;
};

TEST_F(CosmeticFilterHostTest, NoServiceDisablesCosmetics) {
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      nullptr, GURL("https://example.test/"), OriginOf("https://example.test/"),
      &exceptions);
  EXPECT_FALSE(resources->enabled);
  EXPECT_TRUE(resources->hide_selectors.empty());
  EXPECT_TRUE(exceptions.empty());
}

TEST_F(CosmeticFilterHostTest, NonHttpIsDisabled) {
  FilteringRulesetService service(&prefs_,
                                  ReaderReturning("example.test##.ad-slot\n"));
  task_environment_.RunUntilIdle();
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      &service, GURL("chrome://settings"), OriginOf("chrome://settings"),
      &exceptions);
  EXPECT_FALSE(resources->enabled);
}

TEST_F(CosmeticFilterHostTest, ASiteExceptionDisablesCosmetics) {
  FilteringRulesetService service(&prefs_,
                                  ReaderReturning("example.test##.ad-slot\n"));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(service.SetSiteException("example.test", true));
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      &service, GURL("https://example.test/"),
      OriginOf("https://example.test/"), &exceptions);
  EXPECT_FALSE(resources->enabled);
  EXPECT_TRUE(resources->hide_selectors.empty());
}

TEST_F(CosmeticFilterHostTest, ADocumentAllowlistDisablesCosmetics) {
  FilteringRulesetService service(
      &prefs_,
      ReaderReturning("@@||trusted.example^$document\ntrusted.example##.ad\n"));
  task_environment_.RunUntilIdle();
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      &service, GURL("https://trusted.example/"),
      OriginOf("https://trusted.example/"), &exceptions);
  EXPECT_FALSE(resources->enabled);
}

TEST_F(CosmeticFilterHostTest, AnActiveHttpsPageReturnsHideSelectors) {
  FilteringRulesetService service(&prefs_,
                                  ReaderReturning("example.test##.ad-slot\n"));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(service.ruleset());
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      &service, GURL("https://example.test/"),
      OriginOf("https://example.test/"), &exceptions);
  EXPECT_TRUE(resources->enabled);
  EXPECT_FALSE(resources->generichide);
  EXPECT_TRUE(Has(resources->hide_selectors, ".ad-slot"));
}

TEST_F(CosmeticFilterHostTest, GenerichideStopsTheClassIdPump) {
  FilteringRulesetService service(
      &prefs_,
      ReaderReturning("##.generic-ad\n@@||partial.example^$generichide\n"));
  task_environment_.RunUntilIdle();
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      &service, GURL("https://partial.example/"),
      OriginOf("https://partial.example/"), &exceptions);
  EXPECT_TRUE(resources->enabled);
  EXPECT_TRUE(resources->generichide);
  EXPECT_TRUE(CosmeticFilterHost::EvaluateHiddenClassIdSelectors(
                  &service, resources->enabled, resources->generichide,
                  exceptions, {"generic-ad"}, {})
                  .empty());
}

TEST_F(CosmeticFilterHostTest, TokenCapBoundsInvalidInputInspection) {
  FilteringRulesetService service(&prefs_, ReaderReturning("##.generic-ad\n"));
  task_environment_.RunUntilIdle();
  std::vector<std::string> classes;
  for (int i = 0; i < 200; ++i) {
    classes.emplace_back(257, 'a');
  }
  classes.push_back("generic-ad");
  const std::vector<std::string> selectors =
      CosmeticFilterHost::EvaluateHiddenClassIdSelectors(&service, true, false,
                                                         {}, classes, {});
  EXPECT_TRUE(selectors.empty());
}

TEST_F(CosmeticFilterHostTest, ExceptionsStayInTheBrowser) {
  FilteringRulesetService service(
      &prefs_, ReaderReturning(
                   "example.test##.ad-slot\nexample.test#@#.keep\n##.keep\n"));
  task_environment_.RunUntilIdle();
  std::vector<std::string> exceptions;
  auto resources = CosmeticFilterHost::EvaluateUrlResources(
      &service, GURL("https://example.test/"),
      OriginOf("https://example.test/"), &exceptions);
  EXPECT_TRUE(resources->enabled);
  EXPECT_TRUE(Has(exceptions, ".keep"));
  EXPECT_TRUE(CosmeticFilterHost::EvaluateHiddenClassIdSelectors(
                  &service, true, false, exceptions, {"keep"}, {})
                  .empty());
}

}  // namespace
}  // namespace taffy::filtering
