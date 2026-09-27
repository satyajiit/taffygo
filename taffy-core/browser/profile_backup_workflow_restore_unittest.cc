// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "crypto/secure_util.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_internal.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/backup_recovery_key_codec.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class ProfileBackupWorkflowRestoreTestPeer final {
 public:
  // The archive is verified by the import path, which needs a live core and a
  // real payload; every test below is about what happens after that, so this
  // places the operation where a verified import leaves it. The phase is the
  // only precondition BeginImportedRestorePreparation reads.
  static bool MarkImportVerified(ProfileBackupWorkflow& workflow,
                                 const std::string& operation_id) {
    base::AutoLock guard(workflow.state_lock_);
    auto found = workflow.operations_.find(operation_id);
    if (found == workflow.operations_.end()) {
      return false;
    }
    found->second->phase =
        ProfileBackupWorkflow::Operation::Phase::kImportVerified;
    return true;
  }

  static bool ArmInlinePrecommitCleanup(
      ProfileBackupWorkflow& workflow,
      ProfileBackupWorkflow::WindowToken owner,
      const std::string& operation_id,
      const std::string& target_profile_id,
      int* receipt_count) {
    {
      base::AutoLock guard(workflow.state_lock_);
      auto found = workflow.operations_.find(operation_id);
      if (found == workflow.operations_.end()) {
        return false;
      }
      found->second->phase =
          ProfileBackupWorkflow::Operation::Phase::kRestoreCommitting;
    }
    return workflow.coordinator_->ArmPrecommitCancellationReceipt(
        operation_id, target_profile_id,
        base::BindLambdaForTesting(
            [&workflow, owner, operation_id, receipt_count](
                ProfileBackupCoordinator::PrecommitCancellationResult result) {
              EXPECT_TRUE(result.has_value());
              ++*receipt_count;
              workflow.AbandonOperation(owner, operation_id);
            }));
  }

  static void DeliverPrecommitCommitFailure(ProfileBackupWorkflow& workflow,
                                            const std::string& operation_id,
                                            int* commit_count) {
    {
      base::AutoLock guard(workflow.state_lock_);
      auto found = workflow.operations_.find(operation_id);
      ASSERT_TRUE(found != workflow.operations_.end());
      found->second->restore_commit_callback = base::BindLambdaForTesting(
          [commit_count](ProfileBackupWorkflow::RestoreCommitStatus status) {
            EXPECT_EQ(ProfileBackupWorkflow::RestoreCommitStatus::
                          kPrecommitCleanupRequired,
                      status);
            ++*commit_count;
          });
    }
    workflow.OnImportedRestoreCommitted(
        operation_id, base::unexpected(ProfileBackupError::kCoreUnavailable));
  }

  static ProfileBackupWorkflow::RestoreReviewToken AddHiddenReview(
      ProfileBackupWorkflow& workflow,
      const std::string& operation_id,
      bool discard_only = false) {
    base::AutoLock guard(workflow.state_lock_);
    auto found = workflow.operations_.find(operation_id);
    if (found == workflow.operations_.end()) {
      return 0u;
    }
    constexpr ProfileBackupWorkflow::RestoreReviewToken kReview = 17u;
    found->second->phase =
        discard_only ? ProfileBackupWorkflow::Operation::Phase::
                           kRestoreDefinitelyNotCommitted
                     : ProfileBackupWorkflow::Operation::Phase::kHiddenReview;
    found->second->restore_discard_only_resolution = discard_only;
    found->second->restore_review_token = kReview;
    found->second->reservation_id = "reservation";
    return kReview;
  }

  static bool CoordinatorHasImport(const ProfileBackupWorkflow& workflow,
                                   const std::string& operation_id) {
    return workflow.coordinator_ && ProfileBackupCoordinatorTestPeer::HasImport(
                                        *workflow.coordinator_, operation_id);
  }

  static ProfileBackupCoordinator& Coordinator(
      ProfileBackupWorkflow& workflow) {
    return *workflow.coordinator_;
  }

  static ProfileBackupWorkflow::RestoreReviewToken MarkStagedReview(
      ProfileBackupWorkflow& workflow,
      const std::string& operation_id) {
    base::AutoLock guard(workflow.state_lock_);
    auto found = workflow.operations_.find(operation_id);
    if (found == workflow.operations_.end()) {
      return 0u;
    }
    constexpr ProfileBackupWorkflow::RestoreReviewToken kReview = 23u;
    found->second->phase =
        ProfileBackupWorkflow::Operation::Phase::kRestoreStaged;
    found->second->restore_review_token = kReview;
    return kReview;
  }
};

