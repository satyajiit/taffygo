// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/bundled_asset_seeder.h"

#include <array>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"
#include "taffy/browser/assets/asset_store.h"
#include "taffy/browser/assets/bundled_asset_source.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

// What these tests are about, and what they deliberately are not.
//
// They are about the seeder's decisions: install, skip, refuse, and what the
// store is left holding in each case. They are **not** about whether
// `kBundledAssetRows` describes the four artifacts this repository commits.
// That is a question about the tree rather than about a running browser, and
// the `catalog` lane answers it every run by measuring the committed bytes
// against the rows it generates them into. Asserting it here as well would be
// a second, weaker copy of that check — weaker because a suite can only fail
// on a host that has the tree, and this one is meant to run on a phone.

std::string Digest(std::string_view bytes) {
  std::array<uint8_t, 32> digest = {};
  crypto::hash::Hash(crypto::hash::kSha256, base::as_byte_span(bytes), digest);
  return base::ToLowerASCII(base::HexEncode(digest));
}

class BundledAssetSeederTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(store_directory_.CreateUniqueTempDir());
    ASSERT_TRUE(package_.CreateUniqueTempDir());
    package_assets_ = package_.GetPath().AppendASCII("assets").AppendASCII(
        "taffy-delivery");
    ASSERT_TRUE(base::CreateDirectory(package_assets_));
  }

  // Puts one artifact in the stand-in package and answers the row that names
  // it. `pinned` is what the catalogue claims, which is the same as the bytes
  // unless a test is asking what happens when it is not.
  BundledAssetRow Package(std::string_view file,
                          std::string_view bytes,
                          std::string_view asset_id,
                          std::string_view revision) {
    EXPECT_TRUE(base::WriteFile(package_assets_.AppendASCII(file), bytes));
    paths_.push_back(base::StrCat({"assets/taffy-delivery/", file}));
    digests_.push_back(Digest(bytes));
    return BundledAssetRow{asset_id, revision, paths_.back(), bytes.size(),
                           digests_.back()};
  }

  AssetStore Store() { return AssetStore(store_directory_.GetPath()); }

  DirectoryBundledAssetSource Source() {
    return DirectoryBundledAssetSource(package_.GetPath());
  }

  base::ScopedTempDir store_directory_;
  base::ScopedTempDir package_;
  base::FilePath package_assets_;
  // A deque rather than a vector, because a row holds views into these and
  // a second `Package` call would leave the first row's views dangling if the
  // storage moved. That is a real hazard rather than a stylistic one: two of
  // the tests below build one row, then another.
  std::deque<std::string> paths_;
  std::deque<std::string> digests_;
};

TEST_F(BundledAssetSeederTest, APackagedArtifactIsInstalledAndReadable) {
  const std::string bytes = "the pinned artifact";
  const BundledAssetRow row =
      Package("part.zip", bytes, "python-stdlib", "3.14.7-taffy.1");

  DirectoryBundledAssetSource source = Source();
  const BundledSeedOutcome outcome = SeedBundledAssets(
      store_directory_.GetPath(), source, base::span_from_ref(row));

  EXPECT_EQ(1, outcome.seeded);
  EXPECT_EQ(0, outcome.absent);
  EXPECT_EQ(0, outcome.refused);
  EXPECT_EQ(0, outcome.already_installed);

  AssetStore store = Store();
  EXPECT_EQ("3.14.7-taffy.1", store.InstalledRevision("python-stdlib"));
  base::File installed = store.OpenInstalled("python-stdlib", "3.14.7-taffy.1");
  ASSERT_TRUE(installed.IsValid());
  EXPECT_EQ(static_cast<int64_t>(bytes.size()), installed.GetLength());
}

TEST_F(BundledAssetSeederTest, ASecondPassInstallsNothingAgain) {
  const BundledAssetRow row =
      Package("part.zip", "bytes", "start-scenes", "20260910-taffy.1");
  DirectoryBundledAssetSource source = Source();

  EXPECT_EQ(1, SeedBundledAssets(store_directory_.GetPath(), source,
                                 base::span_from_ref(row))
                   .seeded);
  const BundledSeedOutcome again = SeedBundledAssets(
      store_directory_.GetPath(), source, base::span_from_ref(row));
  EXPECT_EQ(1, again.already_installed);
  EXPECT_EQ(0, again.seeded);
}

