// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/omnibox_input_classifier.h"

#include <stddef.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace taffy {

namespace {

// The three scheme lists below hold std::string_view rather than const char*,
// and are searched with std::ranges::contains rather than the index loop that
// used to live here. Both halves are forced by the pin: -Wunsafe-buffer-usage
// is an error in this build and it fires on subscripting a decayed
// `const char* const*`, and base/containers/contains.h no longer exists at
// 152.0.7977.42 — upstream has moved these searches to the std::ranges
// algorithms over string_view arrays (see
// components/autofill/core/common/autocomplete_parsing_util.cc for the same
// shape). The membership tests themselves are unchanged: exact, lowercase,
// whole-scheme comparisons against a closed list.

// Schemes that execute rather than locate. Permanently refused from the
// address bar: pasting one is almost always somebody else's idea, and every
// browser that ever accepted javascript: from a paste regretted it.
constexpr std::string_view kActiveContentSchemes[] = {
    "javascript", "data", "blob", "filesystem", "vbscript",
};

// Schemes the address bar does not act on at M1. Each has a parity row with a
// later milestone — PAR-FILE-006 for file and content, PAR-PERM-007 for
// external application intents — and acting on one now would be shipping a
// capability that row has not reviewed.
constexpr std::string_view kSchemesNotSupportedYet[] = {
    "file",    "content", "intent",  "android-app", "chrome",
    "devtools", "about",  "view-source",
};

// Schemes the classifier navigates to. Kept explicit rather than
// "anything GURL accepts", because GURL accepts a great deal.
constexpr std::string_view kNavigableSchemes[] = {
    "http", "https", "ftp", "ws", "wss",
};

bool IsAsciiText(std::string_view text) {
  return std::none_of(text.begin(), text.end(), [](char c) {
    return static_cast<unsigned char>(c) >= 0x80;
  });
}

// True when what follows the colon is a port rather than the body of a URL:
// all digits, non-empty, up to the first slash. This is the rule that keeps
// "example.com:8443/admin" and "localhost:8080" from being read as schemes
// named "example.com" and "localhost", which is the classic way a URL
// classifier sends an intranet address to a search engine.
bool ColonIntroducesAPort(std::string_view after_colon) {
  const size_t slash = after_colon.find('/');
  const std::string_view segment = after_colon.substr(0, slash);
  if (segment.empty()) {
    return false;
  }
  return std::all_of(segment.begin(), segment.end(),
                     [](char c) { return base::IsAsciiDigit(c); });
}

// Returns the scheme the person typed, lowercased, or an empty string when the
// text does not begin with one. Deliberately stricter than a URL parser: a
// scheme is letters, digits, '+', '-' and '.', starting with a letter, then a
// colon that does not introduce a port. "C:" and "3.14:" are not schemes by
// this rule, which is what stops a Windows path and a numeric range from being
// treated as one.
std::string ExplicitScheme(std::string_view input) {
  size_t colon = input.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return std::string();
  }
  std::string_view candidate = input.substr(0, colon);
  if (!base::IsAsciiAlpha(candidate.front())) {
    return std::string();
  }
  for (char c : candidate) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '+' && c != '-' && c != '.') {
      return std::string();
    }
  }
  if (ColonIntroducesAPort(input.substr(colon + 1))) {
    return std::string();
  }
  return base::ToLowerASCII(candidate);
}

bool HostHasKnownRegistry(const GURL& url) {
  // //net owns the public suffix list. Asking it is what keeps this file from
  // growing a table of top-level domains that would be wrong within a year.
  //
  // GURL's accessors were renamed at the pin: the zero-copy views that used to
  // be host_piece()/path_piece() are now host()/path(), and the names host()
  // and path() no longer return std::string — that spelling is GetHost() and
  // GetPath(). Confirmed in url/gurl.h at 152.0.7977.42, where host() is
  // declared `std::string_view host() const LIFETIME_BOUND`. The views are
  // what this file wants everywhere: it only ever reads.
  return net::registry_controlled_domains::GetCanonicalHostRegistryLength(
             url.host(),
             net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
             net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES) > 0;
}

bool IsLocalhostName(std::string_view host) {
  return host == "localhost" || base::EndsWith(host, ".localhost");
}

size_t CountDots(std::string_view text) {
  return static_cast<size_t>(std::count(text.begin(), text.end(), '.'));
}

OmniboxClassification Ambiguous(std::string_view input,
                                ClassificationReason reason,
                                const GURL& candidate) {
  OmniboxClassification result;
  result.kind = OmniboxInputKind::kAmbiguous;
  result.reason = reason;
  result.requires_user_choice = true;
  result.search_terms = std::string(input);
  // Both interpretations travel, so the surface asking the question does not
  // have to reconstruct either one and cannot reconstruct them differently.
  if (candidate.is_valid()) {
    result.navigation_url = candidate.spec();
  }
  return result;
}

OmniboxClassification Search(std::string_view input) {
  OmniboxClassification result;
  result.kind = OmniboxInputKind::kSearchQuery;
  result.search_terms = std::string(input);
  return result;
}

OmniboxClassification Navigate(OmniboxInputKind kind, const GURL& url) {
  OmniboxClassification result;
  result.kind = kind;
  result.navigation_url = url.spec();
  return result;
}

