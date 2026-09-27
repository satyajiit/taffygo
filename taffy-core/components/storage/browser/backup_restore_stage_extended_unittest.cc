// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_view_util.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_extended_record_test_support.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
void Append(test::DormantBackupRestoreScenario* scenario,
            mojom::BackupRecordKind kind,
            std::string stable_id,
            uint64_t revision,
            std::vector<uint8_t> plaintext,
            mojom::BackupRecordState state) {
  auto entry = mojom::BackupRestorePlanEntry::New();
  entry->kind = kind;
  entry->stable_id = std::move(stable_id);
  entry->archive_revision = revision;
  entry->action = state == mojom::BackupRecordState::kTombstone
                      ? mojom::BackupRestoreAction::kStageDeletion
                      : mojom::BackupRestoreAction::kStageCreate;
  entry->schema_version = 1u;
  entry->state = state;
  entry->plaintext_bytes = plaintext.size();
  if (state == mojom::BackupRecordState::kTombstone) {
    entry->plaintext_sha256.assign(32u, 0u);
  } else {
    const auto digest = crypto::hash::Sha256(plaintext);
    entry->plaintext_sha256.assign(digest.begin(), digest.end());
  }
  scenario->payload.insert(scenario->payload.end(), plaintext.begin(),
                           plaintext.end());
  scenario->plan->entries.push_back(std::move(entry));
}

class BackupRestoreStageExtendedTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(root_.CreateUniqueTempDir());
    stage_parent_ = root_.GetPath().AppendASCII("staging");
    ASSERT_TRUE(base::CreateDirectory(stage_parent_));
    profile_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  base::expected<std::unique_ptr<BackupRestoreStage>, BackupRestoreStageError>
  Build(test::DormantBackupRestoreScenario scenario) {
    const auto payload_path = root_.GetPath().AppendASCII("plaintext");
    if (!base::WriteFile(payload_path, scenario.payload)) {
      return base::unexpected(BackupRestoreStageError::kStorageUnavailable);
    }
    return BackupRestoreStage::Create(
        stage_parent_, *scenario.plan, scenario.plan->confirmation_sha256,
        base::File(payload_path,
                   base::File::FLAG_OPEN | base::File::FLAG_READ));
  }

  test::DormantBackupRestoreScenario ExtendedScenario() {
    auto scenario = test::MakeEmptyRestoreScenario(profile_id_);
    EXPECT_TRUE(test::AppendExtendedRestoreRecords(&scenario));
    return scenario;
  }

  base::ScopedTempDir root_;
  base::FilePath stage_parent_;
  std::string profile_id_;
};

TEST_F(BackupRestoreStageExtendedTest,
       TypedStageRoundTripsSixFamilyRowsWithoutRunOrSourceAuthority) {
  auto stage = Build(ExtendedScenario());
  ASSERT_TRUE(stage.has_value());
  auto records = (*stage)->ReadVerifiedRecords();
  ASSERT_TRUE(records.has_value());
  ASSERT_EQ(records->size(), 4u);
  auto skills = (*stage)->ReadVerifiedSkillRecords();
  ASSERT_TRUE(skills.has_value());
  ASSERT_EQ(skills->size(), 2u);
  EXPECT_EQ((*skills)[0]->skill_id, "capture-details");
  EXPECT_EQ((*skills)[0]->status, mojom::SkillStatus::kDisabled);
  EXPECT_EQ((*skills)[1]->skill_id, "compare-products");
  EXPECT_EQ((*skills)[1]->status, mojom::SkillStatus::kRetired);

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open((*stage)->database_path()));
  constexpr std::array expected_counts = {
      std::pair<std::string_view, int64_t>("core_workspace", 2),
      std::pair<std::string_view, int64_t>("core_skill_installation", 2),
      std::pair<std::string_view, int64_t>("core_skill_version", 2),
      std::pair<std::string_view, int64_t>("core_skill_run", 0),
  };
  for (const auto& [table, expected] : expected_counts) {
    sql::Statement count(database.GetUniqueStatement("SELECT COUNT(*) FROM " +
                                                     std::string(table)));
    ASSERT_TRUE(count.Step());
    EXPECT_EQ(count.ColumnInt64(0), expected) << table;
  }
  sql::Statement tombstone(database.GetUniqueStatement(
      "SELECT snapshot FROM core_workspace WHERE workspace_id=?"));
  tombstone.BindString(0, test::kExtendedDeletedWorkspaceId);
  ASSERT_TRUE(tombstone.Step());
  const std::vector<uint8_t> bytes = tombstone.ColumnBlobAsVector(0);
  EXPECT_TRUE(IsWorkspaceDeletionTombstone(
      bytes, test::kExtendedDeletedWorkspaceId, 3u));
  EXPECT_EQ(base::as_string_view(bytes).find(std::string(64u, 'a')),
            std::string_view::npos);
}

TEST_F(BackupRestoreStageExtendedTest,
       CrossKindSkillIdentityAndSkillTombstoneFailBeforeStageCreation) {
  auto duplicate = test::MakeEmptyRestoreScenario(profile_id_);
  for (const auto kind : {mojom::BackupRecordKind::kUserAuthoredSkill,
                          mojom::BackupRecordKind::kLearnedProcedure}) {
    auto encoded = EncodeSkillRecordV1(
        *test::CurrentProcedure("same-skill", 1u, kind), kind);
    ASSERT_TRUE(encoded.has_value());
    Append(&duplicate, kind, "same-skill", 1u, std::move(*encoded),
           mojom::BackupRecordState::kActive);
  }
  auto refused = Build(std::move(duplicate));
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), BackupRestoreStageError::kPlanRefused);

  auto tombstone = test::MakeEmptyRestoreScenario(profile_id_);
  Append(&tombstone, mojom::BackupRecordKind::kLearnedProcedure,
         "deleted-procedure", 2u, {}, mojom::BackupRecordState::kTombstone);
  refused = Build(std::move(tombstone));
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), BackupRestoreStageError::kUnsupportedRecord);
  EXPECT_TRUE(base::IsDirectoryEmpty(stage_parent_));
}

}  // namespace
}  // namespace taffy::storage::backup
