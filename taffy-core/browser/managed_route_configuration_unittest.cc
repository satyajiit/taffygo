// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/managed_route_configuration.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(ManagedRouteConfigurationTest, EmptyIsADisableNotAnError) {
  EXPECT_EQ(ValidateManagedRouteConfiguration(""),
            ManagedRouteConfigurationStatus::kDisabled);
}

TEST(ManagedRouteConfigurationTest, ABareHttpsOriginIsReady) {
  EXPECT_EQ(ValidateManagedRouteConfiguration("https://route.example.com"),
            ManagedRouteConfigurationStatus::kReady);
  // The one spelling with a trailing slash still names the bare origin.
  EXPECT_EQ(ValidateManagedRouteConfiguration("https://route.example.com/"),
            ManagedRouteConfigurationStatus::kReady);
}

TEST(ManagedRouteConfigurationTest, AnythingBeyondAnOriginIsInvalid) {
  for (const char* origin : {
           "http://route.example.com",
           "https://route.example.com/v1",
           "https://route.example.com?key=1",
           "https://route.example.com#frag",
           "https://user@route.example.com",
           "https://user:secret@route.example.com",
           "not a url",
       }) {
    EXPECT_EQ(ValidateManagedRouteConfiguration(origin),
              ManagedRouteConfigurationStatus::kInvalidOrigin)
        << origin;
  }
}

TEST(ManagedRouteConfigurationTest, TheEndpointCheckFailsClosed) {
  // A disabled or invalid configuration matches no endpoint at all — the
  // caller is deciding whether to attach a bearer token, and "misconfigured"
  // must not read as "matches".
  EXPECT_FALSE(IsManagedOriginEndpoint("", "https://route.example.com"));
  EXPECT_FALSE(IsManagedOriginEndpoint("http://route.example.com",
                                       "https://route.example.com"));
}

TEST(ManagedRouteConfigurationTest, TheEndpointCheckMatchesTheOriginExactly) {
  EXPECT_TRUE(IsManagedOriginEndpoint("https://route.example.com",
                                      "https://route.example.com"));
  EXPECT_FALSE(IsManagedOriginEndpoint("https://route.example.com",
                                       "https://evil.example.com"));
  EXPECT_FALSE(IsManagedOriginEndpoint("https://route.example.com",
                                       "https://route.example.com.evil.test"));
  EXPECT_FALSE(IsManagedOriginEndpoint("https://route.example.com", ""));
  EXPECT_FALSE(
      IsManagedOriginEndpoint("https://route.example.com", "not a url"));
}

}  // namespace
}  // namespace taffy
