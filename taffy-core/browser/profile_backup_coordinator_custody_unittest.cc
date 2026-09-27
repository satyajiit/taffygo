// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file.h"
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

constexpr char kSourceProfile[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTargetProfile[] = "22222222-2222-4222-8222-222222222222";
constexpr char kInstallation[] = "33333333-3333-4333-8333-333333333333";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestorePlanResultPtr RestorePlan(uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New("restore-plan", generation, 0u,
                                                 NowMonotonicMillis() + 30'000u,
                                                 "restore-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, kTargetProfile);
  auto result = mojom::BackupRestorePlanResult::New();
  result->operation = operation.Clone();
  result->status = mojom::BackupPlanningStatus::kSucceeded;
  result->backup_id = "backup-1";
  result->snapshot_sha256.assign(32u, 2u);
  result->target = target.Clone();
  result->confirmation_sha256.assign(32u, 3u);
  result->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kSourceProfile, std::move(target),
      result->backup_id, result->snapshot_sha256, result->confirmation_sha256);
  return result;
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

class ProfileBackupCoordinatorCustodyTest : public testing::Test {
 protected:
  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    manager_ = MakeManager(tail_, context_.get());
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_,
                                                              kSourceProfile);
    ASSERT_TRUE(profile_.CreateUniqueTempDir());
    stage_store_ = storage::backup::BackupArchiveStageStore::Create(
        profile_.GetPath().Append(
            storage::backup::kBackupStagingDirectoryName));
    ASSERT_TRUE(stage_store_);
    coordinator_ = std::make_unique<ProfileBackupCoordinator>(
        manager_.get(), stage_store_, kInstallation);
  }

  void TearDown() override {
    coordinator_.reset();
    manager_->Shutdown();
    task_environment_.RunUntilIdle();
  }

  void Begin(const std::string& operation_id) {
    storage::backup::Secret key{};
    key.fill(1u);
    ASSERT_TRUE(coordinator_->BeginImport(operation_id, key).has_value());
  }

  base::File Payload() {
    const std::array<uint8_t, 1> bytes = {1u};
    base::FilePath path;
    base::File file =
        base::CreateAndOpenTemporaryFileInDir(profile_.GetPath(), &path);
    if (!file.IsValid() || !file.WriteAtCurrentPosAndCheck(bytes) ||
        !file.Flush()) {
      return base::File();
    }
    return file;
  }

  void Ready(const std::string& operation_id) {
    Begin(operation_id);
    ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetReadyRestore(
        *coordinator_, operation_id, target_.TakeOwner(),
        RestorePlan(manager_->service_generation()), Payload()));
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
  base::ScopedTempDir profile_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_;
  ProfileBackupRestoreTargetTestState target_{kTargetProfile};
  std::unique_ptr<ProfileBackupCoordinator> coordinator_;
};

TEST_F(ProfileBackupCoordinatorCustodyTest,
       CleanupFailureRetainsOwnerAndRetryDoesNotRepeatPortableRpc) {
  constexpr char kOperation[] = "cleanup-retry";
  Ready(kOperation);
  coordinator_->Cancel(kOperation);
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      *coordinator_, kOperation));
  ASSERT_TRUE(target_.cleanup_pending());

  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  target_.CompleteCleanup(false);
  EXPECT_TRUE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::HoldsWorkflowInterest(
      *coordinator_, kOperation));
  EXPECT_EQ(1, target_.cleanup_calls());

  coordinator_->Cancel(kOperation);
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      *coordinator_, kOperation));
  EXPECT_EQ(2, target_.cleanup_calls());
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(true);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorCustodyTest,
       EarlyCancelRetainsTargetUntilCleanupCompletes) {
  constexpr char kOperation[] = "cancel-before-plan";
  Begin(kOperation);
  int callbacks = 0;
  coordinator_->PlanImportedRestore(
      kOperation, target_.TakeOwner(),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestorePreviewResult result) {
            ++callbacks;
            ASSERT_FALSE(result);
            EXPECT_EQ(ProfileBackupError::kCancelled, result.error());
          }));
  coordinator_->Cancel(kOperation);

  EXPECT_EQ(1, callbacks);
  EXPECT_TRUE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      *coordinator_, kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(true);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorCustodyTest,
       EarlyFailureRetainsTargetUntilCleanupCompletes) {
  constexpr char kOperation[] = "failure-before-plan";
  Begin(kOperation);
  int callbacks = 0;
  coordinator_->PlanImportedRestore(
      kOperation, target_.TakeOwner(),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestorePreviewResult result) {
            ++callbacks;
            ASSERT_FALSE(result);
            EXPECT_EQ(ProfileBackupError::kStorageUnavailable, result.error());
          }));
  ProfileBackupCoordinatorTestPeer::DeliverImportFailure(
      *coordinator_, kOperation, ProfileBackupError::kStorageUnavailable);

  EXPECT_EQ(1, callbacks);
  EXPECT_TRUE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      *coordinator_, kOperation));
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(true);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

}  // namespace
}  // namespace taffy
