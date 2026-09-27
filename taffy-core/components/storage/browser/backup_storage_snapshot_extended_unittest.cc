// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_view_util.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_extended_record_test_support.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace test_support = test;
constexpr char kSavedId[] = "11111111111111111111111111111111";
constexpr char kUnsavedId[] = "22222222222222222222222222222222";
constexpr char kDeletedId[] = "33333333333333333333333333333333";

class BackupStorageSnapshotExtendedTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(database_.OpenInMemory());
    for (auto statement : storage_schema::kStatements) {
      ASSERT_TRUE(database_.Execute(statement));
    }
  }

  void InsertWorkspace(const mojom::WorkspaceRestoreRecord& record,
                       std::string_view effect_id) {
    sql::Statement insert(database_.GetUniqueStatement(
        "INSERT INTO core_workspace(workspace_id,revision,snapshot,effect_id,"
        "expected_revision) VALUES(?,?,?,?,0)"));
    insert.BindString(0, record.workspace_id);
    insert.BindInt64(1, static_cast<int64_t>(record.revision));
    insert.BindBlob(2, record.snapshot);
    insert.BindString(3, effect_id);
    ASSERT_TRUE(insert.Run());
  }

  void InsertSkill(const mojom::SkillRecord& record,
                   std::string_view effect_prefix) {
    sql::Statement installation(database_.GetUniqueStatement(
        "INSERT INTO core_skill_installation(skill_id,origin,provenance,"
        "active_version,effect_id,installed_at_utc_ms,updated_at_utc_ms) "
        "VALUES(?,?,?,?,?,?,?)"));
    installation.BindString(0, record.skill_id);
    installation.BindString(1, record.origin);
    installation.BindInt(2, static_cast<int>(record.provenance));
    installation.BindInt64(3, record.active_version);
    installation.BindString(4, std::string(effect_prefix) + "-installation");
    installation.BindInt64(5, record.installed_at_utc_ms);
    installation.BindInt64(6, record.updated_at_utc_ms);
    ASSERT_TRUE(installation.Run());

    sql::Statement version(database_.GetUniqueStatement(
        "INSERT INTO core_skill_version(skill_id,version,status,definition,"
        "step_count,effect_id,created_at_utc_ms) VALUES(?,?,?,?,?,?,?)"));
    version.BindString(0, record.skill_id);
    version.BindInt64(1, record.active_version);
    version.BindInt(2, static_cast<int>(record.status));
    version.BindBlob(3, record.definition);
    version.BindInt64(4, record.step_count);
    version.BindString(5, std::string(effect_prefix) + "-version");
    version.BindInt64(6, record.updated_at_utc_ms);
    ASSERT_TRUE(version.Run());
  }

  void InsertSupersededVersion(const mojom::SkillRecord& current) {
    sql::Statement version(database_.GetUniqueStatement(
        "INSERT INTO core_skill_version(skill_id,version,status,definition,"
        "step_count,effect_id,created_at_utc_ms) VALUES(?,?,?,?,?,?,?)"));
    version.BindString(0, current.skill_id);
    version.BindInt64(1, current.active_version - 1u);
    version.BindInt(2, static_cast<int>(mojom::SkillStatus::kSuperseded));
    version.BindBlob(3, current.definition);
    version.BindInt64(4, current.step_count);
    version.BindString(5, "superseded-effect-must-not-leave");
    version.BindInt64(6, current.installed_at_utc_ms);
    ASSERT_TRUE(version.Run());
  }

  sql::Database database_{sql::test::kTestTag};
};

TEST_F(BackupStorageSnapshotExtendedTest,
       SavedAndSanitizedDeletedWorkspacesAreTheOnlyWorkspaceRecords) {
  auto saved = test_support::SavedWorkspace(kSavedId, 7u);
  auto unsaved = test_support::SavedWorkspace(kUnsavedId, 4u, false);
  auto deleted = test_support::SavedWorkspace(kDeletedId, 2u);
  InsertWorkspace(*saved, "saved-effect-must-not-leave");
  InsertWorkspace(*unsaved, "unsaved-effect-must-not-leave");
  InsertWorkspace(*deleted, "deleted-before-effect");
  WorkspaceDeletionRequest deletion;
  deletion.effect_id = "deletion-effect-must-not-leave";
  deletion.workspace_id = kDeletedId;
  deletion.expected_revision = 2u;
  deletion.resulting_revision = 3u;
  deletion.confirmation_token.assign(64u, 'a');
  ASSERT_TRUE(CommitWorkspaceDeletion(&database_, deletion));

  auto result = ReadSelectedBackupRecords(
      &database_, std::array{mojom::BackupRecordKind::kSavedWorkspace});
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0].descriptor->stable_id, kSavedId);
  EXPECT_EQ((*result)[0].descriptor->state, mojom::BackupRecordState::kActive);
  EXPECT_TRUE(DecodeSavedWorkspaceRecordV1((*result)[0].plaintext, kSavedId, 7u)
                  .has_value());
  EXPECT_EQ((*result)[1].descriptor->stable_id, kDeletedId);
  EXPECT_EQ((*result)[1].descriptor->revision, 3u);
  EXPECT_EQ((*result)[1].descriptor->state,
            mojom::BackupRecordState::kTombstone);
  EXPECT_TRUE((*result)[1].plaintext.empty());
  for (const auto& record : *result) {
    const std::string_view plaintext = base::as_string_view(record.plaintext);
    EXPECT_EQ(plaintext.find("effect-must-not-leave"), std::string_view::npos);
    EXPECT_EQ(plaintext.find(std::string(64u, 'a')), std::string_view::npos);
  }
}

