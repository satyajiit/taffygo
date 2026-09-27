// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SEEDED_SECRET_CORPUS_H_
#define TAFFY_BROWSER_SEEDED_SECRET_CORPUS_H_

#include <stddef.h>

#include <string_view>

// The seeded canaries PAR-SEC-009 is measured against, as compile-time data.
//
// The fixture corpus (`test-fixtures/web/manifest.json`) is the single
// authority for which canaries exist, which class each belongs to and which
// page carries it. This header declares the shape; the translation unit is
// generated from that manifest by
// //taffy/browser/tools/write_seeded_secret_corpus.py, exactly the
// way the upstream provenance is generated from chromium/REVISION.
//
// Copying the tokens into a test file by hand would have been shorter and
// wrong. A corpus change is a version bump with a like-for-like comparison
// (test-fixtures/web/README.md), and a hand-copied list would drift silently
// past that rule until the day it stopped covering the canary somebody added.
// Generating means a corpus change either updates this data or fails the
// build.
//
// **Test-only.** These are canaries, not secrets, and they still have no place
// in a shipping binary: a product that contains the strings its leak detector
// looks for is a product whose leak detector cannot distinguish a leak from
// itself. The GN target is testonly.

namespace taffy {

struct SeededSecret {
  // The canary token as it appears in the fixture page.
  std::string_view token;
  // The class from the manifest: "password", "one-time-code", and so on.
  std::string_view secret_class;
  // The fixture that carries it, so a failure names the page to look at.
  std::string_view carried_by;
};

// The corpus version from the manifest. A suite that reports a result without
// naming the corpus version it ran against is reporting a number nobody can
// reproduce.
std::string_view GetSeededSecretCorpusVersion();

size_t GetSeededSecretCount();

// Indices below GetSeededSecretCount(). Out of range returns an entry whose
// token is empty rather than reading past the end.
const SeededSecret& GetSeededSecret(size_t index);

}  // namespace taffy

#endif  // TAFFY_BROWSER_SEEDED_SECRET_CORPUS_H_