namespace {

namespace mojom = core_service::mojom;

constexpr char kInstallationId[] = "08e23c21-a9ee-4c04-9357-673a6de6a82c";
constexpr char kSource[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTarget[] = "22222222-2222-4222-8222-222222222222";

mojom::BackupRestorePlanResultPtr Plan(uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New(
      "workflow-restore-plan", generation, 0u,
      base::TimeTicks::Now().since_origin().InMilliseconds() + 30'000u,
      "workflow-restore-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, kTarget);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "workflow-restore-archive";
  plan->target = target.Clone();
  plan->snapshot_sha256.assign(32u, 1u);
  plan->confirmation_sha256.assign(32u, 2u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kSource, std::move(target), plan->backup_id,
      plan->snapshot_sha256, plan->confirmation_sha256);
  return plan;
}

std::unique_ptr<CoreServiceManager> MakeManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context) {
  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(context),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
}

class ProfileBackupWorkflowRestoreTest : public testing::Test {
 protected:
  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    manager_ = MakeManager(tail_, context_.get());
    ASSERT_TRUE(staging_parent_.CreateUniqueTempDir());
    stage_store_ = storage::backup::BackupArchiveStageStore::Create(
        staging_parent_.GetPath().Append(
            storage::backup::kBackupStagingDirectoryName));
    ASSERT_TRUE(stage_store_);
    workflow_ = std::make_unique<ProfileBackupWorkflow>(manager_.get(),
                                                        kInstallationId);
    ASSERT_TRUE(workflow_->InstallStageStore(stage_store_));
  }

  void TearDown() override {
    workflow_.reset();
    manager_->Shutdown();
    environment_.RunUntilIdle();
  }

  std::pair<ProfileBackupWorkflow::WindowToken, std::string> OpenImport() {
    const auto owner = workflow_->RegisterWindow();
    EXPECT_TRUE(workflow_->ActivateWindow(owner));
    auto opened = workflow_->OpenRecoveryKeySession(
        owner, ProfileBackupWorkflow::KeyMode::kRestore);
    EXPECT_TRUE(opened);
    if (!opened) {
      return {owner, {}};
    }
    storage::backup::Secret key = storage::backup::GenerateSecret();
    auto displayed = storage::backup::FormatRecoveryKeyForDisplay(key);
    EXPECT_TRUE(displayed);
    if (!displayed) {
      crypto::SecureZeroBuffer(key);
      return {owner, {}};
    }
    EXPECT_EQ(ProfileBackupWorkflow::KeyAcceptance::kAccepted,
              workflow_->AcceptEnteredKey(owner, *opened, *displayed));
    crypto::SecureZeroBuffer(key);
    // The key text is UTF-16, and SecureZeroBuffer wipes a byte span, so
    // the whole array is handed over reinterpreted as its bytes.
    crypto::SecureZeroBuffer(base::as_writable_byte_span(*displayed));
    return {owner, std::move(*opened)};
  }

  content::BrowserTaskEnvironment environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  base::ScopedTempDir staging_parent_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_;
  std::unique_ptr<ProfileBackupWorkflow> workflow_;
};

TEST_F(ProfileBackupWorkflowRestoreTest,
       InlinePrecommitCleanupReleasesWorkflowBeforeCommitDelivery) {
  auto [owner, operation_id] = OpenImport();
  ASSERT_FALSE(operation_id.empty());
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_, kSource);
  int receipt_count = 0;
  int commit_count = 0;
  ASSERT_TRUE(ProfileBackupWorkflowRestoreTestPeer::ArmInlinePrecommitCleanup(
      *workflow_, owner, operation_id, kTarget, &receipt_count));

