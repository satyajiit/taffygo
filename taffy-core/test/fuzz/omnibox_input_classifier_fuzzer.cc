// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "base/check.h"
#include "base/check_op.h"

#include <string_view>

#include "taffy/browser/omnibox_classification.h"
#include "taffy/browser/omnibox_input_classifier.h"
#include "third_party/icu/fuzzers/fuzzer_utils.h"

// Address-bar input: the one place a person types something that becomes a
// navigation. It has to be total and it has to be deterministic, because
// "ambiguous input never starts a task silently" is only meaningful if the same
// input always classifies the same way.

// Classification reaches URL parsing, and host canonicalization there opens the
// UTS #46 table. Without this the first input that looked like a host reached
// `NOTREACHED` in url/url_idna_icu.cc rather than the classifier, so the run
// ended on a missing data file and proved nothing about the property below.
struct OmniboxClassifierEnvironment {
  IcuEnvironment icu_environment;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static OmniboxClassifierEnvironment environment;
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const taffy::OmniboxClassification first =
      taffy::OmniboxInputClassifier::Classify(input);
  const taffy::OmniboxClassification second =
      taffy::OmniboxInputClassifier::Classify(input);
  CHECK(first.kind == second.kind);
  return 0;
}
