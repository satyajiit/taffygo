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
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_operations.h"
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

mojom::BackupManifestPrepareRequestPtr Request(uint64_t generation) {
  auto request = mojom::BackupManifestPrepareRequest::New();
  request->operation = mojom::OperationEnvelope::New(
      "backup-operation", generation, 0u, NowMonotonicMillis() + 30'000u,
      "backup-idempotency");
  request->backup_id = "backup-1";
  request->source_installation_id = "installation-1";
  request->created_at_utc = "2026-09-05T00:00:00Z";
  request->selection = {mojom::BackupRecordKind::kLibraryEntry};
  auto record = mojom::BackupRecordDescriptor::New();
  record->kind = mojom::BackupRecordKind::kLibraryEntry;
  record->stable_id = "library-1";
  record->revision = 1u;
  record->schema_version = 1u;
  record->state = mojom::BackupRecordState::kActive;
  record->plaintext_bytes = 4u;
  record->plaintext_sha256.assign(32u, 1u);
  request->records.push_back(std::move(record));
  return request;
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

TEST(CoreServiceManagerBackupTest,
     InvalidOrDuplicateOperationNeverReachesTheCoreSession) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);

  int first_callbacks = 0;
  manager->backup_protocol().PrepareBackupManifest(
      Request(manager->service_generation()),
      base::BindLambdaForTesting([&](BackupPrepareResult result) {
        ++first_callbacks;
        ASSERT_TRUE(result);
        EXPECT_EQ(result->status, mojom::BackupPlanningStatus::kUnavailable);
        EXPECT_EQ(result->operation->operation_id, "backup-operation");
      }));
  EXPECT_EQ(first_callbacks, 0);

  int duplicate_callbacks = 0;
  manager->backup_protocol().PrepareBackupManifest(
      Request(manager->service_generation()),
      base::BindLambdaForTesting([&](BackupPrepareResult result) {
        ++duplicate_callbacks;
        ASSERT_TRUE(result);
        EXPECT_EQ(result->status, mojom::BackupPlanningStatus::kUnavailable);
      }));
  EXPECT_EQ(duplicate_callbacks, 1);

  int stale_callbacks = 0;
  manager->backup_protocol().PrepareBackupManifest(
      Request(manager->service_generation() + 1u),
      base::BindLambdaForTesting([&](BackupPrepareResult result) {
        ++stale_callbacks;
        ASSERT_TRUE(result);
        EXPECT_EQ(result->status, mojom::BackupPlanningStatus::kUnavailable);
      }));
  EXPECT_EQ(stale_callbacks, 1);

  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  task_environment.RunUntilIdle();
  EXPECT_EQ(first_callbacks, 1);
  static_cast<void>(session);
}

TEST(CoreServiceManagerBackupTest,
     ExpiredOperationIsSettledWithoutOpeningPendingCustody) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  auto request = Request(manager->service_generation());
  request->operation->deadline_monotonic_ms = NowMonotonicMillis();

  int callbacks = 0;
  manager->backup_protocol().PrepareBackupManifest(
      std::move(request),
      base::BindLambdaForTesting([&](BackupPrepareResult result) {
        ++callbacks;
        ASSERT_TRUE(result);
        EXPECT_EQ(result->status, mojom::BackupPlanningStatus::kUnavailable);
      }));
  EXPECT_EQ(callbacks, 1);
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerBackupTest,
     WorkflowInterestPreventsIdleUntilItsOwnerReleasesIt) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::TaffyCoreService> service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  ASSERT_TRUE(manager->is_quiescent_for_testing());

  int disconnects = 0;
  auto interest = manager->backup_protocol().AcquireWorkflowInterest(
      base::BindLambdaForTesting([&] { ++disconnects; }));
  ASSERT_TRUE(interest);
  EXPECT_FALSE(manager->is_quiescent_for_testing());
  EXPECT_EQ(0, disconnects);

  interest.reset();
  EXPECT_TRUE(manager->is_quiescent_for_testing());
  EXPECT_EQ(0, disconnects);
  manager->Shutdown();
  static_cast<void>(service);
  static_cast<void>(session);
}

TEST(CoreServiceManagerBackupTest,
     DisconnectConsumesWorkflowInterestWithoutPinningTheSuccessor) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get());
  mojo::PendingReceiver<mojom::TaffyCoreService> first_service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  mojo::PendingReceiver<mojom::CoreSession> first_session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);

  int disconnects = 0;
  auto stale_interest = manager->backup_protocol().AcquireWorkflowInterest(
      base::BindLambdaForTesting([&] { ++disconnects; }));
  ASSERT_TRUE(stale_interest);
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  EXPECT_EQ(1, disconnects);

  mojo::PendingReceiver<mojom::TaffyCoreService> successor_service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  mojo::PendingReceiver<mojom::CoreSession> successor_session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  EXPECT_TRUE(manager->is_quiescent_for_testing());
  stale_interest.reset();
  EXPECT_TRUE(manager->is_quiescent_for_testing());
  EXPECT_EQ(1, disconnects);

  manager->Shutdown();
  static_cast<void>(first_service);
  static_cast<void>(first_session);
  static_cast<void>(successor_service);
  static_cast<void>(successor_session);
}

}  // namespace
}  // namespace taffy
