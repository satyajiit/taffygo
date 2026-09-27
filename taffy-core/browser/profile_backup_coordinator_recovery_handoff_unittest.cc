// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator_commit_test_fixture.h"

namespace taffy::backup_coordinator_commit_test {

TEST_F(ProfileBackupCoordinatorCommitTest,
       RecoveryHandoffWaitsForWriterCloseAndMayDestroyCoordinator) {
  Commit();
  Authorize();
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = true});
  Acknowledge();
  int closed = 0;
  coordinator_->CloseRestoreForRecovery(
      kOperation, base::BindLambdaForTesting(
                      [&](ProfileBackupCoordinator::RestoreStageResult result) {
                        EXPECT_TRUE(result);
                        EXPECT_FALSE(target_.owner_live());
                        ++closed;
                        coordinator_.reset();
                      }));
  EXPECT_EQ(1, target_.close_calls());
  EXPECT_EQ(0, closed);
  EXPECT_TRUE(target_.owner_live());
  EXPECT_TRUE(Peer::HoldsWorkflowInterest(*coordinator_, kOperation));
  coordinator_->CancelAll();
  target_.CompleteCloseForRecovery(true);
  EXPECT_EQ(1, closed);
  EXPECT_FALSE(coordinator_);
  EXPECT_EQ(0, target_.cleanup_calls());
  EXPECT_EQ(1, target_.commit_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       RecoveryHandoffCannotRaceCommitOrItsOutstandingReport) {
  auto refused = base::BindLambdaForTesting(
      [](ProfileBackupCoordinator::RestoreStageResult result) {
        ASSERT_FALSE(result);
        EXPECT_EQ(ProfileBackupError::kBusy, result.error());
      });
  coordinator_->CloseRestoreForRecovery(kOperation, refused);
  Commit();
  Authorize();
  coordinator_->CloseRestoreForRecovery(kOperation, refused);
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = true});
  coordinator_->CloseRestoreForRecovery(kOperation, refused);
  EXPECT_EQ(0, target_.close_calls());
  Acknowledge();
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       RecoveryHandoffAfterSourceLossDoesNotRequireLiveCore) {
  Commit();
  Authorize();
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kOutcomeUnknown, .journal_durable = true});
  int closed = 0;
  coordinator_->CloseRestoreForRecovery(
      kOperation, base::BindLambdaForTesting(
                      [&](ProfileBackupCoordinator::RestoreStageResult result) {
                        EXPECT_TRUE(result);
                        ++closed;
                      }));
  EXPECT_EQ(1, target_.close_calls());
  target_.CompleteCloseForRecovery(true);
  EXPECT_EQ(1, closed);
  EXPECT_FALSE(target_.owner_live());
  EXPECT_FALSE(Peer::HoldsWorkflowInterest(*coordinator_, kOperation));
  EXPECT_EQ(0, target_.cleanup_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       DeclinedCloseRetainsCustodyAndCannotBecomeAnotherPhysicalAttempt) {
  Commit();
  Authorize();
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kOutcomeUnknown, .journal_durable = true});
  Acknowledge();
  int callbacks = 0;
  auto refused = base::BindLambdaForTesting(
      [&](ProfileBackupCoordinator::RestoreStageResult result) {
        EXPECT_FALSE(result);
        ++callbacks;
      });
  coordinator_->CloseRestoreForRecovery(kOperation, refused);
  target_.CompleteCloseForRecovery(false);
  EXPECT_TRUE(target_.owner_live());
  coordinator_->CloseRestoreForRecovery(kOperation, refused);
  EXPECT_EQ(2, callbacks);
  EXPECT_EQ(1, target_.close_calls());
  EXPECT_EQ(1, target_.commit_calls());
  EXPECT_EQ(0, target_.cleanup_calls());
}

}  // namespace taffy::backup_coordinator_commit_test
