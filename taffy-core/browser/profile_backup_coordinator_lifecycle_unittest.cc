// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <utility>

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

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestorePlanResultPtr RestorePlan(uint64_t generation) {
  constexpr char kSourceProfile[] = "11111111-1111-4111-8111-111111111111";
  constexpr char kTargetProfile[] = "22222222-2222-4222-8222-222222222222";
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

TEST(ProfileBackupCoordinatorLifecycleTest,
     ImportDisconnectInvalidatesPreviewAndAbandonsNativeStage) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::TaffyCoreService> service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(
      *manager, "11111111-1111-4111-8111-111111111111");
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  auto stage_store = storage::backup::BackupArchiveStageStore::Create(
      profile.GetPath().Append(storage::backup::kBackupStagingDirectoryName));
  ASSERT_TRUE(stage_store);
  ProfileBackupRestoreTargetTestState target(
      "22222222-2222-4222-8222-222222222222");
  ProfileBackupCoordinator coordinator(manager.get(), stage_store,
                                       "33333333-3333-4333-8333-333333333333");
  storage::backup::Secret key{};
  key.fill(1u);
  ASSERT_TRUE(coordinator.BeginImport("import-disconnect", key).has_value());
  ASSERT_EQ(1u, stage_store->size_for_testing());

  int callback_count = 0;
  std::optional<ProfileBackupError> error;
  coordinator.PlanImportedRestore(
      "import-disconnect", target.TakeOwner(),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestorePreviewResult result) {
            ++callback_count;
            ASSERT_FALSE(result.has_value());
            error = result.error();
          }));
  EXPECT_FALSE(manager->is_quiescent_for_testing());

  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(error);
  EXPECT_EQ(ProfileBackupError::kCoreUnavailable, *error);
  ASSERT_TRUE(target.cleanup_pending());
  target.CompleteCleanup(true);
  task_environment.RunUntilIdle();
  EXPECT_EQ(0u, stage_store->size_for_testing());
  static_cast<void>(service);
  static_cast<void>(session);
}

TEST(ProfileBackupCoordinatorLifecycleTest,
     CancelDuringPlanningConsumesAnyLatePortablePlanBeforeCleanup) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::TaffyCoreService> service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(
      *manager, "11111111-1111-4111-8111-111111111111");
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  auto stage_store = storage::backup::BackupArchiveStageStore::Create(
      profile.GetPath().Append(storage::backup::kBackupStagingDirectoryName));
  ASSERT_TRUE(stage_store);
  ProfileBackupRestoreTargetTestState target(
      "22222222-2222-4222-8222-222222222222");
  ProfileBackupCoordinator coordinator(manager.get(), stage_store,
                                       "33333333-3333-4333-8333-333333333333");
  storage::backup::Secret key{};
  key.fill(1u);
  ASSERT_TRUE(coordinator.BeginImport("cancel-planning", key).has_value());

  int callback_count = 0;
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      coordinator, "cancel-planning", target.TakeOwner(),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestorePreviewResult result) {
            ++callback_count;
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(ProfileBackupError::kCancelled, result.error());
          })));
  coordinator.Cancel("cancel-planning");
  EXPECT_EQ(1, callback_count);
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::HasImport(coordinator,
                                                          "cancel-planning"));

  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      coordinator, "cancel-planning",
      RestorePlan(manager->service_generation()));
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::IsCancelling(
      coordinator, "cancel-planning"));
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::HasImport(coordinator,
                                                          "cancel-planning"));
  ASSERT_TRUE(target.cleanup_pending());
  target.CompleteCleanup(true);
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::HasImport(coordinator,
                                                           "cancel-planning"));
  task_environment.RunUntilIdle();
  EXPECT_EQ(0u, stage_store->size_for_testing());
  static_cast<void>(service);
  static_cast<void>(session);
}

