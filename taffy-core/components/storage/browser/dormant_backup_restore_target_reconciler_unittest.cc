// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
using Error = DormantBackupRestoreReconcileError;
using State = DormantBackupRestoreReconcileState;

class DormantBackupRestoreTargetReconcilerTest : public testing::Test {
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

  mojom::BackupRestoreTargetPtr ExactTarget() const {
    return mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile, profile_id_);
  }

  bool Stage(test::DormantBackupRestoreScenario scenario) {
    scenario_ = std::move(scenario);
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

  auto Open() {
    target_.reset();
    return DormantBackupRestoreTargetReconciler::OpenForRecovery(profile_path_,
                                                                 ExactTarget());
  }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  base::FilePath database_path_;
  std::string profile_id_;
  std::unique_ptr<DormantBackupRestoreTarget> target_;
  std::optional<test::DormantBackupRestoreScenario> scenario_;
};

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       PreparedButUncommittedCandidateIsPristineAfterRestart) {
  auto scenario = test::MakeMixedRestoreScenario(profile_id_);
  ASSERT_TRUE(scenario);
  ASSERT_TRUE(Stage(std::move(*scenario)));
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  ASSERT_TRUE(target_->AbandonStage().has_value());

  auto reopened = Open();
  ASSERT_TRUE(reopened.has_value());
  auto state = (*reopened)->Reconcile((*witness).Clone());
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kPristine);
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       MutableOwnerExclusivelyHoldsPhysicalCustodyUntilDestruction) {
  auto busy = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_FALSE(busy.has_value());
  EXPECT_EQ(busy.error(), Error::kTargetBusy);

  target_.reset();
  auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  EXPECT_TRUE(reopened.has_value());
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       ExactCommittedProjectionReconcilesWithoutPlanOrAuthority) {
  auto scenario = test::MakeMixedRestoreScenario(profile_id_);
  ASSERT_TRUE(scenario);
  ASSERT_TRUE(Stage(std::move(*scenario)));
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  auto committed = target_->CommitAuthorized(
      scenario_->plan.Clone(), test::MakeCommitAuthorization(*scenario_->plan),
      (*witness).Clone());
  ASSERT_TRUE(committed.has_value());
  ASSERT_EQ(*committed, DormantBackupRestoreCommitOutcome::kCommitted);

  auto reopened = Open();
  ASSERT_TRUE(reopened.has_value());
  auto state = (*reopened)->Reconcile((*witness).Clone());
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kCommitted);
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       ExactEmptyProjectionIsCommittedBeforePristineClassification) {
  auto witness = target_internal::BuildCandidateWitness({});
  ASSERT_TRUE(witness);

  auto reopened = Open();
  ASSERT_TRUE(reopened.has_value());
  auto state = (*reopened)->Reconcile(witness.Clone());
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kCommitted);
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       WrongWitnessAndTamperedMetadataRemainOutcomeUnknown) {
  auto scenario = test::MakeMixedRestoreScenario(profile_id_);
  ASSERT_TRUE(scenario);
  ASSERT_TRUE(Stage(std::move(*scenario)));
  auto witness = target_->PrepareCommitWitness();
  ASSERT_TRUE(witness.has_value());
  auto committed = target_->CommitAuthorized(
      scenario_->plan.Clone(), test::MakeCommitAuthorization(*scenario_->plan),
      (*witness).Clone());
  ASSERT_TRUE(committed.has_value());
  ASSERT_EQ(*committed, DormantBackupRestoreCommitOutcome::kCommitted);
  target_.reset();

  auto wrong = (*witness).Clone();
  wrong->candidate_records_sha256[0] ^= 1u;
  auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_TRUE(reopened.has_value());
  auto state = (*reopened)->Reconcile(std::move(wrong));
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kOutcomeUnknown);
  reopened->reset();

  sql::Database writer(sql::test::kTestTag);
  ASSERT_TRUE(writer.Open(database_path_));
  ASSERT_TRUE(
      writer.Execute("UPDATE core_memory_record SET effect_id='tampered'"));
  writer.Close();
  reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_TRUE(reopened.has_value());
  state = (*reopened)->Reconcile((*witness).Clone());
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kOutcomeUnknown);
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       RowsInForbiddenAuthorityTableAreNeverAnEmptyCommit) {
  auto witness = target_internal::BuildCandidateWitness({});
  ASSERT_TRUE(witness);
  target_.reset();
  sql::Database writer(sql::test::kTestTag);
  ASSERT_TRUE(writer.Open(database_path_));
  ASSERT_TRUE(writer.Execute(
      "INSERT INTO core_account_session(singleton,session_handle,"
      "account_subject,expires_at_utc_ms,rotation,auth_method,email,"
      "display_name) VALUES(1,'session','subject',1,1,0,NULL,NULL)"));
  writer.Close();

  auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_TRUE(reopened.has_value());
  auto state = (*reopened)->Reconcile(std::move(witness));
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kOutcomeUnknown);
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       NonHeadSchemaIsRefusedWithoutMigrationOrRaze) {
  target_.reset();
  sql::Database writer(sql::test::kTestTag);
  ASSERT_TRUE(writer.Open(database_path_));
  ASSERT_TRUE(writer.Execute("UPDATE taffy_storage_schema SET version=999"));
  writer.Close();

  auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_FALSE(reopened.has_value());
  EXPECT_EQ(reopened.error(), Error::kSchemaMismatch);
  ASSERT_TRUE(writer.Open(database_path_));
  sql::Statement version(
      writer.GetUniqueStatement("SELECT version FROM taffy_storage_schema"));
  ASSERT_TRUE(version.Step());
  EXPECT_EQ(version.ColumnInt(0), 999);
  EXPECT_TRUE(base::PathExists(database_path_));
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       ReplacedHeldDatabaseIsTypedCustodyFailureWithoutOpeningReplacement) {
  auto witness = target_internal::BuildCandidateWitness({});
  ASSERT_TRUE(witness);
  auto reopened = Open();
  ASSERT_TRUE(reopened.has_value());
  const auto retained =
      database_path_.DirName().AppendASCII("retained-core.sqlite3");
  ASSERT_TRUE(base::Move(database_path_, retained));
  ASSERT_TRUE(base::WriteFile(database_path_, "unowned replacement"));

  auto state = (*reopened)->Reconcile(std::move(witness));
  ASSERT_FALSE(state.has_value());
  EXPECT_EQ(state.error(), Error::kTargetChanged);
  std::string replacement;
  ASSERT_TRUE(base::ReadFileToString(database_path_, &replacement));
  EXPECT_EQ(replacement, "unowned replacement");
  EXPECT_TRUE(base::PathExists(retained));
}

