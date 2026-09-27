// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/regular_profile_token.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(RegularProfileTokenTest, MatchesAndroidProfileResolverSerialization) {
  EXPECT_EQ("6SIbLvHkNmg3Ys9YzCiWoJoBzPX2iyHY1Qv3iJQiui8",
            OpaqueRegularProfileToken("Default"));
  EXPECT_EQ("FB4aJJKeShJPodVQmhBO44Jh7x7hbQmxBVRuZBFs2uM",
            OpaqueRegularProfileToken("Profile 7"));
}

TEST(RegularProfileTokenTest, RejectsMalformedOrEmptyTokens) {
  const std::string token = OpaqueRegularProfileToken("Profile 1");
  EXPECT_TRUE(IsValidOpaqueRegularProfileToken(token));
  EXPECT_FALSE(IsValidOpaqueRegularProfileToken(""));
  EXPECT_FALSE(IsValidOpaqueRegularProfileToken(token.substr(1)));
  EXPECT_FALSE(IsValidOpaqueRegularProfileToken(std::string(43, '+')));
  EXPECT_TRUE(OpaqueRegularProfileToken("").empty());
}

}  // namespace
}  // namespace taffy
