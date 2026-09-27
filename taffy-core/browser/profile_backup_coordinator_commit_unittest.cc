// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator_commit_test_fixture.h"

namespace taffy::backup_coordinator_commit_test {

TEST_F(ProfileBackupCoordinatorCommitTest, NoCommitBeforeExactStaging) {
  coordinator_->CommitStagedImportedRestore(
      kOperation,
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreCommitResult result) {
            ++callbacks_;
            ASSERT_FALSE(result);
            EXPECT_EQ(ProfileBackupError::kBusy, result.error());
          }));
  EXPECT_EQ(1, callbacks_);
  EXPECT_EQ(0, target_.commit_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       DurableCommitAndSourceAckEnterHiddenReviewOnce) {
  Commit();
  EXPECT_EQ(0, target_.commit_calls());
  Authorize();
  ASSERT_EQ(1, target_.commit_calls());
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = true});
  EXPECT_EQ(0, callbacks_);
  Acknowledge();
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_ && *result_);
  EXPECT_TRUE(result_->value().source_acknowledged);
  EXPECT_TRUE(Peer::IsHiddenReview(*coordinator_, kOperation));
  coordinator_->Cancel(kOperation);
  coordinator_->CancelAll();
  EXPECT_EQ(0, target_.cleanup_calls());
  EXPECT_TRUE(target_.owner_live());
  coordinator_->CommitStagedImportedRestore(
      kOperation, base::BindLambdaForTesting(
                      [](ProfileBackupCoordinator::RestoreCommitResult result) {
                        ASSERT_FALSE(result);
                        EXPECT_EQ(ProfileBackupError::kBusy, result.error());
                      }));
  EXPECT_EQ(1, target_.commit_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       UnwitnessedSuccessIsUnknownAndNeverHiddenReview) {
  Commit();
  Authorize();
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = false});
  Acknowledge();
  ASSERT_TRUE(result_ && *result_);
  EXPECT_EQ(Outcome::kOutcomeUnknown, result_->value().physical.outcome);
  EXPECT_FALSE(result_->value().physical.journal_durable);
  EXPECT_TRUE(Peer::IsCommitRecoveryRequired(*coordinator_, kOperation));
  EXPECT_FALSE(Peer::HoldsWorkflowInterest(*coordinator_, kOperation));
  EXPECT_EQ(0, target_.cleanup_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       CancellationBeforeAuthorizationNeverDispatches) {
  Commit();
  coordinator_->Cancel(kOperation);
  Authorize();
  EXPECT_EQ(0, target_.commit_calls());
  Acknowledge();
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_);
  ASSERT_FALSE(*result_);
  EXPECT_EQ(ProfileBackupError::kCancelled, result_->error());
  EXPECT_EQ(0, target_.cleanup_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       SourceLossDuringCommitDrainsWithoutCleanupOrReplay) {
  Commit();
  Authorize();
  ASSERT_TRUE(target_.commit_pending());
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  EXPECT_EQ(0, callbacks_);
  EXPECT_EQ(0, target_.cleanup_calls());
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = true});
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_ && *result_);
  EXPECT_EQ(Outcome::kCommitted, result_->value().physical.outcome);
  EXPECT_FALSE(result_->value().source_acknowledged);
  EXPECT_TRUE(Peer::IsCommitRecoveryRequired(*coordinator_, kOperation));
  EXPECT_EQ(1, target_.commit_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       CancellationAfterDispatchStillReportsPhysicalTerminal) {
  Commit();
  Authorize();
  coordinator_->Cancel(kOperation);
  EXPECT_EQ(0, callbacks_);
  EXPECT_EQ(0, target_.cleanup_calls());
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kOutcomeUnknown, .journal_durable = true});
  Acknowledge();
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_ && *result_);
  EXPECT_EQ(Outcome::kOutcomeUnknown, result_->value().physical.outcome);
  EXPECT_TRUE(Peer::IsCommitRecoveryRequired(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       LateAuthorityAfterOwnerDestructionNeverWrites) {
  Commit();
  auto reply = Peer::PendingCommitReply(*coordinator_, kOperation);
  auto authorization = Peer::ExactCommitReply(*coordinator_, kOperation);
  ASSERT_TRUE(authorization);
  coordinator_.reset();
  ASSERT_FALSE(target_.owner_live());
  std::move(reply).Run(std::move(authorization));
  EXPECT_EQ(0, target_.commit_calls());
  EXPECT_EQ(0, target_.cleanup_calls());
  EXPECT_EQ(0, callbacks_);
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       TerminalCallbackMayDestroyCoordinator) {
  ASSERT_TRUE(Peer::MarkStaged(*coordinator_, kOperation));
  coordinator_->CommitStagedImportedRestore(
      kOperation,
      base::BindLambdaForTesting(
          [this](ProfileBackupCoordinator::RestoreCommitResult result) {
            ++callbacks_;
            EXPECT_TRUE(result.has_value());
            coordinator_.reset();
          }));
  Authorize();
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = true});
  Acknowledge();
  EXPECT_EQ(1, callbacks_);
  EXPECT_FALSE(coordinator_);
  EXPECT_FALSE(target_.owner_live());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       WrongAuthorizationBindingNeverReachesPhysicalOwner) {
  Commit();
  auto reply = Peer::PendingCommitReply(*coordinator_, kOperation);
  auto authorization = Peer::ExactCommitReply(*coordinator_, kOperation);
  ASSERT_TRUE(authorization && authorization->authorization);
  authorization->authorization->binding->confirmation_sha256.assign(32u, 9u);
  std::move(reply).Run(std::move(authorization));
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_);
  EXPECT_FALSE(*result_);
  EXPECT_EQ(0, target_.commit_calls());
  EXPECT_EQ(0, target_.cleanup_calls());
  EXPECT_TRUE(Peer::IsCommitRecoveryRequired(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       SourceLossBeforeAuthorityNeverDispatchesLateReply) {
  Commit();
  auto reply = Peer::PendingCommitReply(*coordinator_, kOperation);
  auto authorization = Peer::ExactCommitReply(*coordinator_, kOperation);
  ASSERT_TRUE(authorization);
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_);
  EXPECT_FALSE(*result_);
  std::move(reply).Run(std::move(authorization));
  EXPECT_EQ(1, callbacks_);
  EXPECT_EQ(0, target_.commit_calls());
  EXPECT_EQ(0, target_.cleanup_calls());

  // The consumptive request escaped, so precommit deletion is no longer
  // permitted. Its drained loss must still release the mutable writer for
  // read-only recovery while preserving the target and stage.
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
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       PreDispatchStorageRefusalNeverStartsCleanupOrRetry) {
  Commit();
  Authorize();
  target_.CompleteCommit(
      base::unexpected(ProfileBackupRestoreTargetError::kStorageUnavailable));
  Acknowledge();
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_);
  ASSERT_FALSE(*result_);
  EXPECT_EQ(ProfileBackupError::kStorageUnavailable, result_->error());
  EXPECT_TRUE(Peer::IsCommitRecoveryRequired(*coordinator_, kOperation));
  EXPECT_EQ(1, target_.commit_calls());
  EXPECT_EQ(0, target_.cleanup_calls());
}

TEST_F(ProfileBackupCoordinatorCommitTest,
       SourceLossAwaitingAcknowledgementPreservesPhysicalReceipt) {
  Commit();
  Authorize();
  target_.CompleteCommit(ProfileBackupRestoreCommitObservation{
      .outcome = Outcome::kCommitted, .journal_durable = true});
  EXPECT_EQ(0, callbacks_);
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  ASSERT_EQ(1, callbacks_);
  ASSERT_TRUE(result_ && *result_);
  EXPECT_EQ(Outcome::kCommitted, result_->value().physical.outcome);
  EXPECT_TRUE(result_->value().physical.journal_durable);
  EXPECT_FALSE(result_->value().source_acknowledged);
  Acknowledge();
  EXPECT_EQ(1, callbacks_);
  EXPECT_TRUE(Peer::IsCommitRecoveryRequired(*coordinator_, kOperation));
  EXPECT_EQ(0, target_.cleanup_calls());
}

}  // namespace taffy::backup_coordinator_commit_test