// This test used to assert that a second connection could move the schema
// version under an open reconciler, and then that Reconcile answered
// kSchemaMismatch. Its first half is impossible: sql::DatabaseOptions defaults
// exclusive locking on, so the reconciler's own read-only connection holds the
// file for its lifetime and the writer below is refused. The test therefore
// asserted a write that never happened and had never passed anywhere.
//
// What it establishes now is the half that is real — the custody that makes
// the other half unreachable, and a reconcile that stays typed across it.
// Reconcile does re-read the schema and does answer kSchemaMismatch rather
// than kOutcomeUnknown, and that branch is reachable only by replacing the
// file, which ReplacedHeldDatabaseIsTypedCustodyFailureWithoutOpeningReplacement
// covers, or by a non-head schema at open, which
// NonHeadSchemaIsRefusedWithoutMigrationOrRaze covers.
TEST_F(DormantBackupRestoreTargetReconcilerTest,
       SchemaCannotMoveUnderAnOpenReconcilerAndReconcileStaysTyped) {
  auto witness = target_internal::BuildCandidateWitness({});
  ASSERT_TRUE(witness);
  auto reopened = Open();
  ASSERT_TRUE(reopened.has_value());
  sql::Database writer(sql::test::kTestTag);
  ASSERT_TRUE(writer.Open(database_path_));
  EXPECT_FALSE(writer.Execute("UPDATE taffy_storage_schema SET version=999"));
  writer.Close();

  auto state = (*reopened)->Reconcile(std::move(witness));
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, State::kCommitted);
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       WrongIdentityAndLinkedReservationAreRefusedReadOnly) {
  target_.reset();
  auto wrong_target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile,
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  auto wrong = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, std::move(wrong_target));
  ASSERT_FALSE(wrong.has_value());
  EXPECT_EQ(wrong.error(), Error::kSchemaMismatch);

  const auto link = root_.GetPath().AppendASCII("linked-profile");
  ASSERT_TRUE(base::CreateSymbolicLink(profile_path_, link));
  auto linked = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      link, ExactTarget());
  ASSERT_FALSE(linked.has_value());
  EXPECT_EQ(linked.error(), Error::kInvalidTarget);
  EXPECT_TRUE(base::PathExists(database_path_));
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       UnownedSqliteSidecarIsRefusedAndNeverRemoved) {
  target_.reset();
  const base::FilePath foreign = root_.GetPath().AppendASCII("foreign-wal");
  ASSERT_TRUE(base::WriteFile(foreign, "keep"));
  const base::FilePath sidecar(database_path_.value() +
                               FILE_PATH_LITERAL("-wal"));
  ASSERT_TRUE(base::CreateSymbolicLink(foreign, sidecar));

  auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_FALSE(reopened.has_value());
  EXPECT_EQ(reopened.error(), Error::kTargetChanged);
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(foreign, &contents));
  EXPECT_EQ(contents, "keep");
  EXPECT_TRUE(base::IsLink(sidecar));
}

TEST_F(DormantBackupRestoreTargetReconcilerTest,
       MalformedWitnessIsAClosedInputError) {
  auto invalid = mojom::BackupRestoreCandidateWitness::New();
  invalid->selection = {mojom::BackupRecordKind::kLibraryEntry};
  invalid->record_count = 0u;
  invalid->candidate_records_sha256.assign(32u, 9u);
  auto reopened = Open();
  ASSERT_TRUE(reopened.has_value());

  auto state = (*reopened)->Reconcile(std::move(invalid));
  ASSERT_FALSE(state.has_value());
  EXPECT_EQ(state.error(), Error::kInvalidWitness);
}

}  // namespace
}  // namespace taffy::storage::backup
