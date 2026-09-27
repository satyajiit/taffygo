// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/hide_stylesheet.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::filtering {
namespace {

TEST(HideStylesheetTest, EmptyInputProducesNoSheet) {
  EXPECT_TRUE(BuildHideStylesheet({}).empty());
  EXPECT_TRUE(BuildHideStylesheet({""}).empty());
}

TEST(HideStylesheetTest, OneSelectorHidesWithImportant) {
  EXPECT_EQ(".ad { display: none !important; }",
            BuildHideStylesheet({".ad"}));
}

TEST(HideStylesheetTest, SeveralSelectorsJoinOnANewline) {
  EXPECT_EQ(".ad,\n#banner { display: none !important; }",
            BuildHideStylesheet({".ad", "#banner"}));
}

TEST(HideStylesheetTest, EmptySelectorsAreSkipped) {
  EXPECT_EQ(".ad { display: none !important; }",
            BuildHideStylesheet({"", ".ad", ""}));
}

TEST(HideStylesheetTest, BracesAndClosingMarkupAreRejected) {
  EXPECT_TRUE(BuildHideStylesheet({".ad{color:red}"}).empty());
  EXPECT_TRUE(BuildHideStylesheet({"} body"}).empty());
  EXPECT_TRUE(BuildHideStylesheet({"</style>"}).empty());
  EXPECT_EQ(".ad { display: none !important; }",
            BuildHideStylesheet({".ad{x}", ".ad", "</style>"}));
}

}  // namespace
}  // namespace taffy::filtering
