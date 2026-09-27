// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/auth_callback_parser.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(AccountAuthCallbackParserTest, AcceptsStrictQueryOnlySuccess) {
  const auto parsed = ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&code=one-use-code");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->state, "state_1");
  EXPECT_EQ(parsed->authorization_code, "one-use-code");
  EXPECT_EQ(parsed->outcome, AccountAuthCallbackOutcome::kAuthorizationCode);
}

TEST(AccountAuthCallbackParserTest, AcceptsMatchingSupabaseErrorFragment) {
  const auto parsed = ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&error=access_denied&"
      "error_code=access_denied#error=access_denied&"
      "error_code=access_denied&error_description=cancelled&sb=1");
  ASSERT_TRUE(parsed);
  EXPECT_FALSE(parsed->authorization_code);
  EXPECT_EQ(parsed->outcome, AccountAuthCallbackOutcome::kDenied);
}

TEST(AccountAuthCallbackParserTest, MapsUnknownProviderErrorWithoutDetails) {
  const auto parsed = ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&error_code=provider_failure#"
      "error=server_error&error_description=private%20detail&sb=1");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->outcome, AccountAuthCallbackOutcome::kProviderError);
}

TEST(AccountAuthCallbackParserTest, RejectsSuccessFragmentAndUnknownFields) {
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&code=code#sb=1"));
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&error=denied&token=secret"));
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&error=denied#"
      "error=denied&sb=1#ignored=tail"));
}

TEST(AccountAuthCallbackParserTest, RejectsDuplicateOrConflictingErrorFacts) {
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&error=one&error=one"));
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=state_1&error=one#error=two&sb=1"));
}

TEST(AccountAuthCallbackParserTest, RejectsMissingOrMalformedState) {
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?error=access_denied"));
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth?state=not%20opaque&error=access_denied"));
}

// The provider twin (decision 0081) is the same parser over the other
// prefix, so it carries one success case, one denial case, and the fact
// that matters most: neither parser accepts the other plane's redirect.
TEST(AccountAuthCallbackParserTest, TheProviderPrefixParsesTheSameShapes) {
  const auto success = ParseProviderAuthCallback(
      "com.taffygo.browser://provider-auth?state=state_1&code=one-use-code");
  ASSERT_TRUE(success);
  EXPECT_EQ(success->state, "state_1");
  EXPECT_EQ(success->authorization_code, "one-use-code");
  EXPECT_EQ(success->outcome, AccountAuthCallbackOutcome::kAuthorizationCode);

  const auto denied = ParseProviderAuthCallback(
      "com.taffygo.browser://provider-auth?state=state_1&error=access_denied");
  ASSERT_TRUE(denied);
  EXPECT_EQ(denied->outcome, AccountAuthCallbackOutcome::kDenied);
}

TEST(AccountAuthCallbackParserTest, NeitherParserClaimsTheOtherPlanesPrefix) {
  EXPECT_FALSE(ParseProviderAuthCallback(
      "com.taffygo.browser://auth?state=state_1&code=one-use-code"));
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://provider-auth?state=state_1&code=one-use-code"));
  // The account prefix is a prefix of nothing: a path that merely begins
  // with it is not it.
  EXPECT_FALSE(ParseAccountAuthCallback(
      "com.taffygo.browser://auth-extra?state=state_1&code=one-use-code"));
}

}  // namespace
}  // namespace taffy
