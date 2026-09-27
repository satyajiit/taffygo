// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORPUS_CORPUS_MOUNT_H_
#define TAFFY_TEST_CORPUS_CORPUS_MOUNT_H_

#include <string>
#include <string_view>

#include "base/files/file_path.h"

// Where the deterministic and hostile web fixture corpus is expected to be,
// inside a Chromium checkout.
//
// The overlay tooling symlink-mounts taffy-core at
// src/taffy. The corpus needs the same treatment at
// src/taffy/test/data/web, and until it has it every suite that
// reads a page fails at load with the instruction below in the message.
//
// That is deliberate and it is the one behaviour in this directory worth
// stating twice: a missing corpus is a FAILURE, never a skip. A redaction test
// that quietly passed because it could not find the secrets it was supposed to
// look for would produce the evidence without the assurance, and a lifecycle
// test that skipped because it could not find the pages it was supposed to
// navigate would do the same. Both are worse than no test, because both appear
// in the exit-evidence packet as a green row.
//
// Separated from CorpusManifest because "where is the corpus" and "what does
// the corpus declare" are different questions with different failure modes.
// The mount can be wrong while the manifest is perfect, and the message a
// developer needs is completely different in each case.

namespace taffy::test {

class CorpusMount {
 public:
  CorpusMount() = delete;

  // src/taffy/test/data/web
  static base::FilePath Root();

  static base::FilePath ManifestPath();

  // The directory an embedded test server serves for one corpus origin key.
  static base::FilePath OriginRoot(std::string_view origin_key);

  // The shared assets the corpus serves under /_fixture/.
  static base::FilePath SharedRoot();

  // True when the manifest is readable. Callers use it to produce a better
  // message, never to skip: see the note above.
  static bool IsMounted();

  // The exact instruction a developer needs when it is not, including the
  // absolute path that was tried. Used in every failure message so that the
  // fix does not have to be looked up.
  static std::string MountInstructions();
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORPUS_CORPUS_MOUNT_H_
