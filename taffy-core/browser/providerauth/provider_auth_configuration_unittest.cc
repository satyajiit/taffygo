// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_auth_configuration.h"

#include <string>
#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The compiled vendor table is a security surface (decision 0081): every
// origin a sign-in leg posts a grant or refresh token to must be a binary
// constant, https, and owned by the vendor. Decision 0095 adds two facts the
// table must also be honest about — whose client identity a row borrows, and
// the date somebody read that vendor's terms — and makes the second of them
// the switch. These tests pin all of it, so a table edit that weakens one is
// a named failure rather than a quietly different product.

namespace taffy {
namespace {

constexpr std::string_view kVendorIds[] = {
    "anthropic", "github-copilot", "kimi-coding", "openai", "openrouter", "xai"};

// A row shaped like the ones that will exist once a review lands: an
// identity, a date, and an address to intercept.
constexpr ProviderAuthVendor kReviewedVendor = {
    "test-reviewed",
    ProviderAuthFlowKind::kPkce,
    ProviderAuthRedirectKind::kTabInterception,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "https://vendor.example/authorize",
    "",
    "https://vendor.example/token",
    "",
    "",
    "",
    "",
    "https://vendor.example/callback",
    "",
    "Someone else's command-line tool",
    {},
    {},
    "2026-08-29",
    /*authorization_displays_code=*/false,
    /*state_is_pkce_verifier=*/false,
    /*presents_client_identity=*/true,
    "test-client",
    "",
    "profile",
};

TEST(ProviderAuthConfigurationTest, TheSevenVendorsAreRegisteredByName) {
  for (const std::string_view id : kVendorIds) {
    EXPECT_NE(nullptr, ProviderAuthVendorFor(id)) << id;
  }
  EXPECT_EQ(nullptr, ProviderAuthVendorFor("moonshot"));
  EXPECT_EQ(nullptr, ProviderAuthVendorFor(""));
  EXPECT_EQ(nullptr, ProviderAuthVendorFor("XAI"));
}

TEST(ProviderAuthConfigurationTest, EveryEndpointIsHttpsAndFlowShaped) {
  for (const std::string_view id : kVendorIds) {
    const ProviderAuthVendor *vendor = ProviderAuthVendorFor(id);
    ASSERT_NE(nullptr, vendor) << id;
    EXPECT_EQ(id, std::string_view(vendor->provider_id));
    // The token endpoint is where a grant becomes a refresh token; every
    // vendor has one and it is https.
    EXPECT_TRUE(GURL(vendor->token_url).SchemeIs("https")) << id;
    if (vendor->flow == ProviderAuthFlowKind::kDeviceCode) {
      EXPECT_TRUE(GURL(vendor->device_code_url).SchemeIs("https")) << id;
      EXPECT_STREQ("", vendor->authorization_url) << id;
    } else {
      EXPECT_TRUE(GURL(vendor->authorization_url).SchemeIs("https")) << id;
    }
    if (vendor->secondary_token_url[0] != '\0') {
      EXPECT_TRUE(GURL(vendor->secondary_token_url).SchemeIs("https")) << id;
    }
    if (vendor->revocation_url[0] != '\0') {
      EXPECT_TRUE(GURL(vendor->revocation_url).SchemeIs("https")) << id;
    }
    // An identity this product did not register is named as somebody else's.
    // The scope is deliberately not asserted with it: one vendor's
    // authorization request takes a client and no scope at all, and a rule
    // demanding one would be satisfied by inventing a scope that vendor
    // refuses.
    if (vendor->client_id[0] != '\0') {
      EXPECT_NE('\0', vendor->borrowed_from[0]) << id;
      EXPECT_TRUE(vendor->presents_client_identity) << id;
    }
    // A client secret is a field of the exchange, so it cannot exist without
    // the identity it belongs to. It is never a way to carry a credential.
    if (vendor->client_secret[0] != '\0') {
      EXPECT_NE('\0', vendor->client_id[0]) << id;
    }
    // Only the strategy that carries an address may rename the parameter it
    // is presented in, and a renamed parameter with no address to put in it
    // would name a column nothing fills.
    if (vendor->authorization_redirect_param[0] != '\0') {
      EXPECT_NE('\0', vendor->registered_redirect_uri[0]) << id;
      EXPECT_EQ(ProviderAuthRedirectKind::kTabInterception, vendor->redirect)
          << id;
    }
    // The second exchange's two vendor facts belong to the second exchange.
    // A scheme or a header on a row with no second exchange is a permission
    // nothing reads, which is the kind still there when somebody later adds
    // the code that would have read it.
    if (vendor->secondary_token_url[0] == '\0') {
      EXPECT_STREQ("", vendor->secondary_authorization_scheme) << id;
      EXPECT_TRUE(vendor->secondary_request_headers.empty()) << id;
    }
  }
}

// The vendors whose terms have been read and dated, and which are therefore
// offered. Naming them here rather than counting them is the point: dating a
// row is an owner's act (decision 0081), and a row that acquires a date
// without anybody deciding to give it one should fail this list rather than
// slip past a number that still adds up.
//
// All six are named today, and the list is still a list. It is what a row
// added tomorrow fails against — a new vendor arrives undated, and arriving
// dated is exactly the accident this exists to catch — and it is what a
// withdrawn review is removed from.
constexpr std::string_view kDatedVendorIds[] = {
    "anthropic", "github-copilot", "kimi-coding", "openai", "openrouter", "xai",
};

bool IsDated(std::string_view id) {
  for (const std::string_view dated : kDatedVendorIds) {
    if (dated == id) {
      return true;
    }
  }
  return false;
}

TEST(ProviderAuthConfigurationTest, ARowIsOfferedExactlyWhenItsTermsAreDated) {
  for (const std::string_view id : kVendorIds) {
    const ProviderAuthVendor *vendor = ProviderAuthVendorFor(id);
    ASSERT_NE(nullptr, vendor) << id;
    if (IsDated(id)) {
      // Dated, and therefore offered — which also requires an identity, so
      // this arm is the one that would catch a date written onto a row that
      // has nothing to present.
      EXPECT_STRNE("", vendor->terms_reviewed_on) << id;
      EXPECT_TRUE(ProviderAuthVendorRegistered(id)) << id;
    } else {
      EXPECT_STREQ("", vendor->terms_reviewed_on) << id;
      EXPECT_FALSE(ProviderAuthVendorRegistered(id)) << id;
    }
  }
  EXPECT_FALSE(ProviderAuthVendorRegistered("missing"));
}

TEST(ProviderAuthConfigurationTest, ADateWithoutAnIdentityOffersNothing) {
  ProviderAuthVendor vendor = kReviewedVendor;
  vendor.client_id = "";
  ScopedProviderAuthVendorForTesting scoped(&vendor);
  EXPECT_FALSE(ProviderAuthVendorRegistered("test-reviewed"));
}

TEST(ProviderAuthConfigurationTest, AnIdentityWithoutADateOffersNothing) {
  ProviderAuthVendor vendor = kReviewedVendor;
  vendor.terms_reviewed_on = "";
  ScopedProviderAuthVendorForTesting scoped(&vendor);
  EXPECT_FALSE(ProviderAuthVendorRegistered("test-reviewed"));
}

TEST(ProviderAuthConfigurationTest, ADatedIdentityIsWhatOffersARow) {
  ScopedProviderAuthVendorForTesting scoped(&kReviewedVendor);
  EXPECT_TRUE(ProviderAuthVendorRegistered("test-reviewed"));
}

// One vendor's authorization request names no client at all, so its empty
// identity is a finished state rather than an absent one and a dated review
// is the whole of its gate.
TEST(ProviderAuthConfigurationTest, ARowThatPresentsNoClientNeedsNoIdentity) {
  ProviderAuthVendor vendor = kReviewedVendor;
  vendor.presents_client_identity = false;
  vendor.borrowed_from = "";
  vendor.client_id = "";
  ScopedProviderAuthVendorForTesting scoped(&vendor);
  EXPECT_TRUE(ProviderAuthVendorRegistered("test-reviewed"));
}

// The product's own callback belongs to a registration the product has not
// made. A borrowed client's registered redirect is not ours to change, so no
// compiled row may be handed ours (decision 0095 section 2).
TEST(ProviderAuthConfigurationTest, NoCompiledRowIsGivenTheProductCallback) {
  for (const std::string_view id : kVendorIds) {
    const ProviderAuthVendor *vendor = ProviderAuthVendorFor(id);
    ASSERT_NE(nullptr, vendor) << id;
    const std::string redirect(ProviderAuthRedirectFor(*vendor));
    EXPECT_FALSE(redirect.starts_with("com.taffygo.browser:")) << id;
    EXPECT_FALSE(std::string(vendor->registered_redirect_uri)
                     .starts_with("com.taffygo.browser:"))
        << id;
  }
}

TEST(ProviderAuthConfigurationTest, OnlyAnInterceptionRowPresentsAnAddress) {
  ScopedProviderAuthVendorForTesting scoped(&kReviewedVendor);
  EXPECT_EQ("https://vendor.example/callback",
            ProviderAuthRedirectFor(kReviewedVendor));

  ProviderAuthVendor manual = kReviewedVendor;
  manual.redirect = ProviderAuthRedirectKind::kManualCode;
  EXPECT_TRUE(ProviderAuthRedirectFor(manual).empty());

  // The reserved strategy is the only way the product's own callback is ever
  // handed out, and no compiled row uses it.
  ProviderAuthVendor own = kReviewedVendor;
  own.redirect = ProviderAuthRedirectKind::kCustomScheme;
  EXPECT_EQ("com.taffygo.browser://provider-auth",
            ProviderAuthRedirectFor(own));
}

TEST(ProviderAuthConfigurationTest, TheTestOverrideIsScopedAndRestored) {
  EXPECT_EQ(nullptr, ProviderAuthVendorFor("test-reviewed"));
  {
    ScopedProviderAuthVendorForTesting scoped(&kReviewedVendor);
    EXPECT_EQ(&kReviewedVendor, ProviderAuthVendorFor("test-reviewed"));
    EXPECT_TRUE(ProviderAuthVendorRegistered("test-reviewed"));
    // The compiled table is consulted after the override, never replaced.
    EXPECT_NE(nullptr, ProviderAuthVendorFor("xai"));
  }
  EXPECT_EQ(nullptr, ProviderAuthVendorFor("test-reviewed"));
}

}  // namespace
}  // namespace taffy
