// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/uuid.h"
#include "sql/database.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace target_internal = restore_target_internal;
using Error = DormantBackupRestoreResolutionError;

class DormantBackupRestoreTargetResolutionTest : public testing::Test {
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

  mojom::BackupRestoreTargetPtr ExactTarget() const {
    return mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile, profile_id_);
  }

  mojom::BackupRestoreCandidateWitnessPtr CommitAndRelease() {
    scenario_ = test::MakeMixedRestoreScenario(profile_id_);
    if (!scenario_) {
      return nullptr;
    }
    const base::FilePath payload_path =
        root_.GetPath().AppendASCII("plaintext");
    if (!base::WriteFile(payload_path, scenario_->payload)) {
      return nullptr;
    }
    auto staged = target_->StageAuthorized(
        scenario_->plan.Clone(), test::MakeStageAuthorization(*scenario_->plan),
        base::File(payload_path,
                   base::File::FLAG_OPEN | base::File::FLAG_READ));
    if (!staged.has_value()) {
      return nullptr;
    }
    auto witness = target_->PrepareCommitWitness();
    if (!witness.has_value()) {
      return nullptr;
    }
    stage_database_path_ =
        DormantBackupRestoreCommitTestPeer::StageDatabasePath(*target_);
    auto committed = target_->CommitAuthorized(
        scenario_->plan.Clone(),
        test::MakeCommitAuthorization(*scenario_->plan), (*witness).Clone());
    if (!committed.has_value() ||
        *committed != DormantBackupRestoreCommitOutcome::kCommitted) {
      return nullptr;
    }
    target_.reset();
    return std::move(*witness);
  }

  base::FilePath StagingParent() const {
    return profile_path_.AppendASCII("TaffyCore")
        .AppendASCII("TaffyRestoreStaging");
  }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  base::FilePath stage_database_path_;
  std::string profile_id_;
  std::unique_ptr<DormantBackupRestoreTarget> target_;
  std::optional<test::DormantBackupRestoreScenario> scenario_;
};

