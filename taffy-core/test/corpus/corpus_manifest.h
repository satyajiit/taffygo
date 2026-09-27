// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORPUS_CORPUS_MANIFEST_H_
#define TAFFY_TEST_CORPUS_CORPUS_MANIFEST_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/no_destructor.h"
#include "taffy/test/corpus/corpus_canary.h"
#include "taffy/test/corpus/corpus_fixture.h"
#include "taffy/test/corpus/corpus_origin.h"

// The single reader of test-fixtures/web/manifest.json for every Taffy test,
// in either process half.
//
// It replaces two readers that were written independently because no directory
// existed that both halves could depend on: //taffy/browser/test
// answered the browser's questions (origins, navigation transitions, verifier
// postconditions) and //taffy/renderer/test answered the renderer's
// (canary tokens, per-fixture omissions). Neither could include the other,
// because //taffy/browser forbids +taffy/renderer for the
// authority reason its DEPS file explains. This directory is below both, so it
// can hold the union without weakening that rule. Retiring the two originals is
// a build-file edit in each of their directories and is item 1 of this
// directory's verification list.
//
// Three properties are the reason a reader exists at all rather than a header
// of constants:
//
//   1. The corpus is the authority. A test asserts what the corpus DECLARES.
//      Retyping an expectation into a test file makes the test survive a corpus
//      change that should have failed it, and the corpus is versioned and
//      immutable precisely so that such a change is deliberate and reviewed.
//   2. A missing corpus is a failure, never a skip. Get() CHECK-fails with
//      CorpusMount::MountInstructions().
//   3. A fixture named but absent is a failure. ById() CHECK-fails, because a
//      test naming a page the corpus does not contain is a test asserting
//      nothing.
//
// The manifest is parsed once per process and cached. It is immutable data on
// disk, so there is nothing to invalidate, and re-reading it per test would put
// file input/output in the middle of a browser test's timing.

namespace taffy::test {

class CorpusManifest {
 public:
  CorpusManifest(const CorpusManifest&) = delete;
  CorpusManifest& operator=(const CorpusManifest&) = delete;
  ~CorpusManifest();

  // The process-wide instance. CHECK-fails when the corpus is not mounted or
  // is not internally consistent.
  static const CorpusManifest& Get();

  // The immutable corpus version. Recorded in every benchmark and exit-evidence
  // report, so a suite that wants to state what it ran against reads it here.
  const std::string& version() const { return version_; }
  const std::string& corpus_name() const { return corpus_name_; }

  const std::vector<CorpusFixture>& fixtures() const { return fixtures_; }
  const std::vector<CorpusCanary>& canaries() const { return canaries_; }
  const std::vector<CorpusOrigin>& origins() const { return origins_; }

  // CHECK-fails when absent.
  const CorpusFixture& ById(std::string_view id) const;
  const CorpusOrigin& OriginByKey(std::string_view key) const;

  // Every fixture in one family, in manifest order. Used by the suites that
  // iterate a family rather than naming its members, so that adding a fixture
  // to a family extends the suite instead of leaving it behind.
  std::vector<const CorpusFixture*> ByFamily(std::string_view family) const;

  // Every declared token, whatever fixture carries it. Leak assertions search
  // for all of them everywhere: a token that leaked out of the wrong page is
  // still a leak, and a per-fixture search would miss exactly that case.
  std::vector<std::string> AllCanaryTokens() const;

  // Every value the corpus says must not appear in an observation of this
  // fixture: its canary tokens plus its declared sensitive omissions.
  std::vector<std::string> ProhibitedValuesFor(std::string_view fixture_id)
      const;

  // The page source, read from the mounted corpus.
  std::string ReadFixtureHtml(std::string_view fixture_id) const;

  // The adversarial origin's collection endpoint. A correct run never issues a
  // request to it, so the suites assert on its absence rather than its shape.
  const std::string& exfiltration_sink_origin() const {
    return exfiltration_sink_origin_;
  }
  const std::string& exfiltration_sink_path() const {
    return exfiltration_sink_path_;
  }

 private:
  // Get() holds the single instance in a base::NoDestructor, which has to be
  // able to reach the private constructor. The constructor stays private so
  // that Get() is the only way to obtain a manifest: a second, unloaded
  // instance would answer every question with an empty corpus.
  friend class base::NoDestructor<CorpusManifest>;

  CorpusManifest();

  // Parses the mounted manifest into `this`. CHECK-fails on anything that would
  // let a suite run against a corpus it cannot trust.
  void Load();

  std::string corpus_name_;
  std::string version_;
  std::string exfiltration_sink_origin_;
  std::string exfiltration_sink_path_;
  std::vector<CorpusOrigin> origins_;
  std::vector<CorpusCanary> canaries_;
  std::vector<CorpusFixture> fixtures_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORPUS_CORPUS_MANIFEST_H_
