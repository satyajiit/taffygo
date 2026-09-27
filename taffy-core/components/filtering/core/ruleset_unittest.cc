// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/ruleset.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {
namespace {

namespace proto = url_pattern_index::proto;

constexpr char kList[] =
    "[Adblock Plus 2.0]\n"
    "! A miniature list exercising each verdict the matcher answers.\n"
    "||ads.example^\n"
    "||track.example^$third-party\n"
    "||media.example^$image\n"
    "@@||ads.example/allowed.js$script\n"
    "@@||trusted.example^$document\n"
    "@@||partial.example^$generichide\n"
    "example.test##.cosmetic\n"
    "||regex.example^$csp=default-src 'none'\n";

std::unique_ptr<FilterRulesetMatcher> Compile() {
  CompiledRuleset compiled = CompileRuleset(kList);
  return FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                      compiled.checksum);
}

url::Origin Page(const char* origin) {
  return url::Origin::Create(GURL(origin));
}

TEST(FilterRulesetTest, TheCompileReportSaysWhatIsStanding) {
  const CompiledRuleset compiled = CompileRuleset(kList);
  EXPECT_EQ(10u, compiled.counts.lines);
  EXPECT_EQ(8u, compiled.counts.rules_indexed);
  EXPECT_EQ(0u, compiled.counts.cosmetic_skipped);
  EXPECT_EQ(0u, compiled.counts.unsupported_skipped);
  EXPECT_FALSE(compiled.bytes.empty());
}

TEST(FilterRulesetTest, ABlockRuleBlocksAndAnExceptionStandsItDown) {
  const auto matcher = Compile();
  ASSERT_TRUE(matcher);
  const url::Origin page = Page("https://news.example");
  EXPECT_TRUE(matcher->ShouldBlockRequest(GURL("https://ads.example/banner.png"),
                                          page, proto::ELEMENT_TYPE_IMAGE,
                                          false));
  EXPECT_FALSE(matcher->ShouldBlockRequest(
      GURL("https://ads.example/allowed.js"), page, proto::ELEMENT_TYPE_SCRIPT,
      false));
  EXPECT_FALSE(matcher->ShouldBlockRequest(GURL("https://plain.example/app.js"),
                                           page, proto::ELEMENT_TYPE_SCRIPT,
                                           false));
}

TEST(FilterRulesetTest, ThirdPartyScopingHolds) {
  const auto matcher = Compile();
  ASSERT_TRUE(matcher);
  EXPECT_TRUE(matcher->ShouldBlockRequest(
      GURL("https://track.example/pixel.gif"), Page("https://news.example"),
      proto::ELEMENT_TYPE_IMAGE, false));
  EXPECT_FALSE(matcher->ShouldBlockRequest(
      GURL("https://track.example/pixel.gif"), Page("https://track.example"),
      proto::ELEMENT_TYPE_IMAGE, false));
}

TEST(FilterRulesetTest, ElementTypeScopingHolds) {
  const auto matcher = Compile();
  ASSERT_TRUE(matcher);
  const url::Origin page = Page("https://news.example");
  EXPECT_TRUE(matcher->ShouldBlockRequest(GURL("https://media.example/a.webp"),
                                          page, proto::ELEMENT_TYPE_IMAGE,
                                          false));
  EXPECT_FALSE(matcher->ShouldBlockRequest(GURL("https://media.example/a.js"),
                                           page, proto::ELEMENT_TYPE_SCRIPT,
                                           false));
}

TEST(FilterRulesetTest, DocumentActivationAnswersForTheWholePage) {
  const auto matcher = Compile();
  ASSERT_TRUE(matcher);
  EXPECT_TRUE(matcher->IsDocumentAllowlisted(GURL("https://trusted.example/"),
                                             url::Origin()));
  EXPECT_FALSE(matcher->IsDocumentAllowlisted(GURL("https://news.example/"),
                                              url::Origin()));
  EXPECT_FALSE(matcher->IsGenericBlockDisabled(GURL("https://partial.example/"),
                                               url::Origin()));
}

