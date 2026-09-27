// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/class_id_tokens.h"

#include "base/containers/flat_set.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::filtering {
namespace {

TEST(ClassIdTokensTest, SplitsOnHtmlWhitespace) {
  EXPECT_TRUE(SplitClassAttribute("").empty());
  EXPECT_TRUE(SplitClassAttribute("   \t\n").empty());
  const std::vector<std::string> tokens =
      SplitClassAttribute("  ad\tslot\nhero\fwide\r ");
  ASSERT_EQ(4u, tokens.size());
  EXPECT_EQ("ad", tokens[0]);
  EXPECT_EQ("slot", tokens[1]);
  EXPECT_EQ("hero", tokens[2]);
  EXPECT_EQ("wide", tokens[3]);
}

TEST(ClassIdTokensTest, TakeUnseenRecordsAndDropsRepeats) {
  base::flat_set<std::string> seen;
  const std::vector<std::string> first =
      TakeUnseen(seen, {"ad", "slot", "ad", ""});
  ASSERT_EQ(2u, first.size());
  EXPECT_EQ("ad", first[0]);
  EXPECT_EQ("slot", first[1]);
  EXPECT_TRUE(TakeUnseen(seen, {"ad", "slot"}).empty());
  const std::vector<std::string> extra = TakeUnseen(seen, {"hero"});
  ASSERT_EQ(1u, extra.size());
  EXPECT_EQ("hero", extra[0]);
}

}  // namespace
}  // namespace taffy::filtering
