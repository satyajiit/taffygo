// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {

class DormantBackupRestoreTargetTestPeer {
 public:
  static bool HasOwnedDatabasePath(const DormantBackupRestoreTarget& target) {
    return target.HasOwnedDatabasePath();
  }
};

namespace {

using Error = DormantBackupRestoreTargetError;

class DormantBackupRestoreTargetTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(root_.CreateUniqueTempDir());
    profile_path_ = root_.GetPath().AppendASCII("Profile 1");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
    profile_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    core_path_ = profile_path_.AppendASCII("TaffyCore");
  }

  auto Create() {
    return DormantBackupRestoreTarget::Create(profile_path_, profile_id_,
                                              false);
  }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  base::FilePath core_path_;
  std::string profile_id_;
};

TEST_F(DormantBackupRestoreTargetTest,
       FreshTargetContainsOnlySchemaAndTheReservedLocalIdentity) {
  auto target = Create();
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ((*target)->profile_id(), profile_id_);
  const auto path = (*target)->database_path();
  EXPECT_EQ(path, core_path_.AppendASCII("core.sqlite3"));
  target->reset();

  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::Statement identity(
      database.GetUniqueStatement("SELECT browser_profile_id FROM "
                                  "core_profile_identity WHERE singleton=1"));
  ASSERT_TRUE(identity.Step());
  EXPECT_EQ(identity.ColumnString(0), profile_id_);
  EXPECT_FALSE(identity.Step());
  EXPECT_TRUE(identity.Succeeded());
  sql::Statement ledger(database.GetUniqueStatement(
      "SELECT component,version,checksum FROM taffy_storage_schema"));
  ASSERT_TRUE(ledger.Step());
  EXPECT_EQ(ledger.ColumnString(0), storage_schema::kComponent);
  EXPECT_EQ(ledger.ColumnInt(1), static_cast<int>(storage_schema::kVersion));
  EXPECT_EQ(ledger.ColumnString(2), storage_schema::kChecksum);
  EXPECT_FALSE(ledger.Step());
  EXPECT_TRUE(ledger.Succeeded());

  sql::Statement tables(database.GetUniqueStatement(
      "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT IN "
      "('taffy_storage_schema','core_profile_identity','sqlite_sequence')"));
  size_t count = 0;
  while (tables.Step()) {
    const std::string name = tables.ColumnString(0);
    SCOPED_TRACE(name);
    ASSERT_TRUE(name.starts_with("core_"));
    sql::Statement rows(
        database.GetUniqueStatement("SELECT COUNT(*) FROM " + name));
    ASSERT_TRUE(rows.Step());
    EXPECT_EQ(rows.ColumnInt64(0), 0);
    ++count;
  }
  EXPECT_TRUE(tables.Succeeded());
  EXPECT_GT(count, 20u);
}

TEST_F(DormantBackupRestoreTargetTest,
       DestructionKeepsCandidateForTheDurableReservationOwner) {
  const auto sibling = profile_path_.AppendASCII("Preferences");
  ASSERT_TRUE(base::WriteFile(sibling, "owned by Chromium"));
  auto target = Create();
  ASSERT_TRUE(target.has_value());
  const auto database_path = (*target)->database_path();
  target->reset();
  EXPECT_TRUE(base::PathExists(database_path));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(sibling, &contents));
  EXPECT_EQ(contents, "owned by Chromium");
  auto repeated = Create();
  ASSERT_FALSE(repeated.has_value());
  EXPECT_EQ(repeated.error(), Error::kTargetOccupied);
  EXPECT_TRUE(base::PathExists(database_path));
}

TEST_F(DormantBackupRestoreTargetTest,
       ExistingEmptyCoreDirectoryIsNotAnOwnedFreshCandidate) {
  ASSERT_TRUE(base::CreateDirectory(core_path_));
  auto target = Create();
  ASSERT_FALSE(target.has_value());
  EXPECT_EQ(target.error(), Error::kTargetOccupied);
  EXPECT_TRUE(base::IsDirectoryEmpty(core_path_));
}

TEST_F(DormantBackupRestoreTargetTest,
       ExistingCoreBytesAreNeverOpenedOrChanged) {
  ASSERT_TRUE(base::CreateDirectory(core_path_));
  const auto path = core_path_.AppendASCII("core.sqlite3");
  ASSERT_TRUE(base::WriteFile(path, "not an archive or an empty database"));
  auto target = Create();
  ASSERT_FALSE(target.has_value());
  EXPECT_EQ(target.error(), Error::kTargetOccupied);
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents, "not an archive or an empty database");
}