  ProfileBackupWorkflowRestoreTestPeer::DeliverPrecommitCommitFailure(
      *workflow_, operation_id, &commit_count);

  EXPECT_EQ(1, receipt_count);
  EXPECT_EQ(1, commit_count);
  EXPECT_EQ(0u, workflow_->operation_count_for_testing());
  environment_.RunUntilIdle();
}

TEST_F(ProfileBackupWorkflowRestoreTest,
       InlineCoreRefusalStillReportsTransferredCleanupCustody) {
  auto [owner, operation_id] = OpenImport();
  ASSERT_FALSE(operation_id.empty());
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_, kSource);
  ASSERT_TRUE(ProfileBackupWorkflowRestoreTestPeer::MarkImportVerified(
      *workflow_, operation_id));
  int preparation_count = 0;
  ASSERT_TRUE(workflow_->BeginImportedRestorePreparation(
      owner, operation_id, u"Restored profile",
      base::BindLambdaForTesting(
          [&preparation_count](
              ProfileBackupWorkflow::RestorePreparation result) {
            EXPECT_EQ(
                ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable,
                result.status);
            ++preparation_count;
          })));
  ProfileBackupRestoreTargetTestState target(kTarget);
  target.CompleteCleanupInline(true);
  int receipt_count = 0;
  manager_->Shutdown();

  EXPECT_TRUE(workflow_->AttachImportedRestoreTarget(
      owner, operation_id, "reservation", target.TakeOwner(),
      base::BindOnce(
          [](std::string, std::u16string, std::vector<mojom::BackupRecordKind>,
             mojom::BackupRestorePlanResultPtr,
             ProfileBackupWorkflow::RestorePresentationCallback callback) {
            std::move(callback).Run(base::unexpected(
                BackupRestoreProfileRegistryError::kUnavailable));
          }),
      base::BindLambdaForTesting(
          [&receipt_count](
              ProfileBackupCoordinator::PrecommitCancellationResult result) {
            EXPECT_TRUE(result.has_value());
            ++receipt_count;
          })));

  EXPECT_EQ(1, preparation_count);
  EXPECT_EQ(1, receipt_count);
  EXPECT_FALSE(target.owner_live());
}

TEST_F(ProfileBackupWorkflowRestoreTest,
       LostCommitAuthorityReplyClosesIntoRecoveryNotPrecommitRetry) {
  auto [owner, operation_id] = OpenImport();
  ASSERT_FALSE(operation_id.empty());
  auto service = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
  auto session = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_, kSource);
  base::FilePath payload_path;
  base::File payload = base::CreateAndOpenTemporaryFileInDir(
      staging_parent_.GetPath(), &payload_path);
  ASSERT_TRUE(payload.IsValid());
  ProfileBackupRestoreTargetTestState target(kTarget);
  auto& coordinator =
      ProfileBackupWorkflowRestoreTestPeer::Coordinator(*workflow_);
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetReadyRestore(
      coordinator, operation_id, target.TakeOwner(),
      Plan(manager_->service_generation()), std::move(payload)));
  ASSERT_TRUE(
      ProfileBackupCoordinatorTestPeer::MarkStaged(coordinator, operation_id));
  const auto review = ProfileBackupWorkflowRestoreTestPeer::MarkStagedReview(
      *workflow_, operation_id);
  ASSERT_NE(0u, review);
  int commit_count = 0;
  ProfileBackupWorkflow::RestoreCommitStatus commit_status =
      ProfileBackupWorkflow::RestoreCommitStatus::kUnavailable;
  ASSERT_TRUE(workflow_->CommitStagedImportedRestore(
      owner, review,
      base::BindLambdaForTesting(
          [&commit_count,
           &commit_status](ProfileBackupWorkflow::RestoreCommitStatus status) {
            ++commit_count;
            commit_status = status;
          })));

  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  EXPECT_EQ(0, target.commit_calls());
  ASSERT_EQ(1, target.close_calls());
  EXPECT_EQ(0, commit_count);
  target.CompleteCloseForRecovery(true);

  EXPECT_EQ(1, commit_count);
  EXPECT_EQ(ProfileBackupWorkflow::RestoreCommitStatus::kRecoveryRequired,
            commit_status);
  EXPECT_FALSE(target.owner_live());
  static_cast<void>(service);
  static_cast<void>(session);
}

