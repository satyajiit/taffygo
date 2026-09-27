// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_pack.h"

#include <string>
#include <string_view>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "taffy/browser/assets/asset_store.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/zlib/google/zip.h"

namespace taffy {
namespace {

constexpr size_t kMaxPathBytes = 256u;
constexpr size_t kMaxBytes = 4u * 1024u * 1024u;

class AssetPackTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    ASSERT_TRUE(contents_.CreateUniqueTempDir());
  }

  AssetStore Store() { return AssetStore(directory_.GetPath()); }

  // Adds one member to what the next `Install` will pack.
  void AddMember(std::string_view relative_path, std::string_view bytes) {
    const base::FilePath path =
        contents_.GetPath().AppendASCII(relative_path);
    ASSERT_TRUE(base::CreateDirectory(path.DirName()));
    ASSERT_TRUE(base::WriteFile(path, bytes));
  }

  // Packs everything added so far and installs it as one asset.
  //
  // Through the store's own staging and commit rather than by writing into its
  // tree, so the test exercises the layout the product actually produces.
  void Install(const std::string& asset_id, const std::string& revision) {
    const base::FilePath archive =
        contents_.GetPath().DirName().AppendASCII(asset_id + ".zip");
    ASSERT_TRUE(zip::Zip(contents_.GetPath(), archive, /*include_hidden_files=*/false));
    std::string packed;
    ASSERT_TRUE(base::ReadFileToString(archive, &packed));
    InstallBytes(asset_id, revision, packed);
  }

  // Installs `bytes` verbatim, for the cases where they are not an archive.
  void InstallBytes(const std::string& asset_id,
                    const std::string& revision,
                    std::string_view bytes) {
    AssetStore store = Store();
    base::File staged = store.OpenStaging(asset_id, revision, 0);
    ASSERT_TRUE(staged.IsValid());
    ASSERT_TRUE(staged.WriteAtCurrentPosAndCheck(base::as_byte_span(bytes)));
    staged.Close();
    ASSERT_TRUE(store.Commit(asset_id, revision));
  }

  base::ScopedTempDir directory_;
  base::ScopedTempDir contents_;
};

TEST_F(AssetPackTest, AMemberIsReadByNameWithoutUnpackingTheRest) {
  AddMember("flags/gb.svg", "<svg id=gb/>");
  AddMember("flags/in.svg", "<svg id=in/>");
  AddMember("readme.txt", "not a flag");
  Install("country-flags", "1");

  const PackMemberResult result = ReadPackMember(
      Store(), "country-flags", "flags/in.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kOk);
  EXPECT_EQ(std::string(result.bytes.begin(), result.bytes.end()),
            "<svg id=in/>");
}

TEST_F(AssetPackTest, WhicheverRevisionIsInstalledIsTheOneRead) {
  // The caller names no revision on purpose: it wants what is on the device.
  AddMember("flags/gb.svg", "old");
  Install("country-flags", "1");
  ASSERT_TRUE(base::DeletePathRecursively(contents_.GetPath()));
  ASSERT_TRUE(base::CreateDirectory(contents_.GetPath()));
  AddMember("flags/gb.svg", "new");
  Install("country-flags", "2");

  const PackMemberResult result = ReadPackMember(
      Store(), "country-flags", "flags/gb.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kOk);
  EXPECT_EQ(std::string(result.bytes.begin(), result.bytes.end()), "new");
}

TEST_F(AssetPackTest, AnAssetThatIsNotThereIsNotAFailure) {
  const PackMemberResult result = ReadPackMember(
      Store(), "country-flags", "flags/gb.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kNotInstalled);
  EXPECT_TRUE(result.bytes.empty());
}

TEST_F(AssetPackTest, ANameThePackDoesNotCarryIsAMissAndNotADefect) {
  AddMember("flags/gb.svg", "<svg id=gb/>");
  Install("country-flags", "1");

  const PackMemberResult result = ReadPackMember(
      Store(), "country-flags", "flags/zz.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kNotFound);
}

TEST_F(AssetPackTest, BytesThatAreNotAnArchiveAreUnreadableRatherThanEmpty) {
  // An installed asset the catalog packs as a container, that is not one, is a
  // defect in what was delivered. Answering "no such member" would report it
  // as an ordinary miss and hide a broken download behind a missing flag.
  InstallBytes("country-flags", "1", "these are not the bytes of an archive");

  const PackMemberResult result = ReadPackMember(
      Store(), "country-flags", "flags/gb.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kUnreadable);
}

TEST_F(AssetPackTest, AMemberPastTheBoundReturnsNothingRatherThanSomeOfIt) {
  AddMember("flags/gb.svg", std::string(4096, 'a'));
  Install("country-flags", "1");

  const PackMemberResult result = ReadPackMember(
      Store(), "country-flags", "flags/gb.svg", kMaxPathBytes, /*max_bytes=*/16u);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kTooLarge);
  EXPECT_TRUE(result.bytes.empty());
}

TEST_F(AssetPackTest, ANameThatWouldEscapeThePackIsRefusedBeforeItIsOpened) {
  AddMember("flags/gb.svg", "<svg id=gb/>");
  Install("country-flags", "1");
  const std::string_view escapes[] = {
      "../artifact", "flags/../../artifact", "/flags/gb.svg",
      "flags//gb.svg", "flags\\gb.svg",      "./flags/gb.svg",
      "flags/",       "",
  };

  for (std::string_view name : escapes) {
    const PackMemberResult result =
        ReadPackMember(Store(), "country-flags", name, kMaxPathBytes, kMaxBytes);
    EXPECT_EQ(result.verdict, PackMemberVerdict::kNotFound) << name;
    EXPECT_TRUE(result.bytes.empty()) << name;
  }
}

TEST_F(AssetPackTest, ANamePastThePathBoundIsRefused) {
  AddMember("flags/gb.svg", "<svg id=gb/>");
  Install("country-flags", "1");

  const PackMemberResult result =
      ReadPackMember(Store(), "country-flags", std::string(300, 'a'),
                     kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kNotFound);
}

TEST_F(AssetPackTest, AnUnsafeAssetIdentityNeverReachesTheDisk) {
  const PackMemberResult result = ReadPackMember(
      Store(), "../../etc", "flags/gb.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kNotInstalled);
}

TEST_F(AssetPackTest, AStoreWithNoRootReadsNothing) {
  const AssetStore nowhere{base::FilePath()};

  const PackMemberResult result = ReadPackMember(
      nowhere, "country-flags", "flags/gb.svg", kMaxPathBytes, kMaxBytes);

  EXPECT_EQ(result.verdict, PackMemberVerdict::kNotInstalled);
}

}  // namespace
}  // namespace taffy
