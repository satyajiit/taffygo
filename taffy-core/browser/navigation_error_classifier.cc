// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/navigation_error_classifier.h"

#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"

namespace taffy {

namespace {

// The net-error table. Written as data rather than as a chain of comparisons
// so that adding a row is a one-line change a reviewer can read, and so the
// unit test can walk it.
//
// VERIFY AT SP-01: that every symbol below still exists in
// //net/base/net_errors.h at the pinned milestone. net error names are stable
// by policy — they are recorded in histograms — but the list is generated from
// net/base/net_error_list.h and a removal would be a compile error here, which
// is the failure mode worth having.
//
// SP-01 done for 152.0.7977.42, and the failure mode paid off twice:
//   * ERR_DNS_SERVER_FAILED (-802) is gone; net_error_list.h now carries a
//     "Error -802 was removed" tombstone and ERR_DNS_SERVER_FAILURE (-817),
//     "the DNS server responded with a server failure response code", is the
//     surviving spelling of the same fact. Same class, DNS.
//   * ERR_CERT_SYMANTEC_LEGACY (-215) is gone with no successor — the
//     tombstone at net_error_list.h:600 is matched by
//     cert_status_flags_list.h:56 ("Bit 25 was CERT_STATUS_SYMANTEC_LEGACY"),
//     so the distrust program left neither an error code nor a status bit
//     behind. Its row was deleted rather than remapped: no navigation at this
//     milestone can carry that code, so there is nothing to classify. The TLS
//     class loses no coverage, because a certificate failure that arrives with
//     an unnamed net error is still caught by the IsCertStatusError() branch
//     in ClassifyNavigationError().
struct NetErrorRow {
  int net_error;
  NavigationErrorClass error_class;
};

constexpr NetErrorRow kNetErrorTable[] = {
    // Name resolution. PAR-NAV-006's DNS half.
    {net::ERR_NAME_NOT_RESOLVED, NavigationErrorClass::kDnsFailure},
    {net::ERR_NAME_RESOLUTION_FAILED, NavigationErrorClass::kDnsFailure},
    {net::ERR_DNS_TIMED_OUT, NavigationErrorClass::kDnsFailure},
    {net::ERR_DNS_MALFORMED_RESPONSE, NavigationErrorClass::kDnsFailure},
    {net::ERR_DNS_SERVER_FAILURE, NavigationErrorClass::kDnsFailure},

    // No usable network at all. PAR-NAV-006's offline half.
    {net::ERR_INTERNET_DISCONNECTED, NavigationErrorClass::kOffline},
    {net::ERR_NETWORK_CHANGED, NavigationErrorClass::kOffline},
    {net::ERR_NETWORK_ACCESS_DENIED, NavigationErrorClass::kOffline},

    // Reachable network, unusable connection.
    {net::ERR_CONNECTION_REFUSED, NavigationErrorClass::kConnectionFailure},
    {net::ERR_CONNECTION_RESET, NavigationErrorClass::kConnectionFailure},
    {net::ERR_CONNECTION_CLOSED, NavigationErrorClass::kConnectionFailure},
    {net::ERR_CONNECTION_ABORTED, NavigationErrorClass::kConnectionFailure},
    {net::ERR_CONNECTION_FAILED, NavigationErrorClass::kConnectionFailure},
    {net::ERR_ADDRESS_UNREACHABLE, NavigationErrorClass::kConnectionFailure},
    {net::ERR_EMPTY_RESPONSE, NavigationErrorClass::kConnectionFailure},
    {net::ERR_INVALID_RESPONSE, NavigationErrorClass::kConnectionFailure},

    {net::ERR_CONNECTION_TIMED_OUT, NavigationErrorClass::kTimeout},
    {net::ERR_TIMED_OUT, NavigationErrorClass::kTimeout},

    // TLS. PAR-NAV-007. Certificate errors are handled by the cert_status
    // branch below as well, because a certificate error can arrive with a
    // generic net error when an interstitial commits.
    {net::ERR_SSL_PROTOCOL_ERROR, NavigationErrorClass::kTlsFailure},
    {net::ERR_SSL_VERSION_OR_CIPHER_MISMATCH, NavigationErrorClass::kTlsFailure},
    {net::ERR_BAD_SSL_CLIENT_AUTH_CERT, NavigationErrorClass::kTlsFailure},
    {net::ERR_SSL_CLIENT_AUTH_CERT_NEEDED, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_COMMON_NAME_INVALID, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_DATE_INVALID, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_AUTHORITY_INVALID, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_REVOKED, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_INVALID, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_WEAK_SIGNATURE_ALGORITHM, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_NON_UNIQUE_NAME, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_NAME_CONSTRAINT_VIOLATION,
     NavigationErrorClass::kTlsFailure},
    {net::ERR_CERT_VALIDITY_TOO_LONG, NavigationErrorClass::kTlsFailure},
    {net::ERR_CERTIFICATE_TRANSPARENCY_REQUIRED,
     NavigationErrorClass::kTlsFailure},

    // Client-side policy. Safe Browsing shares this net error, which is why
    // the caller has to supply the attribution separately; see the branch
    // above the table walk.
    {net::ERR_BLOCKED_BY_CLIENT, NavigationErrorClass::kBlockedByClient},
    {net::ERR_BLOCKED_BY_RESPONSE, NavigationErrorClass::kBlockedByClient},
    {net::ERR_BLOCKED_BY_CSP, NavigationErrorClass::kBlockedByClient},
    {net::ERR_UNSAFE_REDIRECT, NavigationErrorClass::kBlockedByClient},
    {net::ERR_UNSAFE_PORT, NavigationErrorClass::kBlockedByClient},

    {net::ERR_ABORTED, NavigationErrorClass::kAborted},
};

NavigationErrorClass ClassFromNetError(int net_error) {
  for (const NetErrorRow& row : kNetErrorTable) {
    if (row.net_error == net_error) {
      return row.error_class;
    }
  }
  return NavigationErrorClass::kUnknownFailure;
}

InterstitialKind InterstitialFor(const NavigationErrorFacts& facts,
                                 NavigationErrorClass error_class) {
  if (facts.blocked_by_safe_browsing) {
    return InterstitialKind::kSafeBrowsing;
  }
  // net::IsCertStatusError ignores the informational bits, so a page that is
  // merely served over a legacy cipher does not read as an interstitial.
  if (net::IsCertStatusError(facts.cert_status)) {
    return InterstitialKind::kCertificateError;
  }
  if (error_class == NavigationErrorClass::kTlsFailure && facts.is_error_page) {
    return InterstitialKind::kCertificateError;
  }
  if (error_class == NavigationErrorClass::kBlockedByClient &&
      facts.is_error_page) {
    return InterstitialKind::kBlockedByPolicy;
  }
  if (facts.has_unrecognised_interstitial) {
    return InterstitialKind::kOther;
  }
  return InterstitialKind::kNone;
}

}  // namespace

NavigationErrorVerdict ClassifyNavigationError(
    const NavigationErrorFacts& facts) {
  NavigationErrorVerdict verdict;

  if (facts.net_error == net::OK && !facts.is_error_page) {
    // A committed document. A 4xx or 5xx still committed a document, and the
    // distinction is deliberate: the bytes are real, they are just not the
    // resource that was asked for.
    verdict.error_class = facts.http_status_code >= 400
                              ? NavigationErrorClass::kHttpErrorStatus
                              : NavigationErrorClass::kNone;
    verdict.interstitial = InterstitialFor(facts, verdict.error_class);
    verdict.content_is_absent = false;
    return verdict;
  }

  NavigationErrorClass from_table = ClassFromNetError(facts.net_error);

  // Safe Browsing has to be attributed before the table, because it shares
  // net::ERR_BLOCKED_BY_CLIENT with ordinary content blocking and the two must
  // never be reported as the same thing (PAR-SEC-007).
  if (facts.blocked_by_safe_browsing) {
    from_table = NavigationErrorClass::kBlockedBySafeBrowsing;
  } else if (from_table == NavigationErrorClass::kUnknownFailure &&
             net::IsCertStatusError(facts.cert_status)) {
    // An error page committed for a certificate problem can carry a net error
    // this table does not name; the certificate status is the stronger signal.
    from_table = NavigationErrorClass::kTlsFailure;
  }

  verdict.error_class = from_table;
  verdict.interstitial = InterstitialFor(facts, from_table);

  // An aborted navigation leaves the previous document in place, so content is
  // not absent — the user is still looking at something real. Every other
  // failure class committed an error page or nothing at all.
  verdict.content_is_absent = from_table != NavigationErrorClass::kAborted;
  return verdict;
}

bool NavigationErrorIsRetryable(NavigationErrorClass error_class) {
  switch (error_class) {
    case NavigationErrorClass::kDnsFailure:
    case NavigationErrorClass::kOffline:
    case NavigationErrorClass::kConnectionFailure:
    case NavigationErrorClass::kTimeout:
    case NavigationErrorClass::kUnknownFailure:
      return true;

    // Not retryable, each for its own reason. A TLS failure retried is the
    // same failure; an HTTP error status is the server's answer; a block is a
    // decision, and offering a retry there would be a nudge toward the thing
    // the block exists to prevent; an abort was somebody's choice; and a
    // successful navigation has nothing to retry.
    case NavigationErrorClass::kNone:
    case NavigationErrorClass::kTlsFailure:
    case NavigationErrorClass::kHttpErrorStatus:
    case NavigationErrorClass::kBlockedByClient:
    case NavigationErrorClass::kBlockedBySafeBrowsing:
    case NavigationErrorClass::kAborted:
      return false;
  }
}

}  // namespace taffy