TEST_F(ProfileBackupWorkflowRestoreTest,
       TerminalResolutionReleasesWorkflowAndCoordinatorOperation) {
  auto [owner, operation_id] = OpenImport();
  ASSERT_FALSE(operation_id.empty());
  const auto review = ProfileBackupWorkflowRestoreTestPeer::AddHiddenReview(
      *workflow_, operation_id);
  ASSERT_NE(0u, review);
  int resolutions = 0;
  auto reservation = workflow_->BeginImportedRestoreResolution(
      owner, review, mojom::BackupRestoreResolutionChoice::kAcceptCandidate,
      base::BindLambdaForTesting(
          [&resolutions](
              ProfileBackupWorkflow::RestoreResolutionStatus status) {
            EXPECT_EQ(
                ProfileBackupWorkflow::RestoreResolutionStatus::kPublished,
                status);
            ++resolutions;
          }));
  ASSERT_TRUE(reservation);
  ASSERT_TRUE(ProfileBackupWorkflowRestoreTestPeer::CoordinatorHasImport(
      *workflow_, operation_id));

  workflow_->CompleteImportedRestoreResolution(
      owner, review,
      ProfileBackupWorkflow::RestoreResolutionStatus::kPublished);

  EXPECT_EQ(1, resolutions);
  EXPECT_EQ(0u, workflow_->operation_count_for_testing());
  EXPECT_FALSE(ProfileBackupWorkflowRestoreTestPeer::CoordinatorHasImport(
      *workflow_, operation_id));
}

TEST_F(ProfileBackupWorkflowRestoreTest,
       DefinitelyNotCommittedRemainsDiscardOnlyAfterNoncompletion) {
  auto [owner, operation_id] = OpenImport();
  ASSERT_FALSE(operation_id.empty());
  const auto review = ProfileBackupWorkflowRestoreTestPeer::AddHiddenReview(
      *workflow_, operation_id, /*discard_only=*/true);
  ASSERT_NE(0u, review);
  auto ignored =
      base::BindOnce([](ProfileBackupWorkflow::RestoreResolutionStatus) {});

  EXPECT_FALSE(workflow_->BeginImportedRestoreResolution(
      owner, review, mojom::BackupRestoreResolutionChoice::kAcceptCandidate,
      std::move(ignored)));
  int resolutions = 0;
  EXPECT_TRUE(workflow_->BeginImportedRestoreResolution(
      owner, review, mojom::BackupRestoreResolutionChoice::kDiscardCandidate,
      base::BindLambdaForTesting(
          [&resolutions](ProfileBackupWorkflow::RestoreResolutionStatus) {
            ++resolutions;
          })));
  workflow_->CompleteImportedRestoreResolution(
      owner, review,
      ProfileBackupWorkflow::RestoreResolutionStatus::kDefinitelyNotCompleted);
  EXPECT_EQ(1, resolutions);
  EXPECT_FALSE(workflow_->BeginImportedRestoreResolution(
      owner, review, mojom::BackupRestoreResolutionChoice::kAcceptCandidate,
      base::BindOnce([](ProfileBackupWorkflow::RestoreResolutionStatus) {})));
  EXPECT_TRUE(workflow_->BeginImportedRestoreResolution(
      owner, review, mojom::BackupRestoreResolutionChoice::kDiscardCandidate,
      base::BindOnce([](ProfileBackupWorkflow::RestoreResolutionStatus) {})));
}

}  // namespace
}  // namespace taffy