TEST(ProfileBackupCoordinatorLifecycleTest,
     CancelReadyPlanRetainsCustodyUntilPortableCancellationSucceeds) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::TaffyCoreService> service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(
      *manager, "11111111-1111-4111-8111-111111111111");
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  auto stage_store = storage::backup::BackupArchiveStageStore::Create(
      profile.GetPath().Append(storage::backup::kBackupStagingDirectoryName));
  ASSERT_TRUE(stage_store);
  ProfileBackupRestoreTargetTestState target(
      "22222222-2222-4222-8222-222222222222");
  ProfileBackupCoordinator coordinator(manager.get(), stage_store,
                                       "33333333-3333-4333-8333-333333333333");
  storage::backup::Secret key{};
  key.fill(1u);
  ASSERT_TRUE(coordinator.BeginImport("cancel-ready", key).has_value());
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
      coordinator, "cancel-ready", target.TakeOwner(),
      base::BindLambdaForTesting(
          [](ProfileBackupCoordinator::RestorePreviewResult result) {
            EXPECT_TRUE(result.has_value());
          })));
  ProfileBackupCoordinatorTestPeer::DeliverRestorePlan(
      coordinator, "cancel-ready", RestorePlan(manager->service_generation()));

  coordinator.Cancel("cancel-ready");
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::IsCancelling(coordinator,
                                                             "cancel-ready"));
  // Native bytes are abandoned independently of the portable acknowledgement.
  // Drain cleanup before checking it; its timing is not plan-custody evidence.
  task_environment.RunUntilIdle();
  EXPECT_EQ(0u, stage_store->size_for_testing());
  EXPECT_TRUE(
      ProfileBackupCoordinatorTestPeer::HasImport(coordinator, "cancel-ready"));
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::HoldsWorkflowInterest(
      coordinator, "cancel-ready"));
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      coordinator, "cancel-ready",
      mojom::BackupRestoreProtocolStatus::kUnavailable);
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      coordinator, "cancel-ready"));
  EXPECT_TRUE(
      ProfileBackupCoordinatorTestPeer::HasImport(coordinator, "cancel-ready"));
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::HoldsWorkflowInterest(
      coordinator, "cancel-ready"));
  ASSERT_TRUE(target.cleanup_pending());
  target.CompleteCleanup(true);
  coordinator.Cancel("cancel-ready");
  EXPECT_TRUE(ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
      coordinator, "cancel-ready"));
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      coordinator, "cancel-ready",
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(coordinator, "cancel-ready"));
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::HoldsWorkflowInterest(
      coordinator, "cancel-ready"));
  task_environment.RunUntilIdle();
  EXPECT_EQ(0u, stage_store->size_for_testing());
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  static_cast<void>(service);
  static_cast<void>(session);
}

TEST(ProfileBackupCoordinatorLifecycleTest,
     OwnerDestructionRetiresBothHeldAndLateValidatedPlans) {
  // The peer injects an already-validated planner reply. This checks browser
  // callback and process-interest custody, not archive authentication or the
  // portable session's semantic cancellation, which have their own suites.
  // Keep one browser task environment for both scenarios. Recreating its
  // thread executor mid-test made the second RunUntilIdle spin; each scenario
  // independently completed, and sharing this environment preserves both.
  content::BrowserTaskEnvironment task_environment;
  for (bool deliver_before_destruction : {false, true}) {
    SCOPED_TRACE(deliver_before_destruction);
    auto context = std::make_unique<content::TestBrowserContext>();
    test::QuietManagerTail tail;
    auto manager = MakeManager(tail, context.get());
    auto service = CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
    auto session = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
    CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(
        *manager, "11111111-1111-4111-8111-111111111111");
    base::ScopedTempDir profile;
    ASSERT_TRUE(profile.CreateUniqueTempDir());
    auto store = storage::backup::BackupArchiveStageStore::Create(
        profile.GetPath().Append(storage::backup::kBackupStagingDirectoryName));
    ASSERT_TRUE(store);
    ProfileBackupRestoreTargetTestState target(
        "22222222-2222-4222-8222-222222222222");
    auto coordinator = std::make_unique<ProfileBackupCoordinator>(
        manager.get(), store, "33333333-3333-4333-8333-333333333333");
    storage::backup::Secret key{};
    key.fill(1u);
    ASSERT_TRUE(coordinator->BeginImport("owner-destroyed", key).has_value());
    int previews = 0;
    ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetPlanningRestore(
        *coordinator, "owner-destroyed", target.TakeOwner(),
        base::BindLambdaForTesting(
            [&](ProfileBackupCoordinator::RestorePreviewResult result) {
              ++previews;
              EXPECT_TRUE(result.has_value());
            })));
    auto reply = ProfileBackupCoordinatorTestPeer::PendingPlanReply(
        *coordinator, "owner-destroyed");
    if (deliver_before_destruction) {
      std::move(reply).Run(RestorePlan(manager->service_generation()));
    }
    coordinator.reset();
    EXPECT_EQ(!deliver_before_destruction, manager->is_quiescent_for_testing());
    if (!deliver_before_destruction) {
      std::move(reply).Run(RestorePlan(manager->service_generation()));
    }
    // No coordinator or workflow interest remains. The retirement RPC itself
    // must now hold this source generation until its reply or disconnect.
    EXPECT_FALSE(manager->is_quiescent_for_testing());
    EXPECT_EQ(deliver_before_destruction ? 1 : 0, previews);
    manager->Shutdown();
    task_environment.RunUntilIdle();
    EXPECT_EQ(0u, store->size_for_testing());
    static_cast<void>(service);
    static_cast<void>(session);
  }
}

}  // namespace
}  // namespace taffy
