// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TEST_BIP_FIXTURE_MANIFEST_H_
#define TAFFY_BROWSER_TEST_BIP_FIXTURE_MANIFEST_H_

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"

// The browser-side reader of the deterministic and hostile web fixture corpus
// (test-fixtures/web/manifest.json).
//
// It exists so that a browser test asserts the expectations the corpus
// DECLARES rather than expectations somebody retyped into a test file. The
// corpus is versioned and immutable — "a corpus change is a version bump plus
// a like-for-like comparison run and benchmark-owner approval, never a silent
// baseline rewrite" — and a test with a URL or a transition typed into it would
// keep passing after the corpus moved the fixture it was about.
//
// A missing corpus is a FAILURE, never a skip. Load() CHECK-fails with the
// path it expected. A lifecycle test that quietly passed because it could not
// find the pages it was supposed to navigate would produce the evidence
// without the assurance.
//
// **Why this is not //taffy/renderer/test's FixtureCorpus.** That
// reader answers the renderer's questions: which canary tokens exist, and what
// each fixture must never emit. This one answers the browser's: which origin
// serves a fixture, what navigation transitions it declares, and what its
// verifier postconditions are. Neither set of fields is in the other reader,
// and the two directories cannot include each other — //taffy/browser
// forbids +taffy/renderer, for the authority reason its DEPS file
// explains. Item 12 of browser/README.md's verification list is the proposal
// to merge both into a neutral //taffy/test target once a directory
// exists that both halves may depend on.

namespace taffy::test {

// One fixture page, as the browser cares about it.
struct BipFixture {
  BipFixture();
  BipFixture(const BipFixture&);
  ~BipFixture();

  std::string id;
  // Served path, e.g. "/frames/cross-origin.html".
  std::string url_path;
  // Corpus origin key: "primary", "partner", "embed", "hostile".
  std::string origin;
  std::string family;
  // Declared, in the corpus's own words. Asserted against rather than
  // paraphrased.
  std::vector<std::string> navigation_transitions;
  std::vector<std::string> prohibited_actions;
  std::vector<std::string> verifier_postconditions;
};

class BipFixtureManifest {
 public:
  BipFixtureManifest(const BipFixtureManifest&) = delete;
  BipFixtureManifest& operator=(const BipFixtureManifest&) = delete;
  BipFixtureManifest(BipFixtureManifest&&);
  ~BipFixtureManifest();

  static BipFixtureManifest Load();

  // Where the corpus is expected to be mounted inside a Chromium checkout.
  static base::FilePath ManifestPath();

  // The directory whose files the embedded test server serves for one corpus
  // origin key.
  static base::FilePath OriginRoot(const std::string& origin_key);

  const std::string& version() const { return version_; }
  const std::vector<BipFixture>& fixtures() const { return fixtures_; }

  // CHECK-fails when the identifier is absent: a test naming a fixture the
  // corpus does not contain is a test asserting nothing.
  const BipFixture& ById(const std::string& id) const;

  // The hostname the corpus assigns to an origin key, e.g. "primary" maps to
  // "primary.taffy.test". CHECK-fails for an unknown key.
  std::string HostForOrigin(const std::string& origin_key) const;

 private:
  BipFixtureManifest();

  std::string version_;
  std::vector<BipFixture> fixtures_;
  std::vector<std::pair<std::string, std::string>> origin_hosts_;
  base::FilePath root_;
};

}  // namespace taffy::test

#endif  // TAFFY_BROWSER_TEST_BIP_FIXTURE_MANIFEST_H_
