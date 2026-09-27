// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Result = ProfileBackupCoordinator::PrecommitCancellationResult;

constexpr char kOperation[] = "precommit-cancel";
constexpr char kSource[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTarget[] = "22222222-2222-4222-8222-222222222222";
constexpr char kInstallation[] = "33333333-3333-4333-8333-333333333333";

mojom::BackupRestorePlanResultPtr Plan(uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New(
      "precommit-plan", generation, 0u,
      base::TimeTicks::Now().since_origin().InMilliseconds() + 30'000u,
      "precommit-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, kTarget);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "archive";
  plan->target = target.Clone();
  // The snapshot digest must be the one the inspection already found, because
  // OnRestorePlanned compares the two and refuses a plan about some other
  // archive. SetPlanningRestore is what stands in for that inspection here, so
  // this value is its value; the confirmation digest is only carried, so it
  // stays distinct to keep the two apart in a failure message.
  plan->snapshot_sha256.assign(32u, 2u);
  plan->confirmation_sha256.assign(32u, 3u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kSource, std::move(target), plan->backup_id,
      plan->snapshot_sha256, plan->confirmation_sha256);
  return plan;
}

class ProfileBackupCoordinatorPrecommitCancellationTest : public testing::Test {
 protected:
  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    auto tools = std::make_unique<ProfileToolSupervisor>(
        1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        context_.get(), nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(context_.get()),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_,
                                                              kSource);
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    store_ = storage::backup::BackupArchiveStageStore::Create(
        directory_.GetPath().Append(
            storage::backup::kBackupStagingDirectoryName));
    ASSERT_TRUE(store_);
    coordinator_ = std::make_unique<ProfileBackupCoordinator>(
        manager_.get(), store_, kInstallation);
    storage::backup::Secret key{};
    key.fill(1u);
    ASSERT_TRUE(coordinator_->BeginImport(kOperation, key).has_value());
  }

  void TearDown() override {
    coordinator_.reset();
    manager_->Shutdown();
    environment_.RunUntilIdle();
  }

  void Arm() {
    ASSERT_TRUE(coordinator_->ArmPrecommitCancellationReceipt(
        kOperation, kTarget, base::BindLambdaForTesting([this](Result result) {
          ++receipt_callbacks_;
          receipt_result_.emplace(std::move(result));
        })));
  }

  base::File Payload() {
    base::FilePath path;
    return base::CreateAndOpenTemporaryFileInDir(directory_.GetPath(), &path);
  }

  content::BrowserTaskEnvironment environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
  base::ScopedTempDir directory_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> store_;
  std::unique_ptr<ProfileBackupCoordinator> coordinator_;
  ProfileBackupRestoreTargetTestState target_{kTarget};
  int receipt_callbacks_ = 0;
  std::optional<Result> receipt_result_;
};

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       AwaitingCopyCancellationProducesOnePreplanReceipt) {
  Arm();
  EXPECT_FALSE(coordinator_->ArmPrecommitCancellationReceipt(
      kOperation, kTarget, base::BindOnce([](Result) { ADD_FAILURE(); })));

  EXPECT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_EQ(receipt_callbacks_, 1);
  ASSERT_TRUE(receipt_result_);
  EXPECT_TRUE(receipt_result_->has_value());
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
  EXPECT_FALSE(coordinator_->CancelBeforeCommit(kOperation));
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       PlannedCancellationWaitsForPortableAndPhysicalDrain) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      base::BindOnce([](ProfileBackupCoordinator::RestorePreviewResult result) {
        EXPECT_TRUE(result.has_value());
      })));
  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      *coordinator_, kOperation, Plan(manager_->service_generation()));

  EXPECT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(receipt_callbacks_, 0);
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_EQ(receipt_callbacks_, 0);
  target_.CompleteCleanup(true);

  ASSERT_EQ(receipt_callbacks_, 1);
  ASSERT_TRUE(receipt_result_);
  EXPECT_TRUE(receipt_result_->has_value());
  EXPECT_FALSE(target_.owner_live());
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       FailedPhysicalDrainRetainsCustodyUntilExplicitRetry) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      base::BindOnce([](ProfileBackupCoordinator::RestorePreviewResult) {})));
  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      *coordinator_, kOperation, Plan(manager_->service_generation()));
  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(false);
  EXPECT_EQ(receipt_callbacks_, 0);
  EXPECT_TRUE(target_.owner_live());

  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(true);
  ASSERT_EQ(receipt_callbacks_, 1);
  ASSERT_TRUE(receipt_result_ && receipt_result_->has_value());
  EXPECT_FALSE(target_.owner_live());
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       CancellationDuringFailedDrainQueuesExactRetry) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      base::BindOnce([](ProfileBackupCoordinator::RestorePreviewResult) {})));
  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      *coordinator_, kOperation, Plan(manager_->service_generation()));
  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(1, target_.cleanup_calls());
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);

  // This is the window/session withdrawal racing the first asynchronous
  // cleanup. It must remain an addressable retry after the first result fails.
  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  target_.CompleteCleanup(false);
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(2, target_.cleanup_calls());
  EXPECT_EQ(0, receipt_callbacks_);

  target_.CompleteCleanup(true);
  ASSERT_EQ(1, receipt_callbacks_);
  ASSERT_TRUE(receipt_result_ && receipt_result_->has_value());
  EXPECT_FALSE(target_.owner_live());
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       LatePlanContinuationDoesNotInventPhysicalCleanupRetry) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      base::BindOnce([](ProfileBackupCoordinator::RestorePreviewResult) {})));

  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(1, target_.cleanup_calls());
  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      *coordinator_, kOperation, Plan(manager_->service_generation()));
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      *coordinator_, kOperation));

  target_.CompleteCleanup(false);
  EXPECT_FALSE(target_.cleanup_pending());
  EXPECT_EQ(1, target_.cleanup_calls());
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_EQ(0, receipt_callbacks_);
  EXPECT_EQ(1, target_.cleanup_calls());

  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(2, target_.cleanup_calls());
  target_.CompleteCleanup(true);
  ASSERT_EQ(1, receipt_callbacks_);
  ASSERT_TRUE(receipt_result_ && receipt_result_->has_value());
  EXPECT_FALSE(target_.owner_live());
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       RefusedLatePlanDoesNotInventPhysicalCleanupRetry) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      base::BindOnce([](ProfileBackupCoordinator::RestorePreviewResult) {})));

  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(1, target_.cleanup_calls());
  target_.CompleteCleanup(false);

  auto refused = Plan(manager_->service_generation());
  refused->status = mojom::BackupPlanningStatus::kInvalidRequest;
  refused->binding.reset();
  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      *coordinator_, kOperation, std::move(refused));
  EXPECT_FALSE(target_.cleanup_pending());
  EXPECT_EQ(1, target_.cleanup_calls());
  EXPECT_EQ(0, receipt_callbacks_);

  ASSERT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  EXPECT_EQ(2, target_.cleanup_calls());
  target_.CompleteCleanup(true);
  ASSERT_EQ(1, receipt_callbacks_);
  ASSERT_TRUE(receipt_result_ && receipt_result_->has_value());
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       SynchronousPreAuthorityCommitRefusalStillDrainsExactCleanup) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetReadyRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      Plan(manager_->service_generation()), Payload()));
  ASSERT_TRUE(
      ProfileBackupCoordinatorTestPeer::MarkStaged(*coordinator_, kOperation));
  ASSERT_TRUE(
      ProfileBackupCoordinatorTestPeer::RemoveStageAuthorizationForTesting(
          *coordinator_, kOperation));

  int commit_callbacks = 0;
  coordinator_->CommitStagedImportedRestore(
      kOperation,
      base::BindLambdaForTesting(
          [&commit_callbacks](
              ProfileBackupCoordinator::RestoreCommitResult result) {
            ++commit_callbacks;
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(ProfileBackupError::kCoreUnavailable, result.error());
          }));
  EXPECT_EQ(1, commit_callbacks);
  EXPECT_TRUE(coordinator_->CancelBeforeCommit(kOperation));
  EXPECT_EQ(0, target_.commit_calls());
  ASSERT_TRUE(target_.cleanup_pending());

  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_EQ(0, receipt_callbacks_);
  target_.CompleteCleanup(true);

  ASSERT_EQ(1, receipt_callbacks_);
  ASSERT_TRUE(receipt_result_ && receipt_result_->has_value());
  EXPECT_FALSE(target_.owner_live());
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       CommitAuthorityRequestIrreversiblyDisarmsCleanupReceipt) {
  Arm();
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetReadyRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      Plan(manager_->service_generation()), Payload()));
  ASSERT_TRUE(
      ProfileBackupCoordinatorTestPeer::MarkStaged(*coordinator_, kOperation));
  coordinator_->CommitStagedImportedRestore(
      kOperation,
      base::BindOnce([](ProfileBackupCoordinator::RestoreCommitResult) {}));

  ASSERT_EQ(receipt_callbacks_, 1);
  ASSERT_TRUE(receipt_result_);
  ASSERT_FALSE(receipt_result_->has_value());
  EXPECT_EQ(ProfileBackupError::kBusy, receipt_result_->error());
  EXPECT_FALSE(coordinator_->CancelBeforeCommit(kOperation));
  EXPECT_EQ(target_.commit_calls(), 0);
  EXPECT_EQ(target_.cleanup_calls(), 0);
  EXPECT_TRUE(target_.owner_live());
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       CancelAllStopsAfterInlineReceiptDestroysCoordinator) {
  ASSERT_TRUE(coordinator_->ArmPrecommitCancellationReceipt(
      kOperation, kTarget, base::BindLambdaForTesting([this](Result result) {
        EXPECT_TRUE(result.has_value());
        coordinator_.reset();
      })));
  storage::backup::Secret key{};
  key.fill(2u);
  constexpr char kLaterOperation[] = "zz-later-precommit-cancel";
  ASSERT_TRUE(coordinator_->BeginImport(kLaterOperation, key).has_value());
  ASSERT_TRUE(coordinator_->ArmPrecommitCancellationReceipt(
      kLaterOperation, kTarget, base::BindOnce([](Result) {
        ADD_FAILURE() << "Later cancellation ran after coordinator deletion";
      })));

  coordinator_->CancelAll();
  EXPECT_FALSE(coordinator_);
}

TEST_F(ProfileBackupCoordinatorPrecommitCancellationTest,
       InlineTargetCleanupReceiptMayDestroyCoordinator) {
  ASSERT_TRUE(coordinator_->ArmPrecommitCancellationReceipt(
      kOperation, kTarget, base::BindLambdaForTesting([this](Result result) {
        EXPECT_TRUE(result.has_value());
        coordinator_.reset();
      })));
  target_.CompleteCleanupInline(true);
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      base::BindOnce([](ProfileBackupCoordinator::RestorePreviewResult) {})));

  // Source loss settles portable cancellation before the exact target
  // cleanup. The inline cleanup callback may therefore retire this owner
  // from inside BeginTargetCleanup.
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);

  EXPECT_FALSE(coordinator_);
  EXPECT_FALSE(target_.owner_live());
}

}  // namespace
}  // namespace taffy
