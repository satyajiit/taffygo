// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/backup_workflow_restore_projection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "base/types/expected.h"
#include "content/public/test/browser_task_environment.h"
#include "taffy/browser/android/backup_workflow_restore_android.h"
#include "taffy/browser/android/backup_workflow_restore_android_internal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class BackupWorkflowRestoreAndroidTestPeer final {
 public:
  static void AddCleanupFirstCommit(BackupWorkflowRestoreAndroid& restore,
                                    std::string operation_id) {
    auto operation = std::make_unique<BackupWorkflowRestoreAndroid::Operation>(
        1u, operation_id, u"Retained candidate", nullptr, nullptr);
    operation->phase =
        BackupWorkflowRestoreAndroid::Operation::Phase::kCleaning;
    operation->review_token = 9u;
    operation->target_adopted = true;
    operation->precommit_cleanup_settled = true;
    operation->commit_completion_pending = true;
    restore.operations_.emplace(std::move(operation_id), std::move(operation));
  }

  static void AddAdoptedPlanningOperation(BackupWorkflowRestoreAndroid& restore,
                                          std::string operation_id) {
    auto operation = std::make_unique<BackupWorkflowRestoreAndroid::Operation>(
        1u, operation_id, u"Retained candidate", nullptr, nullptr);
    operation->phase =
        BackupWorkflowRestoreAndroid::Operation::Phase::kPlanning;
    operation->target_adopted = true;
    restore.operations_.emplace(std::move(operation_id), std::move(operation));
  }

  static void DeliverPreparationFailure(BackupWorkflowRestoreAndroid& restore,
                                        const std::string& operation_id) {
    restore.OnWorkflowPrepared(
        operation_id,
        {.status =
             ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable});
  }

  static void CloseWindow(BackupWorkflowRestoreAndroid& restore) {
    restore.OnWindowUnregistered(1u);
  }

  static bool IsAdoptedCleanupWithoutPretransfer(
      const BackupWorkflowRestoreAndroid& restore,
      const std::string& operation_id) {
    auto found = restore.operations_.find(operation_id);
    return found != restore.operations_.end() &&
           found->second->target_adopted &&
           !found->second->pretransfer_cleanup_started &&
           found->second->phase ==
               BackupWorkflowRestoreAndroid::Operation::Phase::kCleaning;
  }

  static void DeliverPrecommitCommit(BackupWorkflowRestoreAndroid& restore,
                                     const std::string& operation_id) {
    restore.OnWorkflowCommitted(
        operation_id,
        ProfileBackupWorkflow::RestoreCommitStatus::kPrecommitCleanupRequired);
  }

  static size_t OperationCount(const BackupWorkflowRestoreAndroid& restore) {
    return restore.operations_.size();
  }

  static void AddPendingRecoveryDiscovery(
      BackupWorkflowRestoreAndroid& restore,
      ProfileBackupWorkflow::WindowToken owner,
      uint64_t request_token) {
    auto pending = std::make_unique<
        BackupWorkflowRestoreAndroid::PendingRecoveryDiscovery>(
        owner, request_token, nullptr, nullptr);
    restore.pending_recovery_discoveries_.emplace(
        std::pair(owner, request_token), std::move(pending));
  }

  static void AddRecoveredReview(BackupWorkflowRestoreAndroid& restore,
                                 ProfileBackupWorkflow::WindowToken owner,
                                 uint64_t token,
                                 bool discard_only,
                                 bool resolving) {
    auto review =
        std::make_unique<BackupWorkflowRestoreAndroid::RecoveredReview>(
            owner, token, "reservation", discard_only);
    if (resolving) {
      review->phase =
          BackupWorkflowRestoreAndroid::RecoveredReview::Phase::kResolving;
    }
    restore.recovered_reviews_.emplace(token, std::move(review));
  }

  static size_t PendingRecoveryCount(
      const BackupWorkflowRestoreAndroid& restore) {
    return restore.pending_recovery_discoveries_.size();
  }

  static size_t RecoveredReviewCount(
      const BackupWorkflowRestoreAndroid& restore) {
    return restore.recovered_reviews_.size();
  }

  static bool IsRecoveredReviewDetached(
      const BackupWorkflowRestoreAndroid& restore,
      uint64_t token) {
    auto found = restore.recovered_reviews_.find(token);
    return found != restore.recovered_reviews_.end() && found->second->detached;
  }
};

