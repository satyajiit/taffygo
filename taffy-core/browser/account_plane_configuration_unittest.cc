// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account_plane_configuration.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

// The shipped state (decision 0252): no origin and no key are compiled in. An
// empty identity must read as "not configured" rather than as ready, because
// every account path gates on kReady before it builds a request.
TEST(AccountPlaneConfigurationTest, AnEmptyIdentityIsNeverReady) {
  EXPECT_EQ(ValidateAccountPlaneConfiguration("", ""),
            AccountPlaneConfigurationStatus::kInvalidOrigin);
  EXPECT_EQ(ValidateAccountPlaneConfiguration("", "sb_publishable_valid"),
            AccountPlaneConfigurationStatus::kInvalidOrigin);
}

TEST(AccountPlaneConfigurationTest, MissingKeyIsUnavailableBeforeOAuth) {
  EXPECT_EQ(
      ValidateAccountPlaneConfiguration("https://accounts.example.com", ""),
      AccountPlaneConfigurationStatus::kMissingPublishableKey);
}

TEST(AccountPlaneConfigurationTest, RefusesLegacyJwtAndRuntimeShapedOrigins) {
  EXPECT_EQ(ValidateAccountPlaneConfiguration("https://accounts.example.com",
                                              "eyJhbGciOiJIUzI1NiJ9.fake"),
            AccountPlaneConfigurationStatus::kInvalidPublishableKey);
  EXPECT_EQ(ValidateAccountPlaneConfiguration(
                "https://accounts.example.com/auth/v1", "sb_publishable_valid"),
            AccountPlaneConfigurationStatus::kInvalidOrigin);
  EXPECT_EQ(ValidateAccountPlaneConfiguration("https://accounts.example.com",
                                              "sb_publishable_valid"),
            AccountPlaneConfigurationStatus::kReady);
}

TEST(AccountPlaneConfigurationTest, ConsequentialTransportFailureIsUnknown) {
  EXPECT_EQ(ClassifyAccountNetworkResponse(0, 200, true),
            AccountNetworkResponseDisposition::kCompleted);
  EXPECT_EQ(ClassifyAccountNetworkResponse(0, 401, true),
            AccountNetworkResponseDisposition::kRejected);
  EXPECT_EQ(ClassifyAccountNetworkResponse(-7, 0, false),
            AccountNetworkResponseDisposition::kOutcomeUnknown);
  EXPECT_EQ(ClassifyAccountNetworkResponse(0, 503, true),
            AccountNetworkResponseDisposition::kOutcomeUnknown);
  EXPECT_EQ(ClassifyAccountNetworkResponse(0, 200, false),
            AccountNetworkResponseDisposition::kOutcomeUnknown);
}

TEST(AccountPlaneConfigurationTest, EmptyGoogleClientIdentityIsDisabled) {
  EXPECT_EQ(ValidateGoogleServerClientConfiguration(""),
            GoogleServerClientConfigurationStatus::kDisabled);
}

TEST(AccountPlaneConfigurationTest, ReviewedGoogleClientIdentityIsReady) {
  EXPECT_EQ(ValidateGoogleServerClientConfiguration(
                "1234567890-abcDEF.apps.googleusercontent.com"),
            GoogleServerClientConfigurationStatus::kReady);
}

TEST(AccountPlaneConfigurationTest, MalformedGoogleClientIdentityIsRefused) {
  EXPECT_EQ(ValidateGoogleServerClientConfiguration(
                "android-client.apps.googleusercontent.com/redirect"),
            GoogleServerClientConfigurationStatus::kMalformed);
  EXPECT_EQ(ValidateGoogleServerClientConfiguration(
                "noseparator.apps.googleusercontent.com"),
            GoogleServerClientConfigurationStatus::kMalformed);
  EXPECT_EQ(ValidateGoogleServerClientConfiguration(
                "bad_value.apps.googleusercontent.com"),
            GoogleServerClientConfigurationStatus::kMalformed);
  EXPECT_EQ(ValidateGoogleServerClientConfiguration(std::string(513u, 'a')),
            GoogleServerClientConfigurationStatus::kTooLong);
}

TEST(AccountPlaneConfigurationTest, PrivateProfilesRefuseAccountEffects) {
  EXPECT_TRUE(AccountPlaneAllowsProfile(false));
  EXPECT_FALSE(AccountPlaneAllowsProfile(true));
}

