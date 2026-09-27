// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORPUS_CORPUS_FIXTURE_H_
#define TAFFY_TEST_CORPUS_CORPUS_FIXTURE_H_

#include <string>
#include <vector>

#include "taffy/test/corpus/corpus_expected_field.h"

// One page of the deterministic and hostile web fixture corpus, exactly as
// test-fixtures/web/manifest.json declares it.
//
// Every list here is the corpus's own text, unparsed and unsummarised. That is
// the whole reason this struct exists: the protocol requires each benchmark
// case to define "expected semantic fields, sensitive omissions, allowed
// actions, prohibited actions, navigation transitions, and verifier
// postconditions", and a test that restated any of them would keep passing
// after the corpus changed its mind. The corpus is versioned and immutable so
// that a change to it is a deliberate, reviewed baseline change; a copy inside
// a test file is how that guarantee is lost.

namespace taffy::test {

struct CorpusFixture {
  CorpusFixture();
  CorpusFixture(const CorpusFixture&);
  CorpusFixture(CorpusFixture&&) noexcept;
  CorpusFixture& operator=(const CorpusFixture&);
  CorpusFixture& operator=(CorpusFixture&&) noexcept;
  ~CorpusFixture();

  // Stable identifier, for example "iframe-nested-oopif". Tests name fixtures
  // by this and never by path.
  std::string id;
  // Corpus-relative path, for example "origins/primary/frames/nested-oopif.html".
  std::string path;
  // Served path, for example "/frames/nested-oopif.html".
  std::string url_path;
  // Corpus origin key: "primary", "partner", "embed", "hostile".
  std::string origin;
  // Family identifier, for example "F06".
  std::string family;
  std::string title;

  std::vector<CorpusExpectedField> expected_semantic_fields;
  // Values that must never appear in any snapshot, projection, log, export,
  // crash report or test artifact for this fixture.
  std::vector<std::string> sensitive_omissions;
  std::vector<std::string> allowed_actions;
  std::vector<std::string> prohibited_actions;
  std::vector<std::string> navigation_transitions;
  std::vector<std::string> verifier_postconditions;

  // True when the corpus places this page on the adversarial origin. Used by
  // the injection suite to enumerate hostile pages without a second list.
  bool is_hostile_origin() const { return origin == "hostile"; }
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORPUS_CORPUS_FIXTURE_H_
