// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_storage_snapshot.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/string_view_util.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
constexpr char kId[] = "11111111111111111111111111111111";
constexpr auto kLibrary = mojom::BackupRecordKind::kLibraryEntry;
constexpr auto kMemory = mojom::BackupRecordKind::kMemoryRecord;
constexpr auto kConfiguration =
    mojom::BackupRecordKind::kAssistantConfiguration;

class BackupStorageSnapshotTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(database_.OpenInMemory());
    for (auto statement : storage_schema::kStatements) {
      ASSERT_TRUE(database_.Execute(statement));
    }
  }

  void SeedConfiguration() {
    ASSERT_TRUE(database_.Execute(
        "INSERT INTO core_assistant_configuration VALUES(1,7,3,1,0,1,2,"
        "'effect-id-must-not-be-exported')"));
  }

  void SeedMemory() {
    ASSERT_TRUE(database_.Execute(
        "INSERT INTO core_memory_state VALUES(1,1,'memory-global-effect',0)"));
    ASSERT_TRUE(database_.Execute(
        "INSERT INTO core_memory_record(memory_id,revision,statement,"
        "source_kind,scope_kind,sensitivity,created_at_epoch_ms,"
        "updated_at_epoch_ms,reviewed_at_epoch_ms,expires_at_epoch_ms,"
        "effect_id,expected_record_revision) VALUES("
        "'11111111111111111111111111111111',1,'Prefer short answers',"
        "0,0,0,1000,1000,0,0,'memory-effect-must-not-be-exported',0)"));
  }

  void SeedLibrary() {
    ASSERT_TRUE(
        database_.Execute("INSERT INTO core_library_state "
                          "VALUES(1,1,'library-global-effect',0)"));
    ASSERT_TRUE(database_.Execute(
        "INSERT INTO core_library_entry(entry_id,revision,collection_id,"
        "collection_name,source_workspace_id,source_workspace_revision,"
        "source_fact_id,field,original_value,kind,captured_at_epoch_ms,"
        "last_checked_epoch_ms,has_conflict,effect_id,expected_entry_revision)"
        " VALUES('11111111111111111111111111111111',1,"
        "'22222222222222222222222222222222','Research',"
        "'33333333333333333333333333333333',7,"
        "'44444444444444444444444444444444','warranty','two years',0,"
        "2000,1000,0,'library-effect-must-not-be-exported',0)"));
    ASSERT_TRUE(
        database_.Execute("INSERT INTO core_library_source VALUES("
                          "'11111111111111111111111111111111',"
                          "'55555555555555555555555555555555','Specifications',"
                          "'maker.example',1000)"));
  }

  sql::Database database_{sql::test::kTestTag};
};

TEST_F(BackupStorageSnapshotTest,
       SelectedFamiliesMatchTypedPayloadsAndExactDigests) {
  SeedConfiguration();
  SeedLibrary();
  SeedMemory();
  const auto result = ReadSelectedBackupRecords(
      &database_, std::array{kConfiguration, kLibrary, kMemory});
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  for (const auto& record : *result) {
    ASSERT_TRUE(record.descriptor);
    EXPECT_EQ(record.descriptor->state, mojom::BackupRecordState::kActive);
    EXPECT_EQ(record.descriptor->plaintext_bytes, record.plaintext.size());
    EXPECT_EQ(record.descriptor->schema_version, 1u);
    const auto digest = crypto::hash::Sha256(record.plaintext);
    EXPECT_TRUE(
        std::ranges::equal(record.descriptor->plaintext_sha256, digest));
    const std::string_view plaintext = base::as_string_view(record.plaintext);
    EXPECT_EQ(plaintext.find("effect"), std::string_view::npos);
  }
  EXPECT_TRUE(
      DecodeAssistantConfigurationV1((*result)[0].plaintext, 7).has_value());
  EXPECT_TRUE(
      DecodeLibraryRecordV1((*result)[1].plaintext, kId, 1).has_value());
  EXPECT_TRUE(DecodeMemoryRecordV1((*result)[2].plaintext, kId, 1).has_value());
}

TEST_F(BackupStorageSnapshotTest, UnselectedFamiliesAreNeverRead) {
  SeedConfiguration();
  ASSERT_TRUE(database_.Execute("DROP TABLE core_library_entry"));
  ASSERT_TRUE(database_.Execute("DROP TABLE core_memory_record"));
  ASSERT_TRUE(database_.Execute("DROP TABLE core_account_session"));

  const auto result =
      ReadSelectedBackupRecords(&database_, std::array{kConfiguration});
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_TRUE(
      DecodeAssistantConfigurationV1(result->front().plaintext, 7).has_value());
}