TEST(AccountPlaneConfigurationTest, RefusalSentencesReduceToClosedTags) {
  EXPECT_EQ(ClassifyAccountRefusalBody(
                R"({"error":"invalid request","error_description":"Bad ID token"})")
                .tag,
            AccountRefusalTag::kBadIdToken);
  EXPECT_EQ(ClassifyAccountRefusalBody(
                R"({"error":"invalid nonce","error_description":"Nonces mismatch"})")
                .tag,
            AccountRefusalTag::kNonceMismatch);
  EXPECT_EQ(ClassifyAccountRefusalBody(
                R"({"error":"invalid request","error_description":"Unacceptable audience in id_token"})")
                .tag,
            AccountRefusalTag::kUnacceptableAudience);
  EXPECT_EQ(ClassifyAccountRefusalBody(
                R"({"error_description":"Passed nonce and nonce in id_token should either both exist or not."})")
                .tag,
            AccountRefusalTag::kNoncePresenceMismatch);
  // A sentence is matched whole: a superstring says nothing this table knows.
  EXPECT_EQ(ClassifyAccountRefusalBody(
                R"({"error_description":"Bad ID token: expired"})")
                .tag,
            AccountRefusalTag::kUnrecognised);
  // The sentence is never part of the diagnostic.
  EXPECT_TRUE(ClassifyAccountRefusalBody(
                  R"({"error_description":"Bad ID token"})")
                  .code.empty());
}

TEST(AccountPlaneConfigurationTest, RefusalCodesAreKeptOnlyWhenCodeShaped) {
  const AccountRefusalDiagnostic disabled = ClassifyAccountRefusalBody(
      R"({"code":400,"error_code":"provider_disabled","msg":"Unsupported provider: provider is not enabled"})");
  EXPECT_EQ(disabled.tag, AccountRefusalTag::kProviderDisabled);
  EXPECT_EQ(disabled.code, "provider_disabled");

  const AccountRefusalDiagnostic stale = ClassifyAccountRefusalBody(
      R"({"code":400,"error_code":"refresh_token_already_used","msg":"Invalid Refresh Token: Already Used"})");
  EXPECT_EQ(stale.tag, AccountRefusalTag::kSessionOrTokenNotFound);
  EXPECT_EQ(stale.code, "refresh_token_already_used");

  const AccountRefusalDiagnostic coded = ClassifyAccountRefusalBody(
      R"({"code":400,"error_code":"bad_jwt","msg":"anything at all"})");
  EXPECT_EQ(coded.tag, AccountRefusalTag::kCoded);
  EXPECT_EQ(coded.code, "bad_jwt");

  // A code that is not shaped like one is dropped rather than kept.
  const AccountRefusalDiagnostic odd =
      ClassifyAccountRefusalBody(R"({"error_code":"Bad Code!","msg":"x"})");
  EXPECT_EQ(odd.tag, AccountRefusalTag::kUnrecognised);
  EXPECT_TRUE(odd.code.empty());
}

TEST(AccountPlaneConfigurationTest, RefusalBodiesOutsideTheShapeAreUnrecognised) {
  EXPECT_EQ(ClassifyAccountRefusalBody("").tag, AccountRefusalTag::kUnrecognised);
  EXPECT_EQ(ClassifyAccountRefusalBody("<html>refused</html>").tag,
            AccountRefusalTag::kUnrecognised);
  EXPECT_EQ(ClassifyAccountRefusalBody("[\"Bad ID token\"]").tag,
            AccountRefusalTag::kUnrecognised);
  const std::string large = "{\"error_description\":\"" +
                            std::string(kMaxAccountRefusalBodyBytes, 'a') +
                            "\"}";
  EXPECT_EQ(ClassifyAccountRefusalBody(large).tag,
            AccountRefusalTag::kUnrecognised);
}

TEST(AccountPlaneConfigurationTest, EveryRefusalTagPrintsItsOwnName) {
  EXPECT_EQ(AccountRefusalTagName(AccountRefusalTag::kUnacceptableAudience),
            "unacceptable_audience");
  EXPECT_EQ(AccountRefusalTagName(AccountRefusalTag::kCoded), "coded");
  EXPECT_EQ(AccountRefusalTagName(AccountRefusalTag::kUnrecognised),
            "unrecognised");
}

} // namespace taffy
