// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/asset_delivery_configuration.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(AssetDeliveryConfigurationTest, AnAbsentOriginIsAbsentRatherThanMalformed) {
  // The two are not the same finding. Absent is the shipped default and means
  // the plane refuses politely; malformed means somebody configured a host and
  // got it wrong, which is a build to stop.
  EXPECT_EQ(ValidateAssetDeliveryConfiguration(""),
            AssetDeliveryConfigurationStatus::kAbsent);
}

TEST(AssetDeliveryConfigurationTest, AWellFormedOriginIsReady) {
  EXPECT_EQ(ValidateAssetDeliveryConfiguration("https://assets.example"),
            AssetDeliveryConfigurationStatus::kReady);
}

TEST(AssetDeliveryConfigurationTest, EverythingThatIsNotABareHttpsOriginIsRefused) {
  for (const char* origin : {
           "http://assets.example",          // not https
           "https://assets.example/prefix",  // carries a path
           "https://user@assets.example",    // carries credentials
           "https://assets.example/?a=b",    // carries a query
           "https://assets.example/#top",    // carries a fragment
           "assets.example",                 // no scheme
           "https://",                       // no host
           "not a url",
       }) {
    EXPECT_EQ(ValidateAssetDeliveryConfiguration(origin),
              AssetDeliveryConfigurationStatus::kMalformedOrigin)
        << origin;
  }
}

TEST(AssetDeliveryConfigurationTest, APathIsJoinedOntoTheOrigin) {
  const GURL url =
      AssetUrl("https://assets.example", "python/3.14.2/stdlib-arm64.zip");
  EXPECT_EQ(url.spec(), "https://assets.example/python/3.14.2/stdlib-arm64.zip");
}

TEST(AssetDeliveryConfigurationTest, NoPathCanLeaveTheOrigin) {
  // Every one of these is a way a path could have named a different host, a
  // different scheme or a parent directory. All are refused rather than
  // escaped: a path that needed escaping did not come from the catalog.
  for (const char* path : {
           "../secrets",
           "a/../../b",
           "/leading",
           "trailing/",
           "a//b",
           "//other.example/x",
           "https://other.example/x",
           "a/./b",
           "a b",
           "a?b",
           "a#b",
           "a%2fb",
           "",
       }) {
    EXPECT_FALSE(AssetUrl("https://assets.example", path).is_valid()) << path;
  }
}

TEST(AssetDeliveryConfigurationTest, NoPathResolvesWhenNoOriginIsConfigured) {
  EXPECT_FALSE(AssetUrl("", "python/3.14.2/stdlib-arm64.zip").is_valid());
}

}  // namespace
}  // namespace taffy