// The shared tail of both URL paths: the checks that make a parsed URL
// ambiguous rather than navigable.
OmniboxClassification RefineParsedUrl(std::string_view input,
                                      const GURL& url,
                                      OmniboxInputKind kind) {
  if (url.has_username() || url.has_password()) {
    // The visible prefix is not the destination. This is the phishing shape,
    // and a browser that navigates without asking is doing the attacker's
    // formatting for them.
    return Ambiguous(input, ClassificationReason::kEmbeddedCredentials, url);
  }
  if (!IsAsciiText(input)) {
    // Internationalized domain display and spoofing rules are PAR-WEB-012 at
    // M4. Asking is the honest M1 answer; guessing would ship the row early
    // and without evidence.
    return Ambiguous(input, ClassificationReason::kNonAsciiHost, url);
  }
  return Navigate(kind, url);
}

}  // namespace

// static
OmniboxClassification OmniboxInputClassifier::Classify(
    std::string_view input,
    const OmniboxClassifierPolicy& policy) {
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(input, base::TRIM_ALL);
  if (trimmed.empty()) {
    return OmniboxClassification();
  }

  // --- 1. An explicit scheme decides almost everything ---------------------
  const std::string scheme = ExplicitScheme(trimmed);
  if (!scheme.empty()) {
    if (std::ranges::contains(kActiveContentSchemes, scheme)) {
      OmniboxClassification result;
      result.kind = OmniboxInputKind::kRefusedScheme;
      result.reason = ClassificationReason::kActiveContentScheme;
      // The text is not swallowed: it is offered as a search, which is what a
      // person who pasted it by accident would want and what a person who was
      // told to paste it gets instead of an execution.
      result.search_terms = std::string(trimmed);
      return result;
    }
    if (std::ranges::contains(kSchemesNotSupportedYet, scheme)) {
      OmniboxClassification result;
      result.kind = OmniboxInputKind::kRefusedScheme;
      result.reason = ClassificationReason::kSchemeNotSupportedYet;
      result.search_terms = std::string(trimmed);
      return result;
    }
    if (std::ranges::contains(kNavigableSchemes, scheme)) {
      const GURL url{trimmed};
      if (!url.is_valid() || !url.has_host() || url.host().empty()) {
        // A scheme the person wrote, followed by something that is not a
        // location. Searching for the whole text is the only useful answer.
        return Search(trimmed);
      }
      return RefineParsedUrl(trimmed, url,
                             OmniboxInputKind::kUrlWithExplicitScheme);
    }
    // An unknown scheme. Not refused — refusing implies a judgement this file
    // has no basis for — but not navigated either.
    return Search(trimmed);
  }

  // --- 2. No scheme. Whitespace means prose --------------------------------
  if (trimmed.find_first_of(" \t\n\r\f\v") != std::string_view::npos) {
    return Search(trimmed);
  }

  // --- 3. No scheme, no whitespace. Try it as a host -----------------------
  const GURL candidate{policy.default_scheme + "://" + std::string(trimmed)};
  if (!candidate.is_valid() || !candidate.has_host() ||
      candidate.host().empty()) {
    return Search(trimmed);
  }

  if (candidate.has_username() || candidate.has_password()) {
    return Ambiguous(trimmed, ClassificationReason::kEmbeddedCredentials,
                     candidate);
  }
  if (!IsAsciiText(trimmed)) {
    return Ambiguous(trimmed, ClassificationReason::kNonAsciiHost, candidate);
  }

  const std::string_view host = candidate.host();

  if (candidate.HostIsIPAddress()) {
    // A dotted quad or a bracketed IPv6 literal is a place. Anything else that
    // canonicalized into an address is a number that URL parsing reinterpreted
    // — "3.14" becomes 3.0.0.14 — and the person probably meant the number.
    const size_t typed_dots = CountDots(trimmed);
    const bool looks_like_a_typed_address =
        host.front() == '[' || typed_dots == 3;
    if (looks_like_a_typed_address) {
      return Navigate(OmniboxInputKind::kUrlWithImplicitScheme, candidate);
    }
    return Ambiguous(trimmed, ClassificationReason::kNumericHostShape,
                     candidate);
  }

  if (IsLocalhostName(host)) {
    return Navigate(OmniboxInputKind::kUrlWithImplicitScheme, candidate);
  }

  if (CountDots(host) == 0) {
    // A single label. On its own it is a search; with a port or a path it is
    // the shape an intranet host takes, which is exactly the ambiguity
    // PAR-BOX-001 says must not be resolved silently.
    const bool has_locator_shape =
        candidate.has_port() || candidate.path().size() > 1 ||
        candidate.has_query() || candidate.has_ref();
    if (has_locator_shape && policy.single_label_hosts_are_ambiguous) {
      return Ambiguous(trimmed, ClassificationReason::kSingleLabelHost,
                       candidate);
    }
    return Search(trimmed);
  }

  if (HostHasKnownRegistry(candidate)) {
    return Navigate(OmniboxInputKind::kUrlWithImplicitScheme, candidate);
  }

  // Dotted, but the last label is not a public registry. "server.internal" is
  // a host on some networks and a typo on others, and there is no fact that
  // decides which.
  return Ambiguous(trimmed, ClassificationReason::kUnknownRegistry, candidate);
}

// static
OmniboxClassification OmniboxInputClassifier::Classify(std::string_view input) {
  static const OmniboxClassifierPolicy kDefaultPolicy;
  return Classify(input, kDefaultPolicy);
}

}  // namespace taffy