TEST_F(BundledAssetSeederTest, ADifferentInstalledRevisionIsNotThisOne) {
  // The identity is installed and the revision is not, which is a device that
  // upgraded. Asking only whether *something* is installed would leave it
  // holding the old artifact while the compiled catalogue names the new one.
  const BundledAssetRow row =
      Package("part.zip", "bytes", "country-flags", "7.5.0-taffy.1");
  {
    AssetStore store = Store();
    base::File staged = store.OpenStaging("country-flags", "7.4.0-taffy.1", 0);
    ASSERT_TRUE(staged.IsValid());
    const std::string previous = "the previous revision";
    ASSERT_TRUE(staged.WriteAtCurrentPosAndCheck(base::as_byte_span(previous)));
    staged.Close();
    ASSERT_TRUE(store.Commit("country-flags", "7.4.0-taffy.1"));
  }

  DirectoryBundledAssetSource source = Source();
  EXPECT_EQ(1, SeedBundledAssets(store_directory_.GetPath(), source,
                                 base::span_from_ref(row))
                   .seeded);
  AssetStore store = Store();
  EXPECT_TRUE(store.OpenInstalled("country-flags", "7.5.0-taffy.1").IsValid());
  EXPECT_TRUE(store.OpenInstalled("country-flags", "7.4.0-taffy.1").IsValid());
}

TEST_F(BundledAssetSeederTest, BytesThatAreNotWhatTheCatalogueSaysInstallNothing) {
  BundledAssetRow row =
      Package("part.zip", "the packaged bytes", "easylist-base", "1-taffy.1");
  // The package and the compiled rows disagree, which is a build that was not
  // repackaged after the catalogue moved.
  digests_.back() = std::string(64u, '0');
  row.digest = digests_.back();

  DirectoryBundledAssetSource source = Source();
  const BundledSeedOutcome outcome = SeedBundledAssets(
      store_directory_.GetPath(), source, base::span_from_ref(row));

  EXPECT_EQ(1, outcome.refused);
  EXPECT_EQ(0, outcome.seeded);
  AssetStore store = Store();
  EXPECT_FALSE(store.OpenInstalled("easylist-base", "1-taffy.1").IsValid());
  // And nothing was staged either: the refusal happens before a byte is
  // written, so the next scan does not report a partial transfer that no
  // origin can ever finish.
  EXPECT_EQ(0u, store.StagedBytes("easylist-base", "1-taffy.1"));
}

TEST_F(BundledAssetSeederTest, ALengthThatDisagreesInstallsNothing) {
  BundledAssetRow row =
      Package("part.zip", "twelve bytes", "easylist-base", "1-taffy.1");
  row.transfer_bytes += 1u;

  DirectoryBundledAssetSource source = Source();
  EXPECT_EQ(1, SeedBundledAssets(store_directory_.GetPath(), source,
                                 base::span_from_ref(row))
                   .refused);
  EXPECT_FALSE(Store().OpenInstalled("easylist-base", "1-taffy.1").IsValid());
}

TEST_F(BundledAssetSeederTest, AnArtifactThePackageDoesNotCarryIsNotAFailure) {
  // Named locals, because the row holds views and a temporary would be gone
  // before the call.
  const std::string digest(64u, 'a');
  const std::string path = "assets/taffy-delivery/absent.zip";
  const BundledAssetRow row{"python-toolkit", "1-taffy.1", path, 4u, digest};
  DirectoryBundledAssetSource source = Source();
  const BundledSeedOutcome outcome = SeedBundledAssets(
      store_directory_.GetPath(), source, base::span_from_ref(row));
  EXPECT_EQ(1, outcome.absent);
  EXPECT_EQ(0, outcome.refused);
}

