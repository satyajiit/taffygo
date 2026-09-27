// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reading one claim out of a credential nobody verified.
//
// The rule under test is narrow and worth stating before the cases: this
// reads a shape, never a proof. Nothing here checks a signature, an issuer or
// an expiry, because the value's only use is telling the issuer which of its
// own accounts a credential belongs to, and an issuer that disagrees with its
// own token refuses the request. What must hold is that no malformed document
// can turn into a header the caller did not write.

#include "taffy/browser/providerauth/provider_auth_claims.h"

#include <string>
#include <string_view>

#include "base/base64url.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::provider_auth {
namespace {

constexpr char kNamespace[] = "https://api.openai.com/auth";
constexpr char kKey[] = "chatgpt_account_id";

// A compact JWS whose payload is `payload`. The header and signature are
// filler: this reader never looks at either, and a test that supplied real
// ones would suggest it did.
std::string TokenWith(std::string_view payload) {
  std::string encoded;
  base::Base64UrlEncode(payload, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &encoded);
  return "aGVhZGVy." + encoded + ".c2ln";
}

TEST(ProviderAuthClaimsTest, TheNestedClaimIsReadWhole) {
  // The namespace is a URL, so it contains dots. A path-style lookup would
  // split it into segments naming nothing and find no claim at all — which
  // would read as a vendor that stopped sending one.
  const std::string token = TokenWith(
      R"({"sub":"u-1","https://api.openai.com/auth":)"
      R"({"chatgpt_account_id":"acct-9","chatgpt_plan_type":"pro"}})");

  EXPECT_EQ(ReadStringClaim(token, kNamespace, kKey),
            std::optional<std::string>("acct-9"));
}

TEST(ProviderAuthClaimsTest, ACredentialThatIsNotATokenReadsNothing) {
  // A pasted API key reaches this function on any family whose route asks for
  // a claim, and must leave it empty rather than mistaken for something
  // structured.
  for (const char* credential : {
           "sk-not-a-token",
           "",
           "one.two",
           "one.two.three.four",
           "..",
           "one..three",
       }) {
    EXPECT_FALSE(ReadStringClaim(credential, kNamespace, kKey).has_value())
        << credential;
  }
}

TEST(ProviderAuthClaimsTest, AMalformedPayloadReadsNothing) {
  EXPECT_FALSE(ReadStringClaim("aGVhZGVy.!!!!.c2ln", kNamespace, kKey));
  EXPECT_FALSE(ReadStringClaim(TokenWith("not json"), kNamespace, kKey));
  EXPECT_FALSE(ReadStringClaim(TokenWith("[1,2,3]"), kNamespace, kKey));
  EXPECT_FALSE(ReadStringClaim(TokenWith(R"({"sub":"u-1"})"), kNamespace, kKey));
  // The namespace is present and is not an object.
  EXPECT_FALSE(ReadStringClaim(
      TokenWith(R"({"https://api.openai.com/auth":"acct-9"})"), kNamespace,
      kKey));
  // The namespace is an object and the claim is not a string.
  EXPECT_FALSE(ReadStringClaim(
      TokenWith(R"({"https://api.openai.com/auth":{"chatgpt_account_id":7}})"),
      kNamespace, kKey));
}

TEST(ProviderAuthClaimsTest, AValueThatCouldNotBeAHeaderIsRefused) {
  // The point of the check: a claim carrying a line ending would otherwise
  // become part of the request rather than part of a value, and a token is a
  // remote party's document.
  const std::string injected = TokenWith(
      R"({"https://api.openai.com/auth":)"
      R"({"chatgpt_account_id":"acct-9\r\nx-admin: 1"}})");
  EXPECT_FALSE(ReadStringClaim(injected, kNamespace, kKey));

  const std::string empty = TokenWith(
      R"({"https://api.openai.com/auth":{"chatgpt_account_id":""}})");
  EXPECT_FALSE(ReadStringClaim(empty, kNamespace, kKey));

  const std::string oversized = TokenWith(
      R"({"https://api.openai.com/auth":{"chatgpt_account_id":")" +
      std::string(kMaxClaimValueBytes + 1, 'a') + R"("}})");
  EXPECT_FALSE(ReadStringClaim(oversized, kNamespace, kKey));

  // One byte under the bound is fine, so the refusal above is the bound and
  // not the shape.
  const std::string largest = TokenWith(
      R"({"https://api.openai.com/auth":{"chatgpt_account_id":")" +
      std::string(kMaxClaimValueBytes, 'a') + R"("}})");
  EXPECT_TRUE(ReadStringClaim(largest, kNamespace, kKey).has_value());
}

TEST(ProviderAuthClaimsTest, APaddedPayloadIsRefused) {
  // A compact JWS carries unpadded base64url. Admitting a padded segment
  // would be admitting a shape the specification forbids, which is how a
  // reader ends up disagreeing with the party that wrote the token.
  //
  // The account id is two digits, not one, and deliberately: base64 pads only
  // when the input length is not a multiple of three, so the one-digit
  // payload this test first used encoded to no padding at all and asserted
  // nothing. The `ASSERT_NE` below is what caught that and stays for it.
  constexpr std::string_view kPayload =
      R"({"https://api.openai.com/auth":{"chatgpt_account_id":"acct-99"}})";
  static_assert(kPayload.size() % 3 != 0, "a padded encoding needs padding");
  std::string padded;
  base::Base64UrlEncode(kPayload, base::Base64UrlEncodePolicy::INCLUDE_PADDING,
                        &padded);
  ASSERT_NE(padded.find('='), std::string::npos);

  EXPECT_FALSE(ReadStringClaim("aGVhZGVy." + padded + ".c2ln", kNamespace, kKey));
  // The same payload unpadded is read, so the refusal above is the padding
  // and not the document.
  std::string unpadded;
  base::Base64UrlEncode(kPayload, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &unpadded);
  EXPECT_EQ(ReadStringClaim("aGVhZGVy." + unpadded + ".c2ln", kNamespace, kKey),
            std::optional<std::string>("acct-99"));
}

}  // namespace
}  // namespace taffy::provider_auth