TEST_F(BackupStorageSnapshotTest, TombstonesAreZeroByteZeroDigestDescriptors) {
  ASSERT_TRUE(database_.Execute(
      "INSERT INTO core_library_tombstone VALUES("
      "'11111111111111111111111111111111',3,1000,'library-delete',2)"));
  ASSERT_TRUE(database_.Execute(
      "INSERT INTO core_memory_tombstone VALUES("
      "'11111111111111111111111111111111',5,1000,'memory-delete',4)"));

  const auto result =
      ReadSelectedBackupRecords(&database_, std::array{kLibrary, kMemory});
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0].descriptor->revision, 3u);
  EXPECT_EQ((*result)[1].descriptor->revision, 5u);
  for (const auto& record : *result) {
    EXPECT_EQ(record.descriptor->stable_id, kId);
    EXPECT_EQ(record.descriptor->state, mojom::BackupRecordState::kTombstone);
    EXPECT_EQ(record.descriptor->plaintext_bytes, 0u);
    EXPECT_TRUE(record.plaintext.empty());
    ASSERT_EQ(record.descriptor->plaintext_sha256.size(), 32u);
    EXPECT_TRUE(std::ranges::all_of(record.descriptor->plaintext_sha256,
                                    [](uint8_t byte) { return byte == 0; }));
  }
}

TEST_F(BackupStorageSnapshotTest,
       AnActiveRowAndItsTombstoneCannotBothBeExported) {
  SeedMemory();
  ASSERT_TRUE(database_.Execute(
      "INSERT INTO core_memory_tombstone VALUES("
      "'11111111111111111111111111111111',3,1000,'memory-delete',2)"));
  const auto result =
      ReadSelectedBackupRecords(&database_, std::array{kMemory});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), BackupSnapshotError::kInvalidRecord);
}

TEST_F(BackupStorageSnapshotTest,
       InvalidStoredIdentityRefusesTheCompleteSnapshot) {
  SeedConfiguration();
  SeedMemory();
  ASSERT_TRUE(
      database_.Execute("UPDATE core_memory_record SET "
                        "memory_id='XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX'"));
  const auto result = ReadSelectedBackupRecords(
      &database_, std::array{kConfiguration, kMemory});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), BackupSnapshotError::kInvalidRecord);
  EXPECT_EQ(database_.transaction_nesting(), 0);
}

TEST_F(BackupStorageSnapshotTest,
       EmptySelectedStoresDoNotInventDefaultOrDeletedRows) {
  const auto result = ReadSelectedBackupRecords(
      &database_, std::array{kConfiguration, kLibrary, kMemory});
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST(BackupStorageSelectionTest,
     UnsupportedOrDuplicateSelectionRefusesBeforeDatabaseAccess) {
  for (auto kind : {mojom::BackupRecordKind::kBookmark,
                    mojom::BackupRecordKind::kBrowserPreference,
                    static_cast<mojom::BackupRecordKind>(255)}) {
    const auto result =
        ReadSelectedBackupRecords(nullptr, std::array{kConfiguration, kind});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BackupSnapshotError::kUnsupportedSelection);
  }
  EXPECT_EQ(ReadSelectedBackupRecords(nullptr, {}).error(),
            BackupSnapshotError::kUnsupportedSelection);
  EXPECT_EQ(
      ReadSelectedBackupRecords(nullptr, std::array{kMemory, kMemory}).error(),
      BackupSnapshotError::kUnsupportedSelection);
}

TEST(BackupStorageSelectionTest,
     PrivateBrokerCannotCreateOrReadABackupDatabase) {
  base::test::TaskEnvironment environment;
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("core.sqlite3");
  CoreStorageBroker broker(path, true);
  base::RunLoop loop;
  std::optional<BackupSnapshotResult> result;
  broker.ReadBackupSnapshot(
      {kMemory}, base::BindLambdaForTesting([&](BackupSnapshotResult value) {
        result.emplace(std::move(value));
        loop.Quit();
      }));
  loop.Run();
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_EQ(result->error(), BackupSnapshotError::kUnavailable);
  EXPECT_FALSE(base::PathExists(path));
}

}  // namespace
}  // namespace taffy::storage::backup
