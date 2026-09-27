// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
using CommitError = DormantBackupRestoreCommitError;
using Outcome = DormantBackupRestoreCommitOutcome;

class DormantBackupRestoreTargetCommitTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(root_.CreateUniqueTempDir());
    profile_path_ = root_.GetPath().AppendASCII("Profile 1");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
    profile_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto created =
        DormantBackupRestoreTarget::Create(profile_path_, profile_id_, false);
    ASSERT_TRUE(created.has_value());
    target_ = std::move(*created);
  }

  bool Stage(test::DormantBackupRestoreScenario scenario) {
    scenario_ = std::move(scenario);
    const auto payload_path = root_.GetPath().AppendASCII(
        "plaintext-" + std::to_string(++payload_ordinal_));
    if (!base::WriteFile(payload_path, scenario_->payload)) {
      return false;
    }
    return target_
        ->StageAuthorized(scenario_->plan.Clone(),
                          test::MakeStageAuthorization(*scenario_->plan),
                          base::File(payload_path, base::File::FLAG_OPEN |
                                                       base::File::FLAG_READ))
        .has_value();
  }

  bool StageMixed() {
    auto scenario = test::MakeMixedRestoreScenario(profile_id_);
    return scenario && Stage(std::move(*scenario));
  }

  auto Commit(const mojom::BackupRestoreCandidateWitness& witness) {
    return target_->CommitAuthorized(
        scenario_->plan.Clone(),
        test::MakeCommitAuthorization(*scenario_->plan), witness.Clone());
  }

  static int64_t Count(sql::Database* database, std::string_view table) {
    sql::Statement count(database->GetUniqueStatement("SELECT COUNT(*) FROM " +
                                                      std::string(table)));
    EXPECT_TRUE(count.Step());
    return count.ColumnInt64(0);
  }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  std::string profile_id_;
  std::unique_ptr<DormantBackupRestoreTarget> target_;
  std::optional<test::DormantBackupRestoreScenario> scenario_;
  uint64_t payload_ordinal_ = 0u;
};

TEST_F(DormantBackupRestoreTargetCommitTest,
       PreparedExactAuthorityAtomicallyProjectsOnlyTypedRecords) {
  ASSERT_TRUE(StageMixed());
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  EXPECT_EQ((*witness)->record_count, 5u);
  ASSERT_EQ((*witness)->selection.size(), 3u);
  auto repeated_witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(repeated_witness.has_value());
  EXPECT_TRUE(
      target_internal::IsExactCandidateWitness(**witness, **repeated_witness));

  auto committed = Commit(**witness);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, Outcome::kCommitted);
  EXPECT_TRUE(target_->AbandonStage().has_value());
  const auto database_path = target_->database_path();
  target_.reset();

  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         sql::test::kTestTag);
  ASSERT_TRUE(database.Open(database_path));
  EXPECT_EQ(Count(&database, "core_assistant_configuration"), 1);
  EXPECT_EQ(Count(&database, "core_library_entry"), 1);
  EXPECT_EQ(Count(&database, "core_library_source"), 1);
  EXPECT_EQ(Count(&database, "core_library_tombstone"), 1);
  EXPECT_EQ(Count(&database, "core_memory_record"), 1);
  EXPECT_EQ(Count(&database, "core_memory_tombstone"), 1);
  EXPECT_EQ(Count(&database, "core_library_state"), 1);
  EXPECT_EQ(Count(&database, "core_memory_state"), 1);
  for (const auto* table : {"core_account_session", "core_effect_journal",
                            "core_task_aggregate", "core_sync_binding"}) {
    EXPECT_EQ(Count(&database, table), 0) << table;
  }
  auto projected = ReadSelectedBackupRecords(
      &database, std::array{mojom::BackupRecordKind::kAssistantConfiguration,
                            mojom::BackupRecordKind::kLibraryEntry,
                            mojom::BackupRecordKind::kMemoryRecord});
  ASSERT_TRUE(projected.has_value());
  auto observed = target_internal::BuildCandidateWitness(*projected);
  ASSERT_TRUE(observed);
  EXPECT_TRUE(target_internal::IsExactCandidateWitness(**witness, *observed));

  sql::Statement effects(database.GetUniqueStatement(
      "SELECT effect_id FROM core_assistant_configuration UNION ALL "
      "SELECT effect_id FROM core_library_entry UNION ALL "
      "SELECT effect_id FROM core_library_tombstone UNION ALL "
      "SELECT effect_id FROM core_memory_record UNION ALL "
      "SELECT effect_id FROM core_memory_tombstone UNION ALL "
      "SELECT effect_id FROM core_library_state UNION ALL "
      "SELECT effect_id FROM core_memory_state"));
  size_t effect_count = 0u;
  while (effects.Step()) {
    EXPECT_TRUE(
        base::StartsWith(effects.ColumnString(0), "backup-restore-v1-"));
    EXPECT_EQ(effects.ColumnString(0).find("stage"), std::string::npos);
    ++effect_count;
  }
  EXPECT_TRUE(effects.Succeeded());
  EXPECT_EQ(effect_count, 7u);
}