TEST(FilterRulesetTest, GenerichideDoesNotStandDownNetworkRules) {
  const auto matcher = Compile();
  ASSERT_TRUE(matcher);
  // `$generichide` is cosmetic. It does not set genericblock, so generic
  // network rules still match.
  EXPECT_FALSE(matcher->IsGenericBlockDisabled(GURL("https://partial.example/"),
                                               url::Origin()));
  EXPECT_TRUE(matcher->ShouldBlockRequest(
      GURL("https://ads.example/banner.png"), Page("https://partial.example"),
      proto::ELEMENT_TYPE_IMAGE, false));
}

constexpr char kGenericBlockList[] =
    "||ads.example^\n"
    "||specific.example^$domain=site.example\n"
    "||generic-important.example^$important\n"
    "@@||site.example^$genericblock\n"
    "@@||hide-only.example^$generichide\n";

TEST(FilterRulesetTest, GenericblockIndexesAndDisablesGenericNetworkRules) {
  const CompiledRuleset compiled = CompileRuleset(kGenericBlockList);
  EXPECT_EQ(0u, compiled.counts.unsupported_skipped);
  EXPECT_EQ(5u, compiled.counts.rules_indexed);

  const auto matcher = FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                                    compiled.checksum);
  ASSERT_TRUE(matcher);
  EXPECT_TRUE(matcher->IsGenericBlockDisabled(GURL("https://site.example/"),
                                              url::Origin()));
  EXPECT_FALSE(matcher->IsGenericBlockDisabled(
      GURL("https://hide-only.example/"), url::Origin()));
  EXPECT_FALSE(matcher->IsGenericBlockDisabled(GURL("https://news.example/"),
                                               url::Origin()));

  const url::Origin site = Page("https://site.example");
  EXPECT_FALSE(matcher->ShouldBlockRequest(
      GURL("https://ads.example/banner.png"), site, proto::ELEMENT_TYPE_IMAGE,
      true));
  EXPECT_TRUE(matcher->ShouldBlockRequest(
      GURL("https://specific.example/pixel.gif"), site,
      proto::ELEMENT_TYPE_IMAGE, true));
  EXPECT_TRUE(matcher->ShouldBlockRequest(
      GURL("https://generic-important.example/x.js"), site,
      proto::ELEMENT_TYPE_SCRIPT, true));

  const url::Origin hide_only = Page("https://hide-only.example");
  EXPECT_TRUE(matcher->ShouldBlockRequest(
      GURL("https://ads.example/banner.png"), hide_only,
      proto::ELEMENT_TYPE_IMAGE, false));
}

TEST(FilterRulesetTest, BlockingGenericblockIsAParseError) {
  const CompiledRuleset compiled =
      CompileRuleset("||ads.example^$genericblock\n");
  EXPECT_EQ(1u, compiled.counts.unsupported_skipped);
  EXPECT_EQ(0u, compiled.counts.rules_indexed);
}

TEST(FilterRulesetTest, EditedBytesAreAnAbsentMatcherNeverAHalfWorkingOne) {
  CompiledRuleset compiled = CompileRuleset(kList);
  const int checksum = compiled.checksum;
  ASSERT_FALSE(compiled.bytes.empty());
  std::vector<uint8_t> edited = compiled.bytes;
  edited[edited.size() / 2] ^= 0x5a;
  EXPECT_FALSE(FilterRulesetMatcher::Create(std::move(edited), checksum));

  std::vector<uint8_t> truncated(compiled.bytes.begin(),
                                 compiled.bytes.begin() +
                                     static_cast<long>(compiled.bytes.size() / 2));
  EXPECT_FALSE(FilterRulesetMatcher::Create(std::move(truncated), checksum));
}

