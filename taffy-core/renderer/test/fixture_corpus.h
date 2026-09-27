// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_TEST_FIXTURE_CORPUS_H_
#define TAFFY_RENDERER_TEST_FIXTURE_CORPUS_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace taffy::test {

// Reads the deterministic and hostile web fixture corpus manifest so that a
// renderer test asserts the expectations the corpus DECLARES rather than
// expectations somebody retyped into a test file.
//
// That distinction is the whole point. The corpus is versioned and immutable:
// "a corpus change is a version bump plus a like-for-like comparison run and
// benchmark-owner approval, never a silent baseline rewrite". A test that
// hard-coded "TAFFYGO-CANARY-PASSWORD-4F1A9C" would keep passing after the
// corpus rotated its canaries, and the rotation is exactly when a leak would
// be introduced.
//
// A missing corpus is a FAILURE, never a skip. A leak test that quietly
// passes because it could not find the secrets it was supposed to look for is
// worse than no leak test: it produces the evidence without the assurance.

// One seeded secret and the fixtures that carry it.
struct Canary {
  Canary();
  Canary(const Canary&);
  ~Canary();

  std::string token;
  // "password", "one-time-code", "session-token", and so on.
  std::string secret_class;
  std::vector<std::string> carried_by;
};

// One fixture page.
struct Fixture {
  Fixture();
  Fixture(const Fixture&);
  ~Fixture();

  std::string id;
  // Repository-relative path, e.g. "origins/primary/auth/login.html".
  std::string path;
  // Served path, e.g. "/auth/login.html".
  std::string url_path;
  std::string origin;
  std::string family;
  // Values the corpus says must never appear in any snapshot, projection,
  // log, export, or crash report for this fixture.
  std::vector<std::string> sensitive_omissions;
};

class FixtureCorpus {
 public:
  FixtureCorpus(const FixtureCorpus&) = delete;
  FixtureCorpus& operator=(const FixtureCorpus&) = delete;
  ~FixtureCorpus();

  // Loads the manifest. CHECK-fails with a message naming the expected path
  // when it is not there: see the note above about skipping.
  static FixtureCorpus Load();

  // Where the corpus is expected to be mounted inside a Chromium checkout.
  // The overlay tooling mounts //taffy from
  // taffy-core; the fixtures need the same treatment,
  // and item 12 of the renderer README verification list is what tracks it.
  static base::FilePath ManifestPath();

  const std::string& version() const { return version_; }
  const std::vector<Canary>& canaries() const { return canaries_; }
  const std::vector<Fixture>& fixtures() const { return fixtures_; }

  // The fixture with this id. CHECK-fails when it is absent, because a test
  // naming a fixture that the corpus does not contain is a test asserting
  // nothing.
  const Fixture& ById(const std::string& id) const;

  // Every canary token, whatever fixture carries it. The leak assertions
  // search for all of them in every message: a token that leaked from the
  // wrong page is still a leak, and a per-fixture search would miss it.
  std::vector<std::string> AllCanaryTokens() const;

  // The full text of one fixture page.
  std::string ReadFixtureHtml(const std::string& id) const;

  FixtureCorpus(FixtureCorpus&&);

 private:
  FixtureCorpus();

  std::string version_;
  std::vector<Canary> canaries_;
  std::vector<Fixture> fixtures_;
  base::FilePath root_;
};

}  // namespace taffy::test

#endif  // TAFFY_RENDERER_TEST_FIXTURE_CORPUS_H_
