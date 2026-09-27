// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORPUS_CORPUS_EXPECTED_FIELD_H_
#define TAFFY_TEST_CORPUS_CORPUS_EXPECTED_FIELD_H_

#include <string>

// One semantic field a corpus fixture promises to carry, in the corpus's own
// words (test-fixtures/web/manifest.json, "expected_semantic_fields").
//
// It is a triple rather than a pair because the protocol requires provenance
// to survive transformation: knowing that a page says the price is 129.00 USD
// is worth nothing to a test unless the test also knows the corpus expects
// that value to come from a specific place. `source` is what lets an
// extraction assertion say "and it came from the JSON-LD, not from the visible
// text" without a second table somewhere else that says the same thing badly.

namespace taffy::test {

struct CorpusExpectedField {
  CorpusExpectedField();
  CorpusExpectedField(const CorpusExpectedField&);
  CorpusExpectedField(CorpusExpectedField&&) noexcept;
  CorpusExpectedField& operator=(const CorpusExpectedField&);
  CorpusExpectedField& operator=(CorpusExpectedField&&) noexcept;
  ~CorpusExpectedField();

  // Dotted field name, for example "price.visible" or "table.measurements".
  std::string field;
  // The value the corpus declares, as written. Never parsed here: a test that
  // needs a number parses it at the assertion, where the tolerance it wants is
  // visible.
  std::string value;
  // Where the corpus says the value comes from, for example
  // "JSON-LD offers.price (stale)".
  std::string source;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORPUS_CORPUS_EXPECTED_FIELD_H_