TEST_F(DormantBackupRestoreTargetResolutionTest,
       AcceptFinalizerDeletesOnlyVerifiedStageAndRetainsCandidate) {
  auto witness = CommitAndRelease();
  ASSERT_TRUE(witness);
  ASSERT_TRUE(base::PathExists(stage_database_path_));

  auto finalizer = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), witness.Clone());
  ASSERT_TRUE(finalizer.has_value())
      << "finalizer refused with error " << static_cast<int>(finalizer.error());
  auto busy = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_FALSE(busy.has_value());
  EXPECT_EQ(busy.error(), DormantBackupRestoreReconcileError::kTargetBusy);

  EXPECT_TRUE((*finalizer)->FinalizeForPublication().has_value());
  EXPECT_FALSE(base::PathExists(StagingParent()));
  EXPECT_TRUE(base::PathExists(
      profile_path_.AppendASCII("TaffyCore").AppendASCII("core.sqlite3")));
  finalizer->reset();

  auto reopened = DormantBackupRestoreTargetReconciler::OpenForRecovery(
      profile_path_, ExactTarget());
  ASSERT_TRUE(reopened.has_value());
  auto state = (*reopened)->Reconcile(witness.Clone());
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, DormantBackupRestoreReconcileState::kCommitted);
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       WrongWitnessOrUnexpectedStageEntryRefusesWithoutCleanup) {
  auto witness = CommitAndRelease();
  ASSERT_TRUE(witness);
  auto wrong = witness.Clone();
  wrong->candidate_records_sha256[0] ^= 1u;
  auto refused = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), std::move(wrong));
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), Error::kCandidateMismatch);
  EXPECT_TRUE(base::PathExists(stage_database_path_));

  const base::FilePath foreign = StagingParent().AppendASCII("foreign");
  ASSERT_TRUE(base::WriteFile(foreign, "keep"));
  refused = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), witness.Clone());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), Error::kStageMismatch);
  EXPECT_TRUE(base::PathExists(stage_database_path_));
  EXPECT_TRUE(base::PathExists(foreign));
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       InterruptedStageCleanupResumesOnlyThroughCanonicalEmptyParents) {
  auto witness = CommitAndRelease();
  ASSERT_TRUE(witness);
  const base::FilePath stage_child = stage_database_path_.DirName();
  ASSERT_TRUE(base::DeleteFile(stage_database_path_));

  auto finalizer = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), witness.Clone());
  ASSERT_TRUE(finalizer.has_value())
      << "finalizer refused with error " << static_cast<int>(finalizer.error());
  EXPECT_TRUE((*finalizer)->FinalizeForPublication().has_value());
  EXPECT_FALSE(base::PathExists(stage_child));
  EXPECT_FALSE(base::PathExists(StagingParent()));
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       StageCleanupReconcilesAnAlreadyUnlinkedHeldDatabase) {
  auto witness = CommitAndRelease();
  ASSERT_TRUE(witness);
  auto finalizer = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), witness.Clone());
  ASSERT_TRUE(finalizer.has_value())
      << "finalizer refused with error " << static_cast<int>(finalizer.error());

  // Models a previous cleanup attempt that removed the exact entry but was
  // interrupted before its parent-directory durability barrier completed.
  ASSERT_TRUE(base::DeleteFile(stage_database_path_));
  EXPECT_TRUE((*finalizer)->FinalizeForPublication().has_value());
  EXPECT_FALSE(base::PathExists(StagingParent()));
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       PublishedIdentityWitnessAllowsLaterTypedRecordRevision) {
  auto witness = CommitAndRelease();
  ASSERT_TRUE(witness);
  auto finalizer = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), witness.Clone());
  ASSERT_TRUE(finalizer.has_value())
      << "finalizer refused with error " << static_cast<int>(finalizer.error());
  ASSERT_TRUE((*finalizer)->FinalizeForPublication().has_value());
  finalizer->reset();

  const base::FilePath database_path =
      profile_path_.AppendASCII("TaffyCore").AppendASCII("core.sqlite3");
  sql::Database database(sql::DatabaseOptions(),
                         sql::Database::Tag("TaffyCore"));
  ASSERT_TRUE(database.Open(database_path));
  ASSERT_TRUE(database.Execute(
      "UPDATE core_library_state SET revision=revision+1 WHERE singleton=1"));
  database.Close();

  EXPECT_TRUE(
      VerifyPublishedDormantBackupRestoreTarget(profile_path_, ExactTarget())
          .has_value());
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       MutableOwnerBlocksAcceptAndDiscardPhysicalCustody) {
  auto witness = target_internal::BuildCandidateWitness({});
  ASSERT_TRUE(witness);
  auto accept = DormantBackupRestoreTargetFinalizer::OpenForAccept(
      profile_path_, ExactTarget(), witness.Clone());
  ASSERT_FALSE(accept.has_value());
  EXPECT_EQ(accept.error(), Error::kTargetBusy);
  auto discard = DormantBackupRestoreTargetDeletionCustody::OpenForDiscard(
      profile_path_, ExactTarget());
  ASSERT_FALSE(discard.has_value());
  EXPECT_EQ(discard.error(), Error::kTargetBusy);
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       DiscardCustodyClosesSqlAndVerifiesExactProfileAbsence) {
  target_.reset();
  auto discard = DormantBackupRestoreTargetDeletionCustody::OpenForDiscard(
      profile_path_, ExactTarget());
  ASSERT_TRUE(discard.has_value());
  EXPECT_TRUE((*discard)->PrepareForProfileDeletion());
  EXPECT_FALSE((*discard)->PrepareForProfileDeletion());
  EXPECT_FALSE((*discard)->VerifyProfileDeleted());

  ASSERT_TRUE(base::DeletePathRecursively(profile_path_));
  EXPECT_TRUE((*discard)->VerifyProfileDeleted());
}

TEST_F(DormantBackupRestoreTargetResolutionTest,
       WrongIdentityAndLinkedProfileNeverGainResolutionCustody) {
  target_.reset();
  auto wrong_target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile,
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  auto wrong = DormantBackupRestoreTargetDeletionCustody::OpenForDiscard(
      profile_path_, std::move(wrong_target));
  ASSERT_FALSE(wrong.has_value());
  EXPECT_EQ(wrong.error(), Error::kSchemaMismatch);

  const base::FilePath linked = root_.GetPath().AppendASCII("Profile 2");
  ASSERT_TRUE(base::CreateSymbolicLink(profile_path_, linked));
  auto linked_result =
      DormantBackupRestoreTargetDeletionCustody::OpenForDiscard(linked,
                                                                ExactTarget());
  ASSERT_FALSE(linked_result.has_value());
  EXPECT_EQ(linked_result.error(), Error::kInvalidTarget);
  EXPECT_TRUE(base::PathExists(profile_path_));
}

}  // namespace
}  // namespace taffy::storage::backup
