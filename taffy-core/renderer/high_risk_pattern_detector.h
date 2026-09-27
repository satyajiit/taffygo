// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_HIGH_RISK_PATTERN_DETECTOR_H_
#define TAFFY_RENDERER_HIGH_RISK_PATTERN_DETECTOR_H_

#include <string_view>

#include "base/memory/raw_ref.h"
#include "taffy/renderer/observation_limits.h"

// The bounded pattern detectors protocol section 9.2 asks for, over free text
// that did NOT come from a form control.
//
// This is the second line of defence and it is important that it is only the
// second. The first is that a prohibited value is never read: a control the
// classifier calls credential material has no value accessor pointed at it at
// all. What is left for these detectors is the case the first line cannot
// cover - a page that put a card number in a paragraph, a recovery phrase in
// an accessible name, or an API key in a JSON-LD string.
//
// Two design consequences follow from being second rather than first:
//
//   * They are BOUNDED, not exhaustive. Each scans a prefix of the string
//     whose length comes from the limits policy. A detector that walked
//     megabytes of page text to catch a case the first line already covers
//     would be spending the extraction deadline on the wrong thing.
//
//   * They must not eat the answer. A detector with enough reach to catch
//     everything would delete ordinary prose, and a redaction layer that
//     deletes the answer is not conservative, it is broken. The seed-phrase
//     detector is a whole-string test rather than a scan for exactly this
//     reason, and the cost of that choice is stated in the .cc rather than
//     hidden.
//
// A match drops the whole string. A mask would still state the length, and
// protocol section 9.1 names a length-derived fingerprint specifically.

namespace taffy {

// What a detector found. Named rather than boolean so a diagnostic can say
// which shape fired without carrying the text that fired it.
enum class HighRiskPatternKind {
  kNone,
  kPaymentCardNumber,
  kLongDigitRun,
  kHexKeyMaterial,
  kOpaqueToken,
  kSeedPhrase,
};

class HighRiskPatternDetector {
 public:
  explicit HighRiskPatternDetector(const ObservationLimits& limits);
  HighRiskPatternDetector(const HighRiskPatternDetector&) = delete;
  HighRiskPatternDetector& operator=(const HighRiskPatternDetector&) = delete;
  ~HighRiskPatternDetector();

  HighRiskPatternKind Detect(std::string_view text) const;

 private:
  const raw_ref<const ObservationLimits> limits_;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_HIGH_RISK_PATTERN_DETECTOR_H_