TEST_F(DormantBackupRestoreTargetCommitTest,
       InvalidAuthorityAndWitnessAreSafeRefusalsBeforeConsumption) {
  ASSERT_TRUE(StageMixed());
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());

  auto wrong_generation = test::MakeCommitAuthorization(*scenario_->plan);
  wrong_generation->decision_operation->service_generation += 1u;
  auto refused = target_->CommitAuthorized(
      scenario_->plan.Clone(), std::move(wrong_generation), (*witness).Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kInvalidAuthorization);

  auto wrong_target = test::MakeCommitAuthorization(*scenario_->plan);
  wrong_target->binding->target->profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  refused = target_->CommitAuthorized(
      scenario_->plan.Clone(), std::move(wrong_target), (*witness).Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kInvalidAuthorization);

  auto wrong_snapshot = test::MakeCommitAuthorization(*scenario_->plan);
  wrong_snapshot->binding->snapshot_sha256[0] ^= 1u;
  refused = target_->CommitAuthorized(
      scenario_->plan.Clone(), std::move(wrong_snapshot), (*witness).Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kInvalidAuthorization);

  auto wrong_source = test::MakeCommitAuthorization(*scenario_->plan);
  wrong_source->binding->owner_profile_id = "another-source";
  refused = target_->CommitAuthorized(
      scenario_->plan.Clone(), std::move(wrong_source), (*witness).Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kInvalidAuthorization);

  auto spliced_plan = scenario_->plan.Clone();
  spliced_plan->backup_id = "another-backup";
  refused = target_->CommitAuthorized(
      std::move(spliced_plan), test::MakeCommitAuthorization(*scenario_->plan),
      (*witness).Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kInvalidAuthorization);

  auto wrong_witness = (*witness).Clone();
  wrong_witness->candidate_records_sha256[0] ^= 1u;
  refused = target_->CommitAuthorized(
      scenario_->plan.Clone(), test::MakeCommitAuthorization(*scenario_->plan),
      std::move(wrong_witness));
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kWitnessMismatch);

  refused = target_->CommitAuthorized(
      scenario_->plan.Clone(),
      test::MakeCommitAuthorization(*scenario_->plan, 1u), (*witness).Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kInvalidAuthorization);
  auto committed = Commit(**witness);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, Outcome::kCommitted);
}

TEST_F(DormantBackupRestoreTargetCommitTest,
       PreparationIsRequiredAndConsumedAuthorityCannotReplay) {
  ASSERT_TRUE(StageMixed());
  auto placeholder = mojom::BackupRestoreCandidateWitness::New();
  placeholder->selection = {mojom::BackupRecordKind::kAssistantConfiguration};
  placeholder->record_count = 1u;
  placeholder->candidate_records_sha256.assign(32u, 7u);
  auto early = Commit(*placeholder);
  ASSERT_FALSE(early.has_value());
  EXPECT_EQ(early.error(), CommitError::kCommitNotPrepared);

  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  auto committed = Commit(**witness);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, Outcome::kCommitted);
  auto duplicate = Commit(**witness);
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), CommitError::kAuthorizationConsumed);
}

TEST_F(DormantBackupRestoreTargetCommitTest,
       NonPristineTargetRefusesBeforeAuthorityIsSpent) {
  ASSERT_TRUE(StageMixed());
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  ASSERT_TRUE(DormantBackupRestoreCommitTestPeer::ExecuteTargetSql(
      target_.get(),
      "INSERT INTO core_library_state(singleton,revision,effect_id,"
      "expected_revision) VALUES(1,1,'foreign',0)"));

  auto refused = Commit(**witness);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kTargetNotPristine);
  ASSERT_TRUE(DormantBackupRestoreCommitTestPeer::ExecuteTargetSql(
      target_.get(), "DELETE FROM core_library_state"));
  auto committed = Commit(**witness);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, Outcome::kCommitted);
}