TEST_F(BackupStorageSnapshotExtendedTest,
       SkillsExportOnlyCurrentDefinitionAndNeverRunHistory) {
  auto authored = test_support::CurrentProcedure(
      "compare-products", 2u, mojom::BackupRecordKind::kUserAuthoredSkill,
      mojom::SkillStatus::kActive);
  auto learned = test_support::CurrentProcedure(
      "capture-details", 3u, mojom::BackupRecordKind::kLearnedProcedure,
      mojom::SkillStatus::kDisabled);
  InsertSkill(*authored, "authored-effect-must-not-leave");
  InsertSkill(*learned, "learned-effect-must-not-leave");
  InsertSupersededVersion(*authored);
  ASSERT_TRUE(database_.Execute(
      "INSERT INTO core_skill_run(skill_id,version,task_id,outcome,"
      "ran_at_utc_ms,effect_id) VALUES('compare-products',2,"
      "'task-history-must-not-leave',0,2000,'run-effect-must-not-leave')"));

  auto result = ReadSelectedBackupRecords(
      &database_, std::array{mojom::BackupRecordKind::kUserAuthoredSkill,
                             mojom::BackupRecordKind::kLearnedProcedure});
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0].descriptor->stable_id, "compare-products");
  EXPECT_EQ((*result)[0].descriptor->revision, 2u);
  EXPECT_EQ((*result)[1].descriptor->stable_id, "capture-details");
  EXPECT_EQ((*result)[1].descriptor->revision, 3u);
  for (const auto& record : *result) {
    const std::string_view plaintext = base::as_string_view(record.plaintext);
    EXPECT_EQ(plaintext.find("effect-must-not-leave"), std::string_view::npos);
    EXPECT_EQ(plaintext.find("task-history-must-not-leave"),
              std::string_view::npos);
    EXPECT_EQ(record.descriptor->state, mojom::BackupRecordState::kActive);
  }
  EXPECT_TRUE(DecodeSkillRecordV1((*result)[0].plaintext,
                                  mojom::BackupRecordKind::kUserAuthoredSkill,
                                  "compare-products", 2u)
                  .has_value());
  EXPECT_TRUE(DecodeSkillRecordV1((*result)[1].plaintext,
                                  mojom::BackupRecordKind::kLearnedProcedure,
                                  "capture-details", 3u)
                  .has_value());
}

TEST_F(BackupStorageSnapshotExtendedTest,
       MissingActiveDefinitionRefusesTheCompleteSelectedFamily) {
  ASSERT_TRUE(database_.Execute(
      "INSERT INTO core_skill_installation(skill_id,origin,provenance,"
      "active_version,effect_id,installed_at_utc_ms,updated_at_utc_ms) "
      "VALUES('missing-version','https://example.test',0,1,'effect',0,0)"));
  auto result = ReadSelectedBackupRecords(
      &database_, std::array{mojom::BackupRecordKind::kUserAuthoredSkill});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), BackupSnapshotError::kInvalidRecord);
}

TEST(BackupStorageSelectionExtendedTest,
     ExactSixKindAllowlistPassesValidationBeforeDatabaseOpen) {
  const auto result = ReadSelectedBackupRecords(
      nullptr, std::array{mojom::BackupRecordKind::kAssistantConfiguration,
                          mojom::BackupRecordKind::kSavedWorkspace,
                          mojom::BackupRecordKind::kLibraryEntry,
                          mojom::BackupRecordKind::kMemoryRecord,
                          mojom::BackupRecordKind::kUserAuthoredSkill,
                          mojom::BackupRecordKind::kLearnedProcedure});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), BackupSnapshotError::kUnavailable);
}

}  // namespace
}  // namespace taffy::storage::backup
