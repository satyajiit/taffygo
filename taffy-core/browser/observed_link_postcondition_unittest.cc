// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>

#include "taffy/components/intelligence/content/postcondition_checks.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

Origin Tuple(std::string serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = std::move(serialization);
  return origin;
}

UrlMetadata FullUrl(const Origin& origin, std::string address) {
  UrlMetadata metadata;
  metadata.origin = origin;
  metadata.disclosure = UrlDisclosure::kFullUrl;
  metadata.url = std::move(address);
  return metadata;
}

TEST(ObservedLinkPostconditionTest, RequiresTheExactAddressIncludingQuery) {
  const Origin destination_origin = Tuple("https://destination.test");
  const std::string destination =
      "https://destination.test/path?approved=yes#section";
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  Destination expected;
  expected.url_metadata = FullUrl(destination_origin, destination);
  postcondition.expected_destination = expected;
  postcondition.allowed_origins.push_back(destination_origin);

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = destination_origin;
  evidence.committed_url = FullUrl(
      destination_origin, "https://destination.test/path?approved=no#section");
  EXPECT_EQ(PostconditionCheck::kContradicted,
            CheckCommittedNavigation(postcondition, evidence));

  evidence.committed_url = FullUrl(destination_origin, destination);
  evidence.redirect_chain.push_back(
      {.origin = destination_origin,
       .url = FullUrl(destination_origin, destination)});
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckCommittedNavigation(postcondition, evidence));
}

TEST(ObservedLinkPostconditionTest, RedirectCannotLeaveAndReturnToGrant) {
  const Origin destination_origin = Tuple("https://destination.test");
  const std::string destination = "https://destination.test/exact";
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  Destination expected;
  expected.url_metadata = FullUrl(destination_origin, destination);
  postcondition.expected_destination = expected;
  postcondition.allowed_origins.push_back(destination_origin);

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = destination_origin;
  evidence.committed_url = FullUrl(destination_origin, destination);
  const Origin undeclared = Tuple("https://undeclared.test");
  evidence.redirect_chain = {
      {.origin = destination_origin,
       .url = FullUrl(destination_origin, destination)},
      {.origin = undeclared,
       .url = FullUrl(undeclared, "https://undeclared.test/collect")},
      {.origin = destination_origin,
       .url = FullUrl(destination_origin, destination)},
  };

  EXPECT_EQ(PostconditionCheck::kContradicted,
            CheckCommittedNavigation(postcondition, evidence));
}

// The measured phone case: a search engine's result link is the engine's own
// address, and following it lands on the site the result names.
TEST(ObservedLinkPostconditionTest, ARedirectorReachesTheSiteTheResultNames) {
  const Origin engine = Tuple("https://www.google.com");
  const std::string redirector = "https://www.google.com/url?q=official";
  const Origin site = Tuple("https://myaadhaar.uidai.gov.in");
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  Destination expected;
  expected.url_metadata.origin = engine;
  expected.url_metadata.disclosure = UrlDisclosure::kOriginOnly;
  postcondition.expected_destination = expected;
  postcondition.allowed_origins.push_back(engine);
  postcondition.allows_registrable_domain_siblings = true;
  postcondition.allows_redirected_landing = true;

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = site;
  evidence.committed_url = FullUrl(site, "https://myaadhaar.uidai.gov.in/");
  evidence.redirect_chain = {
      {.origin = engine, .url = FullUrl(engine, redirector)},
      {.origin = site, .url = FullUrl(site, "https://myaadhaar.uidai.gov.in/")},
  };
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckCommittedNavigation(postcondition, evidence));

  // With no redirect at all there is nothing for a site to have answered, so
  // the declared origin still stands.
  evidence.redirect_chain = {
      {.origin = site, .url = FullUrl(site, "https://myaadhaar.uidai.gov.in/")}};
  EXPECT_EQ(PostconditionCheck::kContradicted,
            CheckCommittedNavigation(postcondition, evidence));

  // A chain that did not start where the request was sent is not the site
  // answering it.
  evidence.redirect_chain = {
      {.origin = site, .url = FullUrl(site, "https://myaadhaar.uidai.gov.in/")},
      {.origin = site, .url = FullUrl(site, "https://myaadhaar.uidai.gov.in/")},
  };
  EXPECT_EQ(PostconditionCheck::kContradicted,
            CheckCommittedNavigation(postcondition, evidence));
}

// A typed address is offered by nobody, so it keeps the narrow rule.
TEST(ObservedLinkPostconditionTest, ATypedAddressStillKeepsItsOwnOrigin) {
  const Origin typed = Tuple("https://uidai.gov.in");
  const Origin elsewhere = Tuple("https://example.test");
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.allowed_origins.push_back(typed);
  postcondition.allows_registrable_domain_siblings = true;
  postcondition.allows_redirected_landing = false;

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = elsewhere;
  evidence.committed_url = FullUrl(elsewhere, "https://example.test/");
  evidence.redirect_chain = {
      {.origin = typed, .url = FullUrl(typed, "https://uidai.gov.in/")},
      {.origin = elsewhere, .url = FullUrl(elsewhere, "https://example.test/")},
  };
  EXPECT_EQ(PostconditionCheck::kContradicted,
            CheckCommittedNavigation(postcondition, evidence));
}

}  // namespace
}  // namespace taffy
