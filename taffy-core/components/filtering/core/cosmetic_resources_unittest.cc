// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/cosmetic_resources.h"

#include <algorithm>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::filtering {
namespace {

bool Has(const std::vector<std::string>& values, const char* needle) {
  return std::find(values.begin(), values.end(), needle) != values.end();
}

TEST(CosmeticResourcesTest, ParsesHideSelectorsExceptionsAndGenerichide) {
  const auto parsed = ParseUrlCosmeticResourcesJson(
      R"json({"hide_selectors":[".ad","#banner"],"procedural_actions":["ignored"],"exceptions":[".keep"],"injected_script":"","generichide":true})json");
  ASSERT_TRUE(parsed);
  EXPECT_TRUE(Has(parsed->hide_selectors, ".ad"));
  EXPECT_TRUE(Has(parsed->hide_selectors, "#banner"));
  EXPECT_TRUE(Has(parsed->exceptions, ".keep"));
  EXPECT_TRUE(parsed->generichide);
}

TEST(CosmeticResourcesTest, MissingFieldsAreEmptyAndGenerichideIsOff) {
  const auto parsed = ParseUrlCosmeticResourcesJson("{}");
  ASSERT_TRUE(parsed);
  EXPECT_TRUE(parsed->hide_selectors.empty());
  EXPECT_TRUE(parsed->exceptions.empty());
  EXPECT_FALSE(parsed->generichide);
}

TEST(CosmeticResourcesTest, ANonObjectIsNotResources) {
  EXPECT_FALSE(ParseUrlCosmeticResourcesJson("[]"));
  EXPECT_FALSE(ParseUrlCosmeticResourcesJson("not-json"));
  EXPECT_FALSE(ParseUrlCosmeticResourcesJson(""));
}

TEST(CosmeticResourcesTest, ParsesASelectorArray) {
  const auto parsed = ParseSelectorListJson(R"([".ad","#banner"])");
  ASSERT_TRUE(parsed);
  ASSERT_EQ(2u, parsed->size());
  EXPECT_EQ(".ad", (*parsed)[0]);
  EXPECT_EQ("#banner", (*parsed)[1]);
}

TEST(CosmeticResourcesTest, ANonArrayIsNotASelectorList) {
  EXPECT_FALSE(ParseSelectorListJson("{}"));
  EXPECT_FALSE(ParseSelectorListJson("not-json"));
}

}  // namespace
}  // namespace taffy::filtering
