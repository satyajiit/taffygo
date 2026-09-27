// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_NAVIGATION_ERROR_CLASSIFIER_H_
#define TAFFY_BROWSER_NAVIGATION_ERROR_CLASSIFIER_H_

#include <stdint.h>

// The one place that decides what a failed navigation *was*
// (PAR-NAV-006 offline and DNS error pages, PAR-NAV-007 TLS and certificate
// interstitials).
//
// It is a pure function over browser-owned scalars, so it is unit-testable on
// any host and cannot be influenced by a renderer. It classifies; it never
// decides whether to show an interstitial, never bypasses one, and never
// changes the outcome of a Chromium security check. Chromium owns those
// decisions and this file reads the result.
//
// The reason a classifier exists at all: five different surfaces need the same
// answer — the address bar's security state, the error page copy, the task
// runtime's "the page did not load, do not claim you read it", the parity
// suite, and the observability record. Four of those five getting the mapping
// slightly different is how a browser ends up claiming a task succeeded on an
// error page.

namespace taffy {

// What happened, in the vocabulary the parity matrix uses. Ordered by how a
// person would describe it, not by numeric net error.
enum class NavigationErrorClass : uint8_t {
  // The navigation committed a real document.
  kNone = 0,

  // The host could not be resolved: PAR-NAV-006's DNS half.
  kDnsFailure = 1,

  // The device has no usable network: PAR-NAV-006's offline half. Distinct
  // from kConnectionFailure because the correct retry advice differs.
  kOffline = 2,

  // The connection could not be established or was lost mid-flight.
  kConnectionFailure = 3,

  kTimeout = 4,

  // The TLS handshake or the certificate failed. PAR-NAV-007 requires that
  // upstream checks are not bypassed; this value records that one fired.
  kTlsFailure = 5,

  // The server answered with a 4xx or 5xx and the response body committed.
  // Not an error page in Chromium's sense, and the distinction matters: the
  // document is real, so a reader that treats it as content is not wrong, but
  // a reader that treats it as the *requested* content is.
  kHttpErrorStatus = 6,

  // A client-side policy stopped it — content blocking, an extension in a
  // build that has them, or an embedder rule.
  kBlockedByClient = 7,

  // Safe Browsing stopped it. Separate from kBlockedByClient because
  // PAR-SEC-007 preserves the upstream baseline from M1 and a fork that
  // silently reclassified this row would be hiding exactly the regression the
  // row exists to catch.
  kBlockedBySafeBrowsing = 8,

  // The user or the browser cancelled it. Never an error to report.
  kAborted = 9,

  // A failure that this table does not name. Fails closed: callers treat it as
  // "the page did not load".
  kUnknownFailure = 10,
};

// Which trusted browser surface stood between the user and the content.
enum class InterstitialKind : uint8_t {
  kNone = 0,
  kCertificateError = 1,
  kSafeBrowsing = 2,
  kBlockedByPolicy = 3,
  // An interstitial the fork does not recognise. Reported rather than
  // flattened to kNone, because "some trusted surface is showing" is the part
  // a caller must not lose.
  kOther = 4,
};

// Browser-owned scalars, every one of them read from Chromium's own
// navigation state. No field here can be set by a renderer-reported value:
// NavigationFactsExtractor is the only production producer and it reads a
// content::NavigationHandle.
struct NavigationErrorFacts {
  // net::Error. 0 (net::OK) means the network stack succeeded.
  int net_error = 0;

  // 0 when there was no HTTP response at all.
  int http_status_code = 0;

  // net::CertStatus bit field. Zero when no certificate was involved.
  uint32_t cert_status = 0;

  // True when Chromium committed an error page rather than the site's own
  // document.
  bool is_error_page = false;

  // True when Chromium's Safe Browsing path is the reason the navigation was
  // blocked.
  //
  // VERIFY AT SP-01: how the browser process learns this at the pinned
  // milestone. A Safe Browsing block commits an error page with
  // net::ERR_BLOCKED_BY_CLIENT, which alone cannot be distinguished from a
  // content-blocking rule, so the signal must come from the security
  // interstitial layer. Upstream symbols to read:
  // //components/security_interstitials/content/security_interstitial_tab_helper.h
  // and its GetBlockingPageForFrame accessor. If no browser-process accessor
  // exists, this field stays false and the classifier reports
  // kBlockedByClient, which is the fail-closed answer — the navigation is
  // still blocked, only the attribution is coarser.
  bool blocked_by_safe_browsing = false;

  // True when a trusted interstitial is showing for this navigation and the
  // fork could not identify which. Keeps "something is showing" from being
  // lost when the kind is unrecognised.
  bool has_unrecognised_interstitial = false;
};

// The answer. Two independent axes: what failed, and what the user is looking
// at. They are not derivable from one another — a certificate failure with an
// accepted exception shows no interstitial, and a Safe Browsing interstitial
// can precede a perfectly healthy connection.
struct NavigationErrorVerdict {
  NavigationErrorClass error_class = NavigationErrorClass::kUnknownFailure;
  InterstitialKind interstitial = InterstitialKind::kNone;

  // True when no content from the requested resource reached the document.
  // This is the value a task runtime must consult before recording that it
  // read a page: PAR-NAV-006 requires "no false completion by AI".
  bool content_is_absent = true;
};

// Pure. Same input, same output, on every host and in every process.
NavigationErrorVerdict ClassifyNavigationError(
    const NavigationErrorFacts& facts);

// True when this class of failure is worth offering a retry for. A DNS or
// offline failure is; a Safe Browsing block is not, and offering one there
// would be a nudge toward the thing the block exists to prevent.
bool NavigationErrorIsRetryable(NavigationErrorClass error_class);

}  // namespace taffy

#endif  // TAFFY_BROWSER_NAVIGATION_ERROR_CLASSIFIER_H_