TEST_F(BundledAssetSeederTest, EveryRowIsTriedEvenWhenOneIsRefused) {
  // One bad row must not stop the rest. A build that repackaged three of four
  // artifacts should install three, not none.
  BundledAssetRow bad =
      Package("bad.zip", "wrong", "easylist-base", "1-taffy.1");
  digests_.back() = std::string(64u, '0');
  bad.digest = digests_.back();
  const BundledAssetRow good =
      Package("good.zip", "right", "start-scenes", "2-taffy.1");
  const std::array<BundledAssetRow, 2> rows = {bad, good};

  DirectoryBundledAssetSource source = Source();
  const BundledSeedOutcome outcome =
      SeedBundledAssets(store_directory_.GetPath(), source, rows);
  EXPECT_EQ(1, outcome.refused);
  EXPECT_EQ(1, outcome.seeded);
  EXPECT_TRUE(Store().OpenInstalled("start-scenes", "2-taffy.1").IsValid());
}

TEST_F(BundledAssetSeederTest, AStoreWithNoRootInstallsNothingAndRefusesNothing) {
  // An empty root is a store with nowhere to put anything, which is what a
  // build or a test with no asset directory gets. It must not read as a
  // package whose bytes were wrong, which is a different and alarming thing.
  const BundledAssetRow row =
      Package("part.zip", "bytes", "start-scenes", "2-taffy.1");
  DirectoryBundledAssetSource source = Source();
  const BundledSeedOutcome outcome =
      SeedBundledAssets(base::FilePath(), source, base::span_from_ref(row));
  EXPECT_EQ(0, outcome.seeded);
  EXPECT_EQ(0, outcome.refused);
  EXPECT_EQ(0, outcome.absent);
  EXPECT_EQ(0, outcome.already_installed);
}

TEST_F(BundledAssetSeederTest, ALargeArtifactCrossesTheChunkBoundary) {
  // The write is chunked so peak memory is a chunk rather than an artifact,
  // and an off-by-one there would truncate every row past the first chunk.
  const std::string bytes(700u * 1024u, 'z');
  const BundledAssetRow row =
      Package("big.zip", bytes, "python-stdlib", "3-taffy.1");
  DirectoryBundledAssetSource source = Source();
  EXPECT_EQ(1, SeedBundledAssets(store_directory_.GetPath(), source,
                                 base::span_from_ref(row))
                   .seeded);
  base::File installed = Store().OpenInstalled("python-stdlib", "3-taffy.1");
  ASSERT_TRUE(installed.IsValid());
  EXPECT_EQ(static_cast<int64_t>(bytes.size()), installed.GetLength());
}

TEST_F(BundledAssetSeederTest, OnlyAnInstallIsNamed) {
  // The counters say how a pass went; `seeded_ids` says what changed, and the
  // plane announces exactly those to readers that cached an empty store. A
  // name here for an artifact that was not installed would refresh a reader
  // over bytes that are not there; a missing name is a reader left holding
  // "absent" for the life of the process.
  BundledAssetRow bad = Package("bad.zip", "wrong", "easylist-base", "1-taffy.1");
  digests_.back() = std::string(64u, '0');
  bad.digest = digests_.back();
  const BundledAssetRow good =
      Package("good.zip", "right", "start-scenes", "2-taffy.1");
  const BundledAssetRow missing{"python-stdlib", "3-taffy.1",
                                "assets/taffy-delivery/absent.zip", 4u,
                                digests_.back()};
  const std::array<BundledAssetRow, 3> rows = {bad, good, missing};

  DirectoryBundledAssetSource source = Source();
  const BundledSeedOutcome first =
      SeedBundledAssets(store_directory_.GetPath(), source, rows);
  ASSERT_EQ(1u, first.seeded_ids.size());
  EXPECT_EQ("start-scenes", first.seeded_ids.front());

  // And nothing is named on the pass that installs nothing, which is every
  // launch after the first.
  const BundledSeedOutcome again =
      SeedBundledAssets(store_directory_.GetPath(), source, rows);
  EXPECT_EQ(1, again.already_installed);
  EXPECT_TRUE(again.seeded_ids.empty());
}

TEST_F(BundledAssetSeederTest, APathThatTraversesMapsNothing) {
  DirectoryBundledAssetSource source = Source();
  EXPECT_EQ(nullptr, source.Map("assets/../../etc/passwd"));
  EXPECT_EQ(nullptr, source.Map(""));
}

}  // namespace
}  // namespace taffy