namespace backup_workflow_restore_internal {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreCandidateResolutionError;
using Physical = BackupRestoreCandidateResolution;
using Result = BrowserProfilesRestoreLifecycle::CandidateResolutionResult;
using Status = ProfileBackupWorkflow::RestoreResolutionStatus;

Result Classified(mojom::BackupRestoreRecoveryClassificationKind kind,
                  bool dispatched = false,
                  bool retired = false) {
  auto classification = mojom::BackupRestoreRecoveryClassification::New();
  classification->kind = kind;
  return Physical{.classification = std::move(classification),
                  .physical_action_dispatched = dispatched,
                  .reservation_retired = retired};
}

BackupRestoreRecoveryPresentation Presentation() {
  BackupRestoreRecoveryPresentation presentation;
  presentation.target_profile_label = u"Recovered profile";
  presentation.selected_classes.push_back(
      {.kind = mojom::BackupRecordKind::kAssistantConfiguration});
  return presentation;
}

BackupRestoreRestartDiscovery Candidate(
    BackupRestoreRestartDiscoveryStatus status,
    BackupRestoreRestartCandidateAction action) {
  return {.status = status,
          .candidate =
              BackupRestoreRestartCandidate{.reservation_id = "reservation",
                                            .presentation = Presentation(),
                                            .action = action}};
}

TEST(BackupWorkflowRestoreProjectionTest,
     InvalidInputIsTheOnlyRetryablePhysicalProjectionError) {
  EXPECT_EQ(Status::kRefused, ProjectCandidateResolution(Result(
                                  base::unexpected(Error::kInvalidArgument))));

  constexpr std::array kAmbiguousErrors = {
      Error::kRegistryRefused,     Error::kPersistenceFailed,
      Error::kProfileStateRefused, Error::kCoreUnavailable,
      Error::kHistoryRefused,      Error::kStorageUnavailable,
  };
  for (Error error : kAmbiguousErrors) {
    EXPECT_EQ(Status::kRecoveryRequired,
              ProjectCandidateResolution(Result(base::unexpected(error))));
  }
}

TEST(BackupWorkflowRestoreProjectionTest,
     TargetLeaseBusyCannotProveCandidateRemainsUnchanged) {
  EXPECT_EQ(Status::kRecoveryRequired,
            ProjectCandidateResolution(Result(base::unexpected(Error::kBusy))));
  EXPECT_FALSE(ResolutionRetainsHiddenCandidate(Status::kRecoveryRequired));
}

TEST(BackupWorkflowRestoreProjectionTest,
     DurableNonCompletionRetainsCandidateAcrossEitherDispatchSide) {
  for (bool dispatched : {false, true}) {
    EXPECT_EQ(
        Status::kDefinitelyNotCompleted,
        ProjectCandidateResolution(Classified(
            mojom::BackupRestoreRecoveryClassificationKind::kRollbackAvailable,
            dispatched)));
    EXPECT_EQ(
        Status::kDefinitelyNotCompleted,
        ProjectCandidateResolution(Classified(
            mojom::BackupRestoreRecoveryClassificationKind::kCleanupRequired,
            dispatched)));
  }
}

TEST(BackupWorkflowRestoreProjectionTest,
     TerminalNeedsRetiredReservationBeforeUiSuccess) {
  EXPECT_EQ(Status::kRecoveryRequired,
            ProjectCandidateResolution(Classified(
                mojom::BackupRestoreRecoveryClassificationKind::kPublished)));
  EXPECT_EQ(Status::kPublished,
            ProjectCandidateResolution(Classified(
                mojom::BackupRestoreRecoveryClassificationKind::kPublished,
                true, true)));
  EXPECT_EQ(
      Status::kRecoveryRequired,
      ProjectCandidateResolution(Classified(
          mojom::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted)));
  EXPECT_EQ(
      Status::kVerifiedDeleted,
      ProjectCandidateResolution(Classified(
          mojom::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted,
          true, true)));
}

TEST(BackupWorkflowRestoreProjectionTest,
     UnknownOrMissingClassificationNeverRetainsRetryAuthority) {
  EXPECT_EQ(Status::kRecoveryRequired, ProjectCandidateResolution(Physical{}));
  EXPECT_EQ(
      Status::kRecoveryRequired,
      ProjectCandidateResolution(Classified(
          mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired,
          true)));
  EXPECT_FALSE(ResolutionRetainsHiddenCandidate(Status::kRecoveryRequired));
  EXPECT_TRUE(
      ResolutionRetainsHiddenCandidate(Status::kDefinitelyNotCompleted));
  EXPECT_TRUE(ResolutionRetainsHiddenCandidate(Status::kRefused));
  EXPECT_TRUE(ResolutionRetainsHiddenCandidate(Status::kUnavailable));
}

TEST(BackupWorkflowRestoreProjectionTest,
     PrecommitOwnerWaitsForBothCleanupAndInternalCommitDelivery) {
  EXPECT_FALSE(CanReleasePrecommitOperation(
      /*cleanup_settled=*/false, /*commit_completion_pending=*/true));
  EXPECT_FALSE(CanReleasePrecommitOperation(
      /*cleanup_settled=*/true, /*commit_completion_pending=*/true));
  EXPECT_FALSE(CanReleasePrecommitOperation(
      /*cleanup_settled=*/false, /*commit_completion_pending=*/false));
  EXPECT_TRUE(CanReleasePrecommitOperation(
      /*cleanup_settled=*/true, /*commit_completion_pending=*/false));
}

TEST(BackupWorkflowRestoreProjectionTest,
     CleanupFirstThenInternalCommitDeliveryReleasesAndroidOwner) {
  content::BrowserTaskEnvironment environment;
  BackupWorkflowRestoreAndroid restore(nullptr, nullptr, nullptr, nullptr);
  constexpr char kOperation[] = "cleanup-first";
  BackupWorkflowRestoreAndroidTestPeer::AddCleanupFirstCommit(restore,
                                                              kOperation);
  ASSERT_EQ(1u, BackupWorkflowRestoreAndroidTestPeer::OperationCount(restore));

  BackupWorkflowRestoreAndroidTestPeer::DeliverPrecommitCommit(restore,
                                                               kOperation);

  EXPECT_EQ(0u, BackupWorkflowRestoreAndroidTestPeer::OperationCount(restore));
}

TEST(BackupWorkflowRestoreProjectionTest,
     AdoptedInlinePreparationFailureCannotStartPretransferCleanup) {
  content::BrowserTaskEnvironment environment;
  BackupWorkflowRestoreAndroid restore(nullptr, nullptr, nullptr, nullptr);
  constexpr char kOperation[] = "inline-adopted-refusal";
  BackupWorkflowRestoreAndroidTestPeer::AddAdoptedPlanningOperation(restore,
                                                                    kOperation);

  BackupWorkflowRestoreAndroidTestPeer::DeliverPreparationFailure(restore,
                                                                  kOperation);
  BackupWorkflowRestoreAndroidTestPeer::CloseWindow(restore);

  EXPECT_TRUE(
      BackupWorkflowRestoreAndroidTestPeer::IsAdoptedCleanupWithoutPretransfer(
          restore, kOperation));
}

TEST(BackupWorkflowRestoreProjectionTest,
     RecoveryPresentationRetainsSelectedZeroCountRows) {
  auto flattened = FlattenRecoveryPresentation(Presentation());
  ASSERT_TRUE(flattened);
  ASSERT_EQ(7u, flattened->size());
  EXPECT_EQ(
      static_cast<int32_t>(mojom::BackupRecordKind::kAssistantConfiguration),
      flattened->front());
  EXPECT_TRUE(std::ranges::all_of(flattened->begin() + 1, flattened->end(),
                                  [](int32_t value) { return value == 0; }));
}

TEST(BackupWorkflowRestoreProjectionTest,
     RecoveryPresentationRejectsUnboundedOrInvalidWireData) {
  auto presentation = Presentation();
  presentation.target_profile_label.clear();
  EXPECT_FALSE(FlattenRecoveryPresentation(presentation));

  presentation = Presentation();
  presentation.selected_classes.front().action_counts.front() =
      static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1u;
  EXPECT_FALSE(FlattenRecoveryPresentation(presentation));

  presentation = Presentation();
  presentation.selected_classes.front().kind =
      static_cast<mojom::BackupRecordKind>(99);
  EXPECT_FALSE(FlattenRecoveryPresentation(presentation));
}

TEST(BackupWorkflowRestoreProjectionTest,
     OnlyExactRecoveryClassificationsMintReviewPresentation) {
  EXPECT_TRUE(IsValidRecoveredCandidate(
      Candidate(BackupRestoreRestartDiscoveryStatus::kRollbackAvailable,
                BackupRestoreRestartCandidateAction::kReview)));
  EXPECT_TRUE(IsValidRecoveredCandidate(
      Candidate(BackupRestoreRestartDiscoveryStatus::kCleanupRequired,
                BackupRestoreRestartCandidateAction::kDiscardOnly)));
  EXPECT_FALSE(IsValidRecoveredCandidate(
      Candidate(BackupRestoreRestartDiscoveryStatus::kRollbackAvailable,
                BackupRestoreRestartCandidateAction::kDiscardOnly)));
  EXPECT_FALSE(IsValidRecoveredCandidate(
      Candidate(BackupRestoreRestartDiscoveryStatus::kOutcomeUnknown,
                BackupRestoreRestartCandidateAction::kReview)));
}

TEST(BackupWorkflowRestoreProjectionTest,
     CleanupOnlyRecoveredReviewRejectsAcceptButAllowsDiscard) {
  EXPECT_FALSE(RecoveryResolutionChoiceAllowed(
      true, mojom::BackupRestoreResolutionChoice::kAcceptCandidate));
  EXPECT_TRUE(RecoveryResolutionChoiceAllowed(
      true, mojom::BackupRestoreResolutionChoice::kDiscardCandidate));
  EXPECT_TRUE(RecoveryResolutionChoiceAllowed(
      false, mojom::BackupRestoreResolutionChoice::kAcceptCandidate));
}

TEST(BackupWorkflowRestoreProjectionTest,
     DiscoveryWithdrawalAndReviewAbandonmentAreExactAndWindowBound) {
  content::BrowserTaskEnvironment environment;
  BackupWorkflowRestoreAndroid restore(nullptr, nullptr, nullptr, nullptr);
  BackupWorkflowRestoreAndroidTestPeer::AddPendingRecoveryDiscovery(restore, 1u,
                                                                    7u);
  restore.AbandonInterruptedRestoreDiscovery(2u, 7u);
  EXPECT_EQ(
      1u, BackupWorkflowRestoreAndroidTestPeer::PendingRecoveryCount(restore));
  restore.AbandonInterruptedRestoreDiscovery(1u, 7u);
  EXPECT_EQ(
      0u, BackupWorkflowRestoreAndroidTestPeer::PendingRecoveryCount(restore));

  BackupWorkflowRestoreAndroidTestPeer::AddRecoveredReview(restore, 1u, 41u,
                                                           false, false);
  restore.AbandonRecoveredRestoreReview(2u, 41u);
  EXPECT_EQ(
      1u, BackupWorkflowRestoreAndroidTestPeer::RecoveredReviewCount(restore));
  restore.AbandonRecoveredRestoreReview(1u, 41u);
  EXPECT_EQ(
      0u, BackupWorkflowRestoreAndroidTestPeer::RecoveredReviewCount(restore));
}

TEST(BackupWorkflowRestoreProjectionTest,
     WindowCloseRevokesReviewButLetsDispatchedResolutionDrain) {
  content::BrowserTaskEnvironment environment;
  BackupWorkflowRestoreAndroid restore(nullptr, nullptr, nullptr, nullptr);
  BackupWorkflowRestoreAndroidTestPeer::AddRecoveredReview(restore, 1u, 51u,
                                                           false, false);
  BackupWorkflowRestoreAndroidTestPeer::AddRecoveredReview(restore, 1u, 52u,
                                                           false, true);

  BackupWorkflowRestoreAndroidTestPeer::CloseWindow(restore);

  EXPECT_EQ(
      1u, BackupWorkflowRestoreAndroidTestPeer::RecoveredReviewCount(restore));
  EXPECT_TRUE(BackupWorkflowRestoreAndroidTestPeer::IsRecoveredReviewDetached(
      restore, 52u));
}

}  // namespace
}  // namespace backup_workflow_restore_internal
}  // namespace taffy