TEST_F(DormantBackupRestoreTargetCommitTest,
       StageMutationAfterPreparationRefusesBeforeAuthorityIsSpent) {
  ASSERT_TRUE(StageMixed());
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  const auto stage_path =
      DormantBackupRestoreCommitTestPeer::StageDatabasePath(*target_);
  sql::Database stage(sql::test::kTestTag);
  ASSERT_TRUE(stage.Open(stage_path));
  ASSERT_TRUE(stage.Execute(
      "UPDATE core_memory_tombstone SET revision=6 WHERE memory_id='"
      "44444444444444444444444444444444'"));
  stage.Close();

  auto refused = Commit(**witness);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kWitnessMismatch);
  ASSERT_TRUE(stage.Open(stage_path));
  ASSERT_TRUE(stage.Execute(
      "UPDATE core_memory_tombstone SET revision=5 WHERE memory_id='"
      "44444444444444444444444444444444'"));
  stage.Close();
  auto committed = Commit(**witness);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, Outcome::kCommitted);
}

// The rolled-back-and-unknown branch of CommitAuthorized has no test, and this
// one used to claim it did. It installed a `RAISE(ABORT)` trigger, which Chrome
// disables and whose result code sql/sqlite_result_code.cc maps to "should
// never show up in Chrome", so the process died on a DCHECK instead of the
// transaction failing — the test had never produced a result on any host.
//
// Nothing else reaches that branch either. The commit re-reads the entire
// schema against a freshly built head and requires a pristine target first, so
// every SQL-level way to make its transaction fail — a moved table, a dropped
// column, an added index, a colliding row — is caught by a guard before the
// transaction opens. Reaching it needs I/O error injection beneath SQLite,
// which this build does not expose. What is left to assert, and what this test
// now asserts, is that refusal and that it costs the authorization nothing.
TEST_F(DormantBackupRestoreTargetCommitTest,
       MovedTargetSchemaIsRefusedBeforeTheAuthorizationIsSpent) {
  ASSERT_TRUE(StageMixed());
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  ASSERT_TRUE(DormantBackupRestoreCommitTestPeer::ExecuteTargetSql(
      target_.get(),
      "ALTER TABLE core_library_entry RENAME TO core_library_entry_absent"));

  auto refused = Commit(**witness);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CommitError::kTargetChanged);
  for (const auto* table :
       {"core_assistant_configuration", "core_library_source",
        "core_library_tombstone", "core_library_state", "core_memory_record",
        "core_memory_tombstone", "core_memory_state"}) {
    auto count = DormantBackupRestoreCommitTestPeer::CountTargetRows(
        target_.get(), table);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 0) << table;
  }
  // Renaming the table back does not restore the target: the guard compares the
  // schema SQL that SQLite stores, and a rename rewrites that text rather than
  // reproducing it. A moved table is therefore terminal for this target, which
  // is the right answer and not one this test had to assume.
  ASSERT_TRUE(DormantBackupRestoreCommitTestPeer::ExecuteTargetSql(
      target_.get(),
      "ALTER TABLE core_library_entry_absent RENAME TO core_library_entry"));
  auto still_refused = Commit(**witness);
  ASSERT_FALSE(still_refused.has_value());
  EXPECT_EQ(still_refused.error(), CommitError::kTargetChanged);
}
TEST_F(DormantBackupRestoreTargetCommitTest,
       PathReplacementAfterSqlCommitIsUnknownAndNeverReplayable) {
  ASSERT_TRUE(StageMixed());
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  const auto database_path = target_->database_path();
  const auto retained = database_path.DirName().AppendASCII("retained.sqlite3");
  // base::BindOnce refuses a capturing lambda, so the two paths are bound
  // arguments of a captureless one instead.
  DormantBackupRestoreCommitTestPeer::SetAfterCommit(
      target_.get(),
      base::BindOnce(
          [](const base::FilePath& database_path,
             const base::FilePath& retained) {
            EXPECT_TRUE(base::Move(database_path, retained));
            EXPECT_TRUE(base::WriteFile(database_path, "unowned replacement"));
          },
          database_path, retained));

  auto result = Commit(**witness);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, Outcome::kOutcomeUnknown);
  auto duplicate = Commit(**witness);
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), CommitError::kAuthorizationConsumed);
  target_.reset();
  sql::Database committed(sql::DatabaseOptions().set_read_only(true),
                          sql::test::kTestTag);
  ASSERT_TRUE(committed.Open(retained));
  EXPECT_EQ(Count(&committed, "core_assistant_configuration"), 1);
  std::string replacement;
  ASSERT_TRUE(base::ReadFileToString(database_path, &replacement));
  EXPECT_EQ(replacement, "unowned replacement");
}

TEST_F(DormantBackupRestoreTargetCommitTest,
       EmptyCandidateIsAnExplicitCommittedNoOp) {
  ASSERT_TRUE(Stage(test::MakeEmptyRestoreScenario(profile_id_)));
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  EXPECT_TRUE((*witness)->selection.empty());
  EXPECT_EQ((*witness)->record_count, 0u);
  auto committed = Commit(**witness);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, Outcome::kCommitted);
}

}  // namespace
}  // namespace taffy::storage::backup
