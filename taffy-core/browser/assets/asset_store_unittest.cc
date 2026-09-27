// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_store.h"

#include <string>
#include <string_view>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class AssetStoreTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  AssetStore Store() { return AssetStore(directory_.GetPath()); }

  // Writes `bytes` into the staging file the store would have written.
  void Stage(const std::string& asset_id,
             const std::string& revision,
             const std::string& bytes) {
    AssetStore store = Store();
    base::File file = store.OpenStaging(asset_id, revision, 0);
    ASSERT_TRUE(file.IsValid());
    ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(base::as_byte_span(bytes)));
  }

  base::ScopedTempDir directory_;
};

TEST_F(AssetStoreTest, OnlyTheCatalogsAlphabetBecomesAPathSegment) {
  EXPECT_TRUE(AssetStore::IsSafeComponent("python-stdlib"));
  EXPECT_TRUE(AssetStore::IsSafeComponent("3.14.2+taffy.1"));
  // Spelled as `string_view`s with explicit lengths, because the interesting
  // one carries an embedded NUL and a `const char*` would silently become the
  // single safe character before it.
  for (const std::string_view value :
       {std::string_view(""), std::string_view("."), std::string_view(".."),
        std::string_view("a/b"), std::string_view("a\\b"),
        std::string_view("A"), std::string_view("a_b"), std::string_view("-a"),
        std::string_view("a-"), std::string_view("a--b"),
        std::string_view("a b"), std::string_view("a\0b", 3)}) {
    EXPECT_FALSE(AssetStore::IsSafeComponent(value)) << value;
  }
}

TEST_F(AssetStoreTest, AnUnsafeComponentOpensNothing) {
  AssetStore store = Store();
  EXPECT_FALSE(store.OpenStaging("../escape", "1", 0).IsValid());
  EXPECT_FALSE(store.OpenStaging("ok", "../escape", 0).IsValid());
  EXPECT_FALSE(store.OpenInstalled("../escape", "1").IsValid());
}

TEST_F(AssetStoreTest, AScanOfAnEmptyStoreFindsNothingAndDoesNotFail) {
  EXPECT_TRUE(Store().Scan().empty());
}

TEST_F(AssetStoreTest, AStagedTransferIsFoundWithItsByteCount) {
  Stage("python-stdlib", "3.14.2", "0123456789");
  const std::vector<AssetStore::Found> found = Store().Scan();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].asset_id, "python-stdlib");
  EXPECT_EQ(found[0].asset_revision, "3.14.2");
  EXPECT_EQ(found[0].staged_bytes, 10u);
  EXPECT_FALSE(found[0].installed);
}

TEST_F(AssetStoreTest, CommittingMovesTheStagedFileAndTheScanSaysSo) {
  Stage("python-stdlib", "3.14.2", "0123456789");
  AssetStore store = Store();
  EXPECT_TRUE(store.Commit("python-stdlib", "3.14.2"));

  const std::vector<AssetStore::Found> found = store.Scan();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_TRUE(found[0].installed);
  EXPECT_EQ(found[0].installed_bytes, 10u);
  EXPECT_EQ(found[0].staged_bytes, 0u)
      << "a committed artifact leaves nothing in staging";
  EXPECT_TRUE(store.OpenInstalled("python-stdlib", "3.14.2").IsValid());
}

TEST_F(AssetStoreTest, CommittingWhatWasNeverStagedFails) {
  EXPECT_FALSE(Store().Commit("python-stdlib", "3.14.2"));
}

TEST_F(AssetStoreTest, ResumingTruncatesToTheOffsetTheCallerNamed) {
  Stage("python-stdlib", "3.14.2", "0123456789");
  AssetStore store = Store();
  // Asking to resume from four means the caller intends bytes 4.. to follow,
  // so the six bytes past it are discarded rather than left as a gap.
  base::File file = store.OpenStaging("python-stdlib", "3.14.2", 4);
  ASSERT_TRUE(file.IsValid());
  file = base::File();
  EXPECT_EQ(store.StagedBytes("python-stdlib", "3.14.2"), 4u);
}

TEST_F(AssetStoreTest, RemovingFreesBothHalvesAndReportsTheBytes) {
  Stage("python-stdlib", "3.14.2", "0123456789");
  AssetStore store = Store();
  ASSERT_TRUE(store.Commit("python-stdlib", "3.14.2"));
  Stage("python-stdlib", "3.14.2", "abc");

  EXPECT_EQ(store.Remove("python-stdlib", "3.14.2"), 13u);
  EXPECT_TRUE(store.Scan().empty());
  EXPECT_FALSE(store.OpenInstalled("python-stdlib", "3.14.2").IsValid());
}

TEST_F(AssetStoreTest, RemovingSomethingAbsentIsNotAFailure) {
  EXPECT_EQ(Store().Remove("python-stdlib", "3.14.2"), 0u);
}

TEST_F(AssetStoreTest, TwoRevisionsCoexistSoAnUpgradeCannotStrandADevice) {
  Stage("python-stdlib", "3.14.1", "old");
  ASSERT_TRUE(Store().Commit("python-stdlib", "3.14.1"));
  Stage("python-stdlib", "3.14.2", "newer");
  ASSERT_TRUE(Store().Commit("python-stdlib", "3.14.2"));

  const std::vector<AssetStore::Found> found = Store().Scan();
  ASSERT_EQ(found.size(), 2u);
  EXPECT_TRUE(found[0].installed);
  EXPECT_TRUE(found[1].installed);
}

TEST_F(AssetStoreTest, AFileTheStoreDidNotWriteIsIgnoredAndNotDeleted) {
  const base::FilePath stray =
      directory_.GetPath().AppendASCII("staging").AppendASCII("NOT-OURS.part");
  ASSERT_TRUE(base::CreateDirectory(stray.DirName()));
  ASSERT_TRUE(base::WriteFile(stray, "x"));

  EXPECT_TRUE(Store().Scan().empty()) << "an unreadable name is not reported";
  EXPECT_TRUE(base::PathExists(stray))
      << "deleting a file nobody understands is how a bug becomes data loss";
}


TEST_F(AssetStoreTest, AStoreWithNoRootReadsNothingAndWritesNothing) {
  // What a build or a test with no asset directory gets. The alternative is a
  // relative path resolved against whatever the working directory happens to
  // be, which is a store that writes somewhere nobody chose.
  AssetStore nowhere{base::FilePath()};
  EXPECT_TRUE(nowhere.Scan().empty());
  EXPECT_FALSE(nowhere.OpenStaging("python-stdlib", "1", 0).IsValid());
  EXPECT_FALSE(nowhere.OpenInstalled("python-stdlib", "1").IsValid());
  EXPECT_FALSE(nowhere.Commit("python-stdlib", "1"));
  EXPECT_EQ(nowhere.Remove("python-stdlib", "1"), 0u);
}

}  // namespace
}  // namespace taffy