TEST_F(DormantBackupRestoreTargetTest,
       PrivateAndMalformedIdentitiesWriteNothing) {
  auto private_target =
      DormantBackupRestoreTarget::Create(profile_path_, profile_id_, true);
  ASSERT_FALSE(private_target.has_value());
  EXPECT_EQ(private_target.error(), Error::kInvalidTarget);
  for (const auto* invalid : {"", "profile", "../../another-profile",
                              "00000000-0000-0000-0000-000000000000",
                              "AAAAAAAA-BBBB-4CCC-8DDD-EEEEEEEEEEEE"}) {
    auto target =
        DormantBackupRestoreTarget::Create(profile_path_, invalid, false);
    ASSERT_FALSE(target.has_value());
    EXPECT_EQ(target.error(), Error::kInvalidTarget);
    EXPECT_FALSE(base::PathExists(core_path_));
  }
}

TEST_F(DormantBackupRestoreTargetTest,
       InvalidPathsNeverCreateParentDirectories) {
  for (const auto& path :
       {base::FilePath(), base::FilePath(FILE_PATH_LITERAL("relative")),
        root_.GetPath().AppendASCII("missing"),
        profile_path_.AppendASCII("..").AppendASCII("Profile 1"),
        profile_path_.AppendASCII("."),
        base::FilePath(FILE_PATH_LITERAL("/"))}) {
    auto target = DormantBackupRestoreTarget::Create(path, profile_id_, false);
    ASSERT_FALSE(target.has_value());
    EXPECT_EQ(target.error(), Error::kInvalidTarget);
  }
  EXPECT_FALSE(base::PathExists(root_.GetPath().AppendASCII("missing")));
  EXPECT_FALSE(base::PathExists(core_path_));
}

TEST_F(DormantBackupRestoreTargetTest, SymbolicProfileAndAncestorAreRefused) {
  const auto profile_link = root_.GetPath().AppendASCII("profile-link");
  ASSERT_TRUE(base::CreateSymbolicLink(profile_path_, profile_link));
  auto direct =
      DormantBackupRestoreTarget::Create(profile_link, profile_id_, false);
  ASSERT_FALSE(direct.has_value());
  EXPECT_EQ(direct.error(), Error::kInvalidTarget);
  const auto ancestor_link = root_.GetPath().AppendASCII("ancestor-link");
  ASSERT_TRUE(base::CreateSymbolicLink(root_.GetPath(), ancestor_link));
  auto ancestor = DormantBackupRestoreTarget::Create(
      ancestor_link.AppendASCII("Profile 1"), profile_id_, false);
  ASSERT_FALSE(ancestor.has_value());
  EXPECT_EQ(ancestor.error(), Error::kInvalidTarget);
  EXPECT_FALSE(base::PathExists(core_path_));
}

TEST_F(DormantBackupRestoreTargetTest,
       DanglingCoreLinkIsOccupiedNotDisposable) {
  const auto destination = root_.GetPath().AppendASCII("unowned");
  ASSERT_TRUE(base::CreateSymbolicLink(destination, core_path_));
  auto target = Create();
  ASSERT_FALSE(target.has_value());
  EXPECT_EQ(target.error(), Error::kTargetOccupied);
  EXPECT_TRUE(base::IsLink(core_path_));
  EXPECT_FALSE(base::PathExists(destination));
}

TEST_F(DormantBackupRestoreTargetTest, LiveOwnerCannotBeReplaced) {
  auto first = Create();
  ASSERT_TRUE(first.has_value());
  auto second = DormantBackupRestoreTarget::Create(
      profile_path_, base::Uuid::GenerateRandomV4().AsLowercaseString(), false);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), Error::kTargetOccupied);
  EXPECT_EQ((*first)->profile_id(), profile_id_);
}

TEST_F(DormantBackupRestoreTargetTest, ReplacedFileIsNotTheHeldDatabase) {
  auto target = Create();
  ASSERT_TRUE(target.has_value());
  EXPECT_TRUE(
      DormantBackupRestoreTargetTestPeer::HasOwnedDatabasePath(**target));
  const auto path = (*target)->database_path();
  const auto retained = core_path_.AppendASCII("retained-original");
  ASSERT_TRUE(base::Move(path, retained));
  ASSERT_TRUE(base::WriteFile(path, "unowned replacement"));
  EXPECT_FALSE(
      DormantBackupRestoreTargetTestPeer::HasOwnedDatabasePath(**target));
  target->reset();
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents, "unowned replacement");
  EXPECT_TRUE(base::PathExists(retained));
}

}  // namespace
}  // namespace taffy::storage::backup
