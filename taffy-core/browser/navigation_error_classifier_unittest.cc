// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/navigation_error_classifier.h"

#include <vector>

#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "testing/gtest/include/gtest/gtest.h"

// The classifier is pure, so this file can be exhaustive rather than
// representative. Three properties are asserted, in this order of importance:
//
//   1. A failure never reads as a success. Every non-OK input produces an
//      error class other than kNone, including inputs this table does not
//      name.
//   2. A Safe Browsing block is never reported as ordinary content blocking.
//      The two share net::ERR_BLOCKED_BY_CLIENT, and collapsing them would
//      hide the exact regression PAR-SEC-007 exists to catch.
//   3. Retry advice is a total function with no default arm, so adding an
//      error class without deciding its retry behavior fails the build.

namespace taffy {
namespace {

NavigationErrorFacts OkFacts() {
  NavigationErrorFacts facts;
  facts.net_error = net::OK;
  facts.http_status_code = 200;
  return facts;
}

TEST(NavigationErrorClassifierTest, SuccessfulNavigationCarriesContent) {
  NavigationErrorVerdict verdict = ClassifyNavigationError(OkFacts());

  EXPECT_EQ(NavigationErrorClass::kNone, verdict.error_class);
  EXPECT_EQ(InterstitialKind::kNone, verdict.interstitial);
  EXPECT_FALSE(verdict.content_is_absent);
}

TEST(NavigationErrorClassifierTest, HttpErrorStatusStillCommittedADocument) {
  NavigationErrorFacts facts = OkFacts();
  facts.http_status_code = 404;

  NavigationErrorVerdict verdict = ClassifyNavigationError(facts);

  // The bytes are real; they are just not the resource that was asked for.
  EXPECT_EQ(NavigationErrorClass::kHttpErrorStatus, verdict.error_class);
  EXPECT_FALSE(verdict.content_is_absent);
  EXPECT_FALSE(NavigationErrorIsRetryable(verdict.error_class));
}

TEST(NavigationErrorClassifierTest, DnsAndOfflineAreDistinguished) {
  NavigationErrorFacts dns;
  dns.net_error = net::ERR_NAME_NOT_RESOLVED;
  dns.is_error_page = true;
  EXPECT_EQ(NavigationErrorClass::kDnsFailure,
            ClassifyNavigationError(dns).error_class);

  NavigationErrorFacts offline;
  offline.net_error = net::ERR_INTERNET_DISCONNECTED;
  offline.is_error_page = true;
  EXPECT_EQ(NavigationErrorClass::kOffline,
            ClassifyNavigationError(offline).error_class);

  // PAR-NAV-006 wants both, separately: the retry advice a person is given
  // differs, and a browser that says "check your connection" for a typo'd
  // hostname is wrong in a way people notice.
  EXPECT_TRUE(NavigationErrorIsRetryable(NavigationErrorClass::kDnsFailure));
  EXPECT_TRUE(NavigationErrorIsRetryable(NavigationErrorClass::kOffline));
}

TEST(NavigationErrorClassifierTest, CertificateErrorRaisesAnInterstitial) {
  NavigationErrorFacts facts;
  facts.net_error = net::ERR_CERT_AUTHORITY_INVALID;
  facts.cert_status = net::CERT_STATUS_AUTHORITY_INVALID;
  facts.is_error_page = true;

  NavigationErrorVerdict verdict = ClassifyNavigationError(facts);

  EXPECT_EQ(NavigationErrorClass::kTlsFailure, verdict.error_class);
  EXPECT_EQ(InterstitialKind::kCertificateError, verdict.interstitial);
  EXPECT_TRUE(verdict.content_is_absent);
  // PAR-NAV-007: a retry is the same failure. Offering one would train people
  // to tap through the interstitial.
  EXPECT_FALSE(NavigationErrorIsRetryable(verdict.error_class));
}

TEST(NavigationErrorClassifierTest,
     CertificateStatusWinsOverAnUnnamedNetError) {
  // An interstitial can commit with a net error this table does not name. The
  // certificate status is the stronger signal and must not be lost.
  NavigationErrorFacts facts;
  facts.net_error = net::ERR_FAILED;
  facts.cert_status = net::CERT_STATUS_DATE_INVALID;
  facts.is_error_page = true;

  NavigationErrorVerdict verdict = ClassifyNavigationError(facts);

  EXPECT_EQ(NavigationErrorClass::kTlsFailure, verdict.error_class);
  EXPECT_EQ(InterstitialKind::kCertificateError, verdict.interstitial);
}

TEST(NavigationErrorClassifierTest,
     InformationalCertStatusIsNotAnInterstitial) {
  // net::IsCertStatusError ignores the informational bits. A page served over
  // a merely unusual certificate configuration is not an interstitial, and
  // reporting one would put a security warning where none is showing.
  NavigationErrorFacts facts = OkFacts();
  facts.cert_status = net::CERT_STATUS_IS_EV;

  NavigationErrorVerdict verdict = ClassifyNavigationError(facts);

  EXPECT_EQ(NavigationErrorClass::kNone, verdict.error_class);
  EXPECT_EQ(InterstitialKind::kNone, verdict.interstitial);
}

TEST(NavigationErrorClassifierTest,
     SafeBrowsingIsNeverReportedAsOrdinaryBlocking) {
  NavigationErrorFacts blocked;
  blocked.net_error = net::ERR_BLOCKED_BY_CLIENT;
  blocked.is_error_page = true;

  // Without the attribution the answer is the coarser, fail-closed one.
  NavigationErrorVerdict without = ClassifyNavigationError(blocked);
  EXPECT_EQ(NavigationErrorClass::kBlockedByClient, without.error_class);
  EXPECT_EQ(InterstitialKind::kBlockedByPolicy, without.interstitial);

  blocked.blocked_by_safe_browsing = true;
  NavigationErrorVerdict with = ClassifyNavigationError(blocked);
  EXPECT_EQ(NavigationErrorClass::kBlockedBySafeBrowsing, with.error_class);
  EXPECT_EQ(InterstitialKind::kSafeBrowsing, with.interstitial);
  EXPECT_FALSE(NavigationErrorIsRetryable(with.error_class));
}

TEST(NavigationErrorClassifierTest, AbortLeavesThePreviousDocumentInPlace) {
  NavigationErrorFacts facts;
  facts.net_error = net::ERR_ABORTED;

  NavigationErrorVerdict verdict = ClassifyNavigationError(facts);

  EXPECT_EQ(NavigationErrorClass::kAborted, verdict.error_class);
  // The user is still looking at something real, so content is not absent.
  EXPECT_FALSE(verdict.content_is_absent);
}

TEST(NavigationErrorClassifierTest, UnrecognisedInterstitialIsNotFlattened) {
  NavigationErrorFacts facts = OkFacts();
  facts.has_unrecognised_interstitial = true;

  EXPECT_EQ(InterstitialKind::kOther,
            ClassifyNavigationError(facts).interstitial);
}

TEST(NavigationErrorClassifierTest, NoFailureIsEverClassifiedAsSuccess) {
  // Property 1, over a spread of net errors including several this classifier
  // deliberately does not name. An unnamed failure must reach
  // kUnknownFailure — never kNone.
  const std::vector<int> failures = {
      net::ERR_FAILED,
      net::ERR_ACCESS_DENIED,
      net::ERR_TOO_MANY_REDIRECTS,
      net::ERR_CONTENT_LENGTH_MISMATCH,
      net::ERR_INVALID_URL,
      net::ERR_UNKNOWN_URL_SCHEME,
      net::ERR_NAME_NOT_RESOLVED,
      net::ERR_INTERNET_DISCONNECTED,
      net::ERR_CONNECTION_REFUSED,
      net::ERR_CONNECTION_TIMED_OUT,
      net::ERR_SSL_PROTOCOL_ERROR,
      net::ERR_BLOCKED_BY_CLIENT,
      net::ERR_ABORTED,
  };

  for (int net_error : failures) {
    NavigationErrorFacts facts;
    facts.net_error = net_error;
    facts.is_error_page = net_error != net::ERR_ABORTED;

    NavigationErrorVerdict verdict = ClassifyNavigationError(facts);
    EXPECT_NE(NavigationErrorClass::kNone, verdict.error_class)
        << "net error " << net_error << " was classified as a success";
  }
}

TEST(NavigationErrorClassifierTest, RetryAdviceIsDecidedForEveryClass) {
  // Property 3. NavigationErrorIsRetryable has no default arm, so a new class
  // is a compile error there; this test is the run-time half — it proves the
  // list below stayed in step with the enumeration by covering every value.
  const std::vector<NavigationErrorClass> all = {
      NavigationErrorClass::kNone,
      NavigationErrorClass::kDnsFailure,
      NavigationErrorClass::kOffline,
      NavigationErrorClass::kConnectionFailure,
      NavigationErrorClass::kTimeout,
      NavigationErrorClass::kTlsFailure,
      NavigationErrorClass::kHttpErrorStatus,
      NavigationErrorClass::kBlockedByClient,
      NavigationErrorClass::kBlockedBySafeBrowsing,
      NavigationErrorClass::kAborted,
      NavigationErrorClass::kUnknownFailure,
  };
  ASSERT_EQ(static_cast<size_t>(NavigationErrorClass::kUnknownFailure) + 1,
            all.size())
      << "A NavigationErrorClass value was added without a retry decision.";

  for (NavigationErrorClass error_class : all) {
    // The call itself is the assertion: a value with no arm would fall off the
    // end of a switch with no default, which is undefined behavior the build
    // rejects. Recording the answer keeps the intent readable.
    const bool retryable = NavigationErrorIsRetryable(error_class);
    if (error_class == NavigationErrorClass::kBlockedBySafeBrowsing ||
        error_class == NavigationErrorClass::kTlsFailure) {
      EXPECT_FALSE(retryable);
    }
  }
}

}  // namespace
}  // namespace taffy
