// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_credential_host.h"

#include <string>

#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "testing/gtest/include/gtest/gtest.h"

// The two bounds on an address a vendor names for one credential. Both are
// pure, so this file is a table rather than a fixture.

namespace taffy {
namespace {

constexpr char kSecondaryUrl[] = "https://vendor.example/second";

constexpr ProviderAuthVendor kLicensedVendor = {
    "test-licensed-host",
    ProviderAuthFlowKind::kDeviceCode,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "",
    "https://vendor.example/device",
    "https://vendor.example/token",
    kSecondaryUrl,
    ".api.vendor.example",
    "",
    "",
    "",
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
    "read:user",
};

// The question asked before either bound: was an address expected at all?
//
// It is answered from the row and from nothing else, which is the point. The
// address itself is absent in the case this exists for, so there is nothing to
// inspect — the only thing that can say whether the silence is ordinary or a
// loss is this binary's own table.
TEST(ProviderCredentialHostTest, OnlyALicensedRowExpectsAnAddress) {
  ScopedProviderAuthVendorForTesting scoped(&kLicensedVendor);
  EXPECT_TRUE(ProviderCredentialNamesItsOwnHost("test-licensed-host"));
  // A vendor with a flow but no licensed domain, and a name this binary has
  // never heard of. Neither expects an address, so neither is refused for
  // arriving without one.
  EXPECT_FALSE(ProviderCredentialNamesItsOwnHost("xai"));
  EXPECT_FALSE(ProviderCredentialNamesItsOwnHost("not-a-vendor"));
  EXPECT_FALSE(ProviderCredentialNamesItsOwnHost(""));
  // The one shipping row that does expect one, and the reason this predicate
  // exists: its catalog origin is the licensing parent of the domain below,
  // which is not an endpoint.
  EXPECT_TRUE(ProviderCredentialNamesItsOwnHost("github-copilot"));
}

TEST(ProviderCredentialHostTest, OnlyALicensedRowMayNameAnAddressAtAll) {
  ScopedProviderAuthVendorForTesting scoped(&kLicensedVendor);
  EXPECT_TRUE(ProviderCredentialHostLicensed("test-licensed-host",
                                             "https://one.api.vendor.example"));
  // A vendor with no licensed domain names nothing, whatever it answers with,
  // and neither does one this binary has never heard of.
  EXPECT_FALSE(
      ProviderCredentialHostLicensed("xai", "https://one.api.vendor.example"));
  EXPECT_FALSE(ProviderCredentialHostLicensed(
      "not-a-vendor", "https://one.api.vendor.example"));
}

TEST(ProviderCredentialHostTest, AnAddressOutsideTheLicensedDomainIsRefused) {
  ScopedProviderAuthVendorForTesting scoped(&kLicensedVendor);
  // The suffix carries its own leading dot, which is what keeps a name that
  // merely ends with the licensed letters — a domain somebody else registers —
  // from matching.
  EXPECT_FALSE(ProviderCredentialHostLicensed(
      "test-licensed-host", "https://evilapi.vendor.example"));
  // The licensed domain itself carries no label in front of it.
  EXPECT_FALSE(ProviderCredentialHostLicensed("test-licensed-host",
                                              "https://api.vendor.example"));
  EXPECT_FALSE(ProviderCredentialHostLicensed("test-licensed-host",
                                              "https://attacker.example"));
}

TEST(ProviderCredentialHostTest, OnlyACanonicalHttpsOriginIsAnAddress) {
  ScopedProviderAuthVendorForTesting scoped(&kLicensedVendor);
  for (const char *rejected : {
           "http://one.api.vendor.example",
           "https://one.api.vendor.example/v1",
           "https://one.api.vendor.example?x=1",
           "https://one.api.vendor.example:443",
           "https://ONE.api.vendor.example",
           "https://user:pass@one.api.vendor.example",
           "one.api.vendor.example",
           "",
       }) {
    EXPECT_FALSE(
        ProviderCredentialHostLicensed("test-licensed-host", rejected))
        << rejected;
  }
}

// The honesty bound: what this product says about where a request went is
// composed from the address the isolated core named, so a substitution may
// only narrow inside that address's domain.
TEST(ProviderCredentialHostTest, ASubstitutionStaysBeneathTheCatalogAddress) {
  EXPECT_TRUE(ProviderCredentialHostAddresses("https://vendor.example",
                                              "https://one.vendor.example"));
  EXPECT_TRUE(ProviderCredentialHostAddresses("https://vendor.example",
                                              "https://vendor.example"));
  EXPECT_FALSE(ProviderCredentialHostAddresses("https://vendor.example",
                                               "https://elsewhere.example"));
  EXPECT_FALSE(ProviderCredentialHostAddresses("https://vendor.example",
                                               "https://notvendor.example"));
  // A parent of the catalog address is not beneath it.
  EXPECT_FALSE(ProviderCredentialHostAddresses("https://one.vendor.example",
                                               "https://vendor.example"));
  EXPECT_FALSE(ProviderCredentialHostAddresses("https://vendor.example",
                                               "http://one.vendor.example"));
}

}  // namespace
}  // namespace taffy
