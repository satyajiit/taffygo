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
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_extended_record_test_support.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
using ReconcileState = DormantBackupRestoreReconcileState;

class DormantBackupRestoreTargetExtendedProjectionTest : public testing::Test {
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
    database_path_ = target_->database_path();
  }

  bool StageExtended() {
    scenario_ = test::MakeEmptyRestoreScenario(profile_id_);
    if (!test::AppendExtendedRestoreRecords(&*scenario_)) {
      return false;
    }
    const auto payload_path = root_.GetPath().AppendASCII("plaintext");
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

  bool CommitExtended() {
    if (!StageExtended()) {
      return false;
    }
    auto prepared = target_->PrepareCommitWitness();
    if (!prepared.has_value()) {
      return false;
    }
    witness_ = std::move(*prepared);
    auto committed = target_->CommitAuthorized(
        scenario_->plan.Clone(),
        test::MakeCommitAuthorization(*scenario_->plan), witness_.Clone());
    return committed.has_value() &&
           *committed == DormantBackupRestoreCommitOutcome::kCommitted;
  }

  mojom::BackupRestoreTargetPtr ExactTarget() const {
    return mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile, profile_id_);
  }

  ReconcileState Reconcile() {
    auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
        profile_path_, ExactTarget());
    EXPECT_TRUE(reopened.has_value());
    if (!reopened.has_value()) {
      return ReconcileState::kOutcomeUnknown;
    }
    auto state = (*reopened)->Reconcile(witness_.Clone());
    EXPECT_TRUE(state.has_value());
    return state ? *state : ReconcileState::kOutcomeUnknown;
  }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  base::FilePath database_path_;
  std::string profile_id_;
  std::unique_ptr<DormantBackupRestoreTarget> target_;
  std::optional<test::DormantBackupRestoreScenario> scenario_;
  mojom::BackupRestoreCandidateWitnessPtr witness_;
};

TEST_F(DormantBackupRestoreTargetExtendedProjectionTest,
       AtomicCommitAndReadOnlyReconcileCoverAllExtendedFamilies) {
  ASSERT_TRUE(CommitExtended());
  ASSERT_TRUE(witness_);
  EXPECT_EQ(witness_->record_count, 4u);
  ASSERT_EQ(witness_->selection.size(), 3u);
  EXPECT_EQ(witness_->selection[0], mojom::BackupRecordKind::kSavedWorkspace);
  EXPECT_EQ(witness_->selection[1],
            mojom::BackupRecordKind::kUserAuthoredSkill);
  EXPECT_EQ(witness_->selection[2], mojom::BackupRecordKind::kLearnedProcedure);
  ASSERT_TRUE(target_->AbandonStage().has_value());
  target_.reset();

  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         sql::test::kTestTag);
  ASSERT_TRUE(database.Open(database_path_));
  auto records = ReadSelectedBackupRecords(
      &database, std::array{mojom::BackupRecordKind::kSavedWorkspace,
                            mojom::BackupRecordKind::kUserAuthoredSkill,
                            mojom::BackupRecordKind::kLearnedProcedure});
  ASSERT_TRUE(records.has_value());
  auto observed = target_internal::BuildCandidateWitness(*records);
  ASSERT_TRUE(observed);
  EXPECT_TRUE(target_internal::IsExactCandidateWitness(*witness_, *observed));

  // Scoped: sql::Database::Close() asserts that no sql::Statement it created is
  // still alive, and this one was not, so the test aborted on the DCHECK before
  // reaching its last assertion rather than reporting anything.
  size_t rows = 0u;
  {
    sql::Statement effects(database.GetUniqueStatement(
        "SELECT effect_id FROM core_workspace UNION ALL "
        "SELECT effect_id FROM core_skill_installation UNION ALL "
        "SELECT effect_id FROM core_skill_version"));
    while (effects.Step()) {
      EXPECT_TRUE(
          base::StartsWith(effects.ColumnString(0), "backup-restore-v1-"));
      EXPECT_EQ(effects.ColumnString(0).find("backup-stage"),
                std::string::npos);
      ++rows;
    }
    EXPECT_TRUE(effects.Succeeded());
  }
  EXPECT_EQ(rows, 6u);
  database.Close();
  EXPECT_EQ(Reconcile(), ReconcileState::kCommitted);
}

TEST_F(DormantBackupRestoreTargetExtendedProjectionTest,
       UnsavedWorkspaceOldSkillVersionAndRunHistoryAreNeverIgnored) {
  ASSERT_TRUE(CommitExtended());
  ASSERT_TRUE(target_->AbandonStage().has_value());
  target_.reset();

  sql::Database writer(sql::test::kTestTag);
  ASSERT_TRUE(writer.Open(database_path_));
  ASSERT_TRUE(writer.Execute(
      "INSERT INTO core_skill_version(skill_id,version,status,definition,"
      "step_count,effect_id,created_at_utc_ms) SELECT skill_id,1,2,definition,"
      "step_count,'unexpected-old-version',created_at_utc_ms FROM "
      "core_skill_version WHERE skill_id='compare-products'"));
  writer.Close();
  EXPECT_EQ(Reconcile(), ReconcileState::kOutcomeUnknown);

  ASSERT_TRUE(writer.Open(database_path_));
  ASSERT_TRUE(
      writer.Execute("DELETE FROM core_skill_version WHERE "
                     "effect_id='unexpected-old-version'"));
  ASSERT_TRUE(writer.Execute(
      "INSERT INTO core_skill_run(effect_id,skill_id,version,task_id,outcome,"
      "ran_at_utc_ms) VALUES('unexpected-run','compare-products',2,"
      "'historical-task',0,2000)"));
  writer.Close();
  EXPECT_EQ(Reconcile(), ReconcileState::kOutcomeUnknown);

  ASSERT_TRUE(writer.Open(database_path_));
  ASSERT_TRUE(writer.Execute("DELETE FROM core_skill_run"));
  auto unsaved =
      test::SavedWorkspace("33333333333333333333333333333333", 1u, false);
  {
    sql::Statement insert(writer.GetUniqueStatement(
        "INSERT INTO core_workspace(workspace_id,revision,snapshot,effect_id,"
        "expected_revision) VALUES(?,?,?,?,0)"));
    insert.BindString(0, unsaved->workspace_id);
    insert.BindInt64(1, static_cast<int64_t>(unsaved->revision));
    insert.BindBlob(2, unsaved->snapshot);
    insert.BindString(3, "unexpected-unsaved-workspace");
    ASSERT_TRUE(insert.Run());
  }
  writer.Close();
  EXPECT_EQ(Reconcile(), ReconcileState::kOutcomeUnknown);
}

}  // namespace
}  // namespace taffy::storage::backup
