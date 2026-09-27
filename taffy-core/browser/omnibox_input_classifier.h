// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_OMNIBOX_INPUT_CLASSIFIER_H_
#define TAFFY_BROWSER_OMNIBOX_INPUT_CLASSIFIER_H_

#include <string>
#include <string_view>

#include "taffy/browser/omnibox_classification.h"

// The deterministic URL-versus-search decision of PAR-BOX-001.
//
// It is a pure function of the text and the policy. No history, no
// autocomplete, no ranking, no network, no clock, and no assistant: the same
// string produces the same answer on every device, in every session, with the
// AI runtime absent. That is what "deterministic URL handling" means in the
// parity row, and it is why this file is testable exhaustively rather than
// representatively.
//
// What it does not do, deliberately:
//
//   * It does not build a search URL. The search provider is PAR-BOX-002 and
//     [Open (OD-019)]; emitting terms instead of a URL keeps that decision out
//     of this file entirely.
//   * It does not implement URL fixup. GURL does the canonicalization and
//     //net does the registry lookup, so there is no second parser here to
//     disagree with Chromium's. This file decides; those two parse.
//   * It does not resolve ambiguity. When the text could be either a place or
//     a question, the answer is kAmbiguous and the person is asked. A
//     classifier that guessed would be a classifier that occasionally sent a
//     private note to a search engine.
//
// Threading: none. Call it from anywhere.

namespace taffy {

// The few choices that are genuinely policy rather than fact. Each has one
// owner elsewhere and is passed in rather than read from a global, so a test
// can state the policy it is testing under.
struct OmniboxClassifierPolicy {
  // The scheme applied to a host typed without one. "https" is the only value
  // the M1 product uses; it is a field so that the choice is visible at the
  // call site rather than buried in a constant.
  std::string default_scheme = "https";

  // When true, a single-label name with a port or a path — "wiki:8080" — is
  // ambiguous rather than a search. Networks where such hosts are real exist;
  // devices where they are typos are more common. Ambiguous is the answer that
  // is wrong in neither direction.
  bool single_label_hosts_are_ambiguous = true;
};

class OmniboxInputClassifier {
 public:
  // Pure. `input` is the raw text from the address bar, before any trimming.
  static OmniboxClassification Classify(std::string_view input,
                                        const OmniboxClassifierPolicy& policy);

  // The same call with the product's default policy, for the many call sites
  // that have no reason to state one.
  static OmniboxClassification Classify(std::string_view input);

 private:
  OmniboxInputClassifier() = delete;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_OMNIBOX_INPUT_CLASSIFIER_H_
