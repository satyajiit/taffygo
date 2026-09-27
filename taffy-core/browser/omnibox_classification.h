// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_OMNIBOX_CLASSIFICATION_H_
#define TAFFY_BROWSER_OMNIBOX_CLASSIFICATION_H_

#include <stdint.h>

#include <string>

// The answer to "what did the person mean by what they typed"
// (PAR-BOX-001: deterministic URL handling, and ambiguous input never starts a
// task silently; REQ-IN-001).
//
// Read the enumeration below for what is *not* in it. There is no value
// meaning "let the assistant research this", and there is no field carrying a
// task, a goal or a plan. That is the encoding of the parity row: a
// classification cannot start a task because a classification cannot name one.
// Deciding to research is a separate, explicit user action with its own
// preview and its own consent record (REQ-IN-002), and it takes its input from
// the person, not from a guess about a string.
//
// The other half of the rule is kAmbiguous. When the input could reasonably be
// either a place or a question, this type says so and says why, and
// requires_user_choice is true. Nothing downstream is allowed to break the tie
// by picking the more useful-looking option.

namespace taffy {

enum class OmniboxInputKind : uint8_t {
  // Nothing but whitespace.
  kEmpty = 0,

  // The person wrote a scheme and the rest parsed. Navigate to
  // OmniboxClassification::navigation_url.
  kUrlWithExplicitScheme = 1,

  // No scheme, but the rest is unambiguously a place. navigation_url carries
  // the canonical form with the default scheme applied.
  kUrlWithImplicitScheme = 2,

  // Send OmniboxClassification::search_terms to the configured search
  // provider. Which provider that is belongs to PAR-BOX-002 and
  // [Open (OD-019)]; this classifier never names one, which is why it emits
  // terms rather than a search URL.
  kSearchQuery = 3,

  // Could be either. The person is asked; nothing happens until they answer.
  kAmbiguous = 4,

  // A scheme the address bar does not act on. search_terms carries the
  // original text so the input is not silently swallowed.
  kRefusedScheme = 5,
};

// Why the classifier reached kAmbiguous or kRefusedScheme. Never a free-form
// string: the surface that explains it uses a trusted local template keyed by
// this value, so no part of the person's input is ever reflected into browser
// chrome as markup.
enum class ClassificationReason : uint8_t {
  kNone = 0,

  // A dotted name whose last label is not a public registry. "server.internal"
  // is a real host on some networks and a typo on others, and the browser has
  // no way to tell.
  kUnknownRegistry = 1,

  // The text embeds credentials before the host — the classic phishing shape
  // where the visible prefix is not the destination.
  kEmbeddedCredentials = 2,

  // A non-ASCII host. Display and security behavior for internationalized
  // domains is PAR-WEB-012 at M4; until that row has evidence, the honest
  // answer at M1 is to ask rather than to guess.
  kNonAsciiHost = 3,

  // Digits and dots that URL canonicalization would turn into an IP address
  // even though the person may have meant a number. "3.14" is the example
  // that matters.
  kNumericHostShape = 4,

  // A single label with a port or a path — "wiki:8080" or "docs/index" — which
  // is a host on an intranet and a search everywhere else.
  kSingleLabelHost = 5,

  // A scheme that executes rather than locates: javascript, data, blob and
  // their relatives. Permanently refused from the address bar, because pasting
  // one is almost always somebody else's idea.
  kActiveContentScheme = 6,

  // A scheme the address bar does not act on at M1 — file, content, intent and
  // the browser's own internal schemes. Their parity rows are PAR-FILE-006 and
  // PAR-PERM-007 at M4.
  kSchemeNotSupportedYet = 7,
};

struct OmniboxClassification {
  OmniboxInputKind kind = OmniboxInputKind::kEmpty;
  ClassificationReason reason = ClassificationReason::kNone;

  // Canonical, produced by GURL. Empty for every kind except the two URL
  // kinds, and empty is never spliced into a navigation.
  std::string navigation_url;

  // What to search for. Set for kSearchQuery, for kRefusedScheme, and for
  // kAmbiguous — an ambiguous input carries both interpretations so the
  // surface asking the question does not have to reconstruct either.
  std::string search_terms;

  // True exactly when kind is kAmbiguous. Stored rather than derived so that a
  // consumer cannot accidentally drop the question by testing the wrong thing.
  bool requires_user_choice = false;

  friend bool operator==(const OmniboxClassification&,
                         const OmniboxClassification&) = default;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_OMNIBOX_CLASSIFICATION_H_
