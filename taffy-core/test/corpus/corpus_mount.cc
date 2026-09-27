// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/corpus/corpus_mount.h"

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"

namespace taffy::test {

namespace {

// The mount point, relative to the test data root. It matches the path the
// browser and renderer readers already expect, so moving the corpus is one
// edit here rather than one edit per directory.
constexpr base::FilePath::CharType kCorpusRelativePath[] =
    FILE_PATH_LITERAL("taffy/test/data/web");

base::FilePath TestDataRoot() {
  base::FilePath root;
  // VERIFY AT SP-01: base::DIR_SRC_TEST_DATA_ROOT is the current spelling of
  // what used to be base::DIR_SOURCE_ROOT. Upstream file to read:
  // base/base_paths.h. Only this line changes if the pinned milestone still
  // uses the older key.
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root))
      << "No test data root. Nothing in this directory can run without one.";
  return root;
}

}  // namespace

// static
base::FilePath CorpusMount::Root() {
  return TestDataRoot().Append(kCorpusRelativePath);
}

// static
base::FilePath CorpusMount::ManifestPath() {
  return Root().Append(FILE_PATH_LITERAL("manifest.json"));
}

// static
base::FilePath CorpusMount::OriginRoot(std::string_view origin_key) {
  CHECK(!origin_key.empty()) << "An empty corpus origin key names no directory.";
  return Root()
      .Append(FILE_PATH_LITERAL("origins"))
      .AppendASCII(std::string(origin_key));
}

// static
base::FilePath CorpusMount::SharedRoot() {
  return Root().Append(FILE_PATH_LITERAL("shared"));
}

// static
bool CorpusMount::IsMounted() {
  return base::PathExists(ManifestPath());
}

// static
std::string CorpusMount::MountInstructions() {
  return base::StrCat(
      {"The web fixture corpus is not mounted. Expected the manifest at ",
       ManifestPath().AsUTF8Unsafe(),
       ". Mount test-fixtures/web there the same way ./tools/chromium/sync "
       "mounts taffy-core at src/taffy. "
       "This is a failure rather than a skip on purpose: a suite that passed "
       "because it could not find the pages or the seeded secrets it was "
       "supposed to read would produce the evidence without the assurance."});
}

}  // namespace taffy::test
