// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/postcondition_checks.h"

#include "testing/gtest/include/gtest/gtest.h"

// Where a navigation is allowed to have landed (protocol section 11.6).
//
// Split from `postcondition_checks_unittest.cc`, which covers every other
// postcondition class, because this one question carries the most cases: a
// typed address is a request, and the whole of the difficulty is that a site
// answers it where it chooses to while a redirect somewhere else must still be
// caught. Nothing here constructs a renderer acknowledgement, because there is
// nothing in the evidence types to construct it with.

namespace taffy {
namespace {

Origin Tuple(const char* serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = serialization;
  return origin;
}

Destination DestinationAt(const char* serialization,
                          UrlDisclosure disclosure,
                          const char* path) {
  Destination destination;
  destination.url_metadata.origin = Tuple(serialization);
  destination.url_metadata.disclosure = disclosure;
  if (disclosure != UrlDisclosure::kOriginOnly) {
    destination.url_metadata.path = path;
  }
  return destination;
}

// A typed address whose site answers where it chooses to.
Postcondition TypedNavigateTo(const char* origin) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.allowed_origins.push_back(Tuple(origin));
  postcondition.expected_destination =
      DestinationAt(origin, UrlDisclosure::kOriginOnly, "");
  postcondition.allows_registrable_domain_siblings = true;
  return postcondition;
}

NavigationEvidence CommittedAt(const char* origin) {
  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = Tuple(origin);
  evidence.committed_url.origin = evidence.committed_origin;
  evidence.committed_url.disclosure = UrlDisclosure::kOriginAndPath;
  evidence.committed_url.path = "/en";
  return evidence;
}

TEST(PostconditionChecksTest, NavigationPendingUntilSomethingCommits) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.allowed_origins.push_back(Tuple("https://primary.taffy.test"));
  EXPECT_EQ(CheckCommittedNavigation(postcondition, NavigationEvidence()),
            PostconditionCheck::kPending);
}

TEST(PostconditionChecksTest, NavigationToADisallowedOriginIsContradicted) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.allowed_origins.push_back(Tuple("https://primary.taffy.test"));

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = Tuple("https://hostile.taffy.test");
  // It went somewhere, and not where the capability allowed. A contradiction,
  // not a timeout: the effect happened and it was the wrong one.
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kContradicted);
  EXPECT_EQ(ContradictionCodeFor(postcondition.kind),
            ActionResultCode::kDestinationChanged);
}

TEST(PostconditionChecksTest, AnErrorDocumentIsNotTheDeclaredDestination) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.allowed_origins.push_back(Tuple("https://primary.taffy.test"));

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.is_error_page = true;
  evidence.committed_origin = Tuple("https://primary.taffy.test");
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kContradicted);
}

TEST(PostconditionChecksTest, OriginOnlyDisclosureIsSatisfiedByAnOriginMatch) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.expected_destination = DestinationAt(
      "https://primary.taffy.test", UrlDisclosure::kOriginOnly, "");

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = Tuple("https://primary.taffy.test");
  evidence.committed_url.origin = evidence.committed_origin;
  evidence.committed_url.disclosure = UrlDisclosure::kOriginAndPath;
  evidence.committed_url.path = "/anything";
  // Demanding a path the policy never disclosed would turn a privacy decision
  // into a reliability bug.
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kSatisfied);
}

TEST(PostconditionChecksTest, PathDisclosureComparesThePath) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  postcondition.expected_destination =
      DestinationAt("https://primary.taffy.test", UrlDisclosure::kOriginAndPath,
                    "/product/lamp.html");

  NavigationEvidence evidence;
  evidence.committed = true;
  evidence.committed_origin = Tuple("https://primary.taffy.test");
  evidence.committed_url.origin = evidence.committed_origin;
  evidence.committed_url.disclosure = UrlDisclosure::kOriginAndPath;
  evidence.committed_url.path = "/forms/checkout-sensitive.html";
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kContradicted);

  evidence.committed_url.path = "/product/lamp.html";
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kSatisfied);
}

// --- new tab ----------------------------------------------------------------

TEST(PostconditionChecksTest, ASiteAnswersAtAHostInsideItsOwnDomain) {
  // Measured: `https://uidai.gov.in/` answers 307 to `https://uidai.gov.in/en`,
  // and an apex that sends a person to `www` is the ordinary shape of the web.
  // Byte equality contradicted the first and origin equality the second, so a
  // typed navigate only ever succeeded against a site that never redirected.
  const Postcondition postcondition = TypedNavigateTo("https://uidai.gov.in");
  EXPECT_EQ(CheckCommittedNavigation(postcondition,
                                     CommittedAt("https://uidai.gov.in")),
            PostconditionCheck::kSatisfied);
  EXPECT_EQ(CheckCommittedNavigation(postcondition,
                                     CommittedAt("https://www.uidai.gov.in")),
            PostconditionCheck::kSatisfied);
  EXPECT_EQ(CheckCommittedNavigation(
                postcondition, CommittedAt("https://myaadhaar.uidai.gov.in")),
            PostconditionCheck::kSatisfied);
}

TEST(PostconditionChecksTest, AnotherSiteIsStillAContradiction) {
  const Postcondition postcondition = TypedNavigateTo("https://uidai.gov.in");
  for (const char* elsewhere : {
           "https://uidai.gov.in.attacker.test",
           "https://attacker.test",
           // A different registrable domain under the same suffix.
           "https://other.gov.in",
           // The same host over http: a downgrade is not the site answering.
           "http://uidai.gov.in",
           // The same host on another port is another origin.
           "https://uidai.gov.in:8443",
       }) {
    SCOPED_TRACE(elsewhere);
    EXPECT_EQ(CheckCommittedNavigation(postcondition, CommittedAt(elsewhere)),
              PostconditionCheck::kContradicted);
  }
}

TEST(PostconditionChecksTest, ARedirectHopOffTheSiteIsStillAContradiction) {
  const Postcondition postcondition = TypedNavigateTo("https://uidai.gov.in");
  NavigationEvidence evidence = CommittedAt("https://www.uidai.gov.in");
  NavigationHopEvidence hop;
  hop.origin = Tuple("https://tracker.test");
  hop.url.origin = hop.origin;
  hop.url.disclosure = UrlDisclosure::kOriginAndPath;
  hop.url.path = "/bounce";
  evidence.redirect_chain.push_back(hop);
  // Every hop is checked, not only where it stopped.
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kContradicted);
}

TEST(PostconditionChecksTest, AnErrorDocumentOnASiblingIsStillContradicted) {
  const Postcondition postcondition = TypedNavigateTo("https://uidai.gov.in");
  NavigationEvidence evidence = CommittedAt("https://www.uidai.gov.in");
  evidence.is_error_page = true;
  EXPECT_EQ(CheckCommittedNavigation(postcondition, evidence),
            PostconditionCheck::kContradicted);
}

TEST(PostconditionChecksTest, WithoutTheFlagOnlyTheExactOriginIsSatisfied) {
  Postcondition postcondition = TypedNavigateTo("https://uidai.gov.in");
  // A followed link and a browser-resolved search carry a destination the
  // browser produced, and are not widened.
  postcondition.allows_registrable_domain_siblings = false;
  EXPECT_EQ(CheckCommittedNavigation(postcondition,
                                     CommittedAt("https://www.uidai.gov.in")),
            PostconditionCheck::kContradicted);
  EXPECT_EQ(CheckCommittedNavigation(postcondition,
                                     CommittedAt("https://uidai.gov.in")),
            PostconditionCheck::kSatisfied);
}

}  // namespace
}  // namespace taffy