TEST(FilterRulesetTest, AnEmptyListIsAWorkingMatcherThatBlocksNothing) {
  CompiledRuleset compiled = CompileRuleset("! nothing here\n");
  EXPECT_EQ(0u, compiled.counts.rules_indexed);
  const auto matcher = FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                                    compiled.checksum);
  ASSERT_TRUE(matcher);
  EXPECT_FALSE(matcher->ShouldBlockRequest(GURL("https://ads.example/x.png"),
                                           Page("https://news.example"),
                                           proto::ELEMENT_TYPE_IMAGE, false));
}

TEST(FilterRulesetTest,
     EngineBlocksListedImageAndAllowsFirstPartyDocument) {
  CompiledRuleset compiled = CompileRuleset("||ads.example.test^\n");
  ASSERT_EQ(0u, compiled.counts.unsupported_skipped);
  const auto matcher = FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                                    compiled.checksum);
  ASSERT_TRUE(matcher);
  EXPECT_TRUE(matcher->ShouldBlockRequest(
      GURL("https://ads.example.test/banner.png"),
      Page("https://news.example.test"), proto::ELEMENT_TYPE_IMAGE, false));
  EXPECT_FALSE(matcher->IsDocumentAllowlisted(
      GURL("https://news.example.test/"), url::Origin()));
  EXPECT_FALSE(matcher->ShouldBlockRequest(
      GURL("https://news.example.test/app.js"),
      Page("https://news.example.test"), proto::ELEMENT_TYPE_SCRIPT, false));
}

constexpr char kCosmeticList[] =
    "example.test##.ad-slot\n"
    "example.test#@#.keep\n"
    "##.generic-ad\n"
    "@@||partial.example^$generichide\n";

std::unique_ptr<FilterRulesetMatcher> CompileCosmetic() {
  CompiledRuleset compiled = CompileRuleset(kCosmeticList);
  return FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                      compiled.checksum);
}

bool HasSelector(const std::vector<std::string>& values, const char* needle) {
  for (const std::string& value : values) {
    if (value == needle) {
      return true;
    }
  }
  return false;
}

TEST(FilterRulesetTest, UrlCosmeticResourcesHideTheNamedClass) {
  const auto matcher = Compile();
  ASSERT_TRUE(matcher);
  const CosmeticResources resources =
      matcher->UrlCosmeticResources(GURL("https://example.test/"));
  EXPECT_TRUE(HasSelector(resources.hide_selectors, ".cosmetic"));
  EXPECT_FALSE(resources.generichide);
}

TEST(FilterRulesetTest, HostnameCosmeticSelectorsAndExceptions) {
  const auto matcher = CompileCosmetic();
  ASSERT_TRUE(matcher);
  const CosmeticResources resources =
      matcher->UrlCosmeticResources(GURL("https://example.test/"));
  EXPECT_TRUE(HasSelector(resources.hide_selectors, ".ad-slot"));
  EXPECT_TRUE(HasSelector(resources.exceptions, ".keep"));
  EXPECT_FALSE(resources.generichide);
}

TEST(FilterRulesetTest, HiddenClassIdSelectorsAnswerGenericClasses) {
  const auto matcher = CompileCosmetic();
  ASSERT_TRUE(matcher);
  const std::vector<std::string> selectors = matcher->HiddenClassIdSelectors(
      {"generic-ad", "keep"}, {}, {".keep"});
  EXPECT_TRUE(HasSelector(selectors, ".generic-ad"));
  EXPECT_FALSE(HasSelector(selectors, ".keep"));
}

TEST(FilterRulesetTest, GenerichideIsADocumentFlag) {
  const auto matcher = CompileCosmetic();
  ASSERT_TRUE(matcher);
  EXPECT_TRUE(matcher->UrlCosmeticResources(GURL("https://partial.example/"))
                  .generichide);
  EXPECT_FALSE(matcher->UrlCosmeticResources(GURL("https://example.test/"))
                   .generichide);
}

}  // namespace
}  // namespace taffy::filtering
