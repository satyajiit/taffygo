// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SECRET_SHAPE_SCANNER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SECRET_SHAPE_SCANNER_H_

#include <stddef.h>

#include <string_view>
#include <vector>

#include "taffy/components/intelligence/content/scrubbed_text.h"

// Finds the shapes a secret takes in free text (PAR-SEC-009).
//
// Pure, deterministic, allocation-light and hand-written. Three deliberate
// choices behind that:
//
//   * **No regular expressions.** Chromium does not use std::regex, and a
//     pattern engine on attacker-influenced text is a denial-of-service
//     surface: a scrubber that can be made quadratic is a scrubber an attacker
//     can use to hang the browser process. Every scanner below is a single
//     forward pass with a bounded look-ahead.
//   * **Shapes, not values.** Nothing here holds a list of secrets. A scrubber
//     that hashed known secrets in order to recognise them would need the
//     secrets, which is the thing it exists to avoid handling.
//   * **Structure first, entropy last.** A labelled parameter, a bearer token
//     and a certificate block are recognised by their structure, which is
//     precise. The high-entropy rule is the catch-all and is deliberately the
//     narrowest of its kind: mixed case *and* digits *and* at least
//     kHighEntropyMinimumLength characters, so a lowercase hexadecimal digest
//     — including the content digests this component computes itself — does
//     not match.
//
// False positives cost a redacted diagnostic. False negatives cost a leaked
// credential. The thresholds are chosen accordingly, and the seeded-secret
// suite is what proves the direction of the trade.

namespace taffy {

// The narrowest run the entropy rule will consider. Below this, ordinary
// identifiers — a session-scoped node identifier, a base64 icon fragment —
// start matching, and a scrubber that redacts everything teaches people to
// ignore it.
inline constexpr size_t kHighEntropyMinimumLength = 24;

// One span of the input that must not survive.
struct SecretMatch {
  size_t begin = 0;
  size_t end = 0;
  ScrubRule rule = ScrubRule::kNone;

  // The text to put in its place. Static, never derived from the input: a
  // replacement that echoed part of what it replaced would be a leak with
  // extra steps.
  std::string_view replacement;

  size_t length() const { return end - begin; }
};

// Returns non-overlapping matches in ascending order of position. When two
// scanners claim overlapping spans the more structural rule wins, because it
// knows what it found; where they are equally structural the longer span wins,
// because a partial redaction of a secret is not a redaction.
std::vector<SecretMatch> ScanForSecretShapes(std::string_view text);

// Exposed for the seeded-secret suite and for the serializer. The literal is
// the prefix every fixture canary carries
// (test-fixtures/web/manifest.json). Matching it is a tripwire, not a
// mitigation: a canary that reaches the scrubber means a redaction upstream of
// here failed, and counting it is how that failure becomes visible.
extern const char kCanaryTokenPrefix[];

// True when `digits`, ignoring separators, passes the Luhn check. Exposed so
// the payment-card rule can be tested directly rather than only through the
// scanner.
bool PassesLuhnCheck(std::string_view digits);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SECRET_SHAPE_SCANNER_H_
