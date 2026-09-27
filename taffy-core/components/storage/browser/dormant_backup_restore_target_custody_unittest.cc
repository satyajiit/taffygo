// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/uuid.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

class DormantBackupRestoreTargetCustodyTest : public testing::Test {
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
    scenario_ = test::MakeMixedRestoreScenario(profile_id_);
    ASSERT_TRUE(scenario_.has_value());
    const base::FilePath payload_path =
        root_.GetPath().AppendASCII("plaintext");
    ASSERT_TRUE(base::WriteFile(payload_path, scenario_->payload));
    auto staged = target_->StageAuthorized(
        scenario_->plan.Clone(), test::MakeStageAuthorization(*scenario_->plan),
        base::File(payload_path,
                   base::File::FLAG_OPEN | base::File::FLAG_READ));
    ASSERT_TRUE(staged.has_value());
    stage_database_path_ =
        DormantBackupRestoreCommitTestPeer::StageDatabasePath(*target_);
    ASSERT_FALSE(stage_database_path_.empty());
    ASSERT_TRUE(base::PathExists(stage_database_path_));
  }

  auto Prepare() { return target_->PrepareCommitWitness(); }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  base::FilePath stage_database_path_;
  std::string profile_id_;
  std::unique_ptr<DormantBackupRestoreTarget> target_;
  std::optional<test::DormantBackupRestoreScenario> scenario_;
};

TEST_F(DormantBackupRestoreTargetCustodyTest,
       PreparedStageSurvivesOwnerDestruction) {
  auto witness = Prepare();
  ASSERT_TRUE(witness.has_value())
      << "preparation refused with error "
      << static_cast<int>(witness.error());

  target_.reset();

  EXPECT_TRUE(base::PathExists(stage_database_path_));
  EXPECT_TRUE(base::DirectoryExists(stage_database_path_.DirName()));
}

TEST_F(DormantBackupRestoreTargetCustodyTest,
       CommittedStageSurvivesOwnerDestruction) {
  auto witness = Prepare();
  ASSERT_TRUE(witness.has_value())
      << "preparation refused with error "
      << static_cast<int>(witness.error());
  auto committed = target_->CommitAuthorized(
      scenario_->plan.Clone(), test::MakeCommitAuthorization(*scenario_->plan),
      (*witness).Clone());
  ASSERT_TRUE(committed.has_value());
  ASSERT_EQ(DormantBackupRestoreCommitOutcome::kCommitted, *committed);

  target_.reset();

  EXPECT_TRUE(base::PathExists(stage_database_path_));
  EXPECT_TRUE(base::DirectoryExists(stage_database_path_.DirName()));
}

TEST_F(DormantBackupRestoreTargetCustodyTest,
       PreparedStageCanStillBeExplicitlyDeletedBeforeCommit) {
  auto witness = Prepare();
  ASSERT_TRUE(witness.has_value())
      << "preparation refused with error "
      << static_cast<int>(witness.error());

  ASSERT_TRUE(target_->AbandonStage().has_value());
  EXPECT_FALSE(base::PathExists(stage_database_path_));
  EXPECT_FALSE(base::PathExists(profile_path_.AppendASCII("TaffyCore")
                                    .AppendASCII("TaffyRestoreStaging")));
  target_.reset();
  EXPECT_FALSE(base::PathExists(stage_database_path_));
}

}  // namespace
}  // namespace taffy::storage::backup
