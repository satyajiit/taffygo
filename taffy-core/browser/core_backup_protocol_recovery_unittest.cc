// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>
#include <vector>

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
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr char kSourceProfile[] = "11111111-1111-4111-8111-111111111111";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

core_mojom::BackupRestoreRecoveryInspectionRequestPtr Request(
    uint64_t generation,
    std::string owner_profile_id = kSourceProfile) {
  auto request = core_mojom::BackupRestoreRecoveryInspectionRequest::New();
  request->operation = core_mojom::OperationEnvelope::New(
      "inspect-recovery", generation, 0u, NowMonotonicMillis() + 30'000u,
      "inspect-recovery-once");
  auto binding = core_mojom::BackupRestoreRecoveryBinding::New();
  binding->reservation_id = "reservation-1";
  binding->owner_profile_id = std::move(owner_profile_id);
  binding->target_kind =
      core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  binding->target_profile_id = "22222222-2222-4222-8222-222222222222";
  binding->backup_id = "backup-1";
  binding->snapshot_sha256.assign(32u, 1u);
  binding->confirmation_sha256.assign(32u, 2u);
  binding->selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  binding->record_count = 1u;
  binding->candidate_records_sha256.assign(32u, 3u);
  auto record = core_mojom::BackupRestoreRecoveryRecord::New();
  record->format_version = 1u;
  record->sequence = 1u;
  record->binding = std::move(binding);
  record->fact_kind =
      core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded;
  record->intent = core_mojom::BackupRestoreRecoveryIntentFact::New(
      "commit-1", core_mojom::BackupRestorePhysicalIntent::kCommitCandidate);
  request->records.push_back(std::move(record));
  return request;
}

// Binds both pipes, because production has both and CoreServiceManager's
// quiescence answer reads both. MakeReady alone leaves the service remote
// unbound, and a manager in that state is never quiescent no matter what it is
// or is not doing — so an EXPECT_TRUE written against it can never pass and an
// EXPECT_FALSE beside it passes without testing anything. The caller keeps the
// receiver alive for the same reason it keeps the session receiver alive.
std::unique_ptr<CoreServiceManager> MakeManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context,
    mojo::PendingReceiver<core_mojom::TaffyCoreService>& service) {
  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto manager = tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(context),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
  service = CoreServiceManagerTaskEffectTestPeer::BindService(*manager);
  return manager;
}

TEST(CoreBackupProtocolRecoveryTest,
     ForeignOwnerAndMalformedHistoryNeverReachTheSourceCore) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  mojo::PendingReceiver<core_mojom::TaffyCoreService> service;
  auto manager = MakeManager(tail, context.get(), service);
  mojo::PendingReceiver<core_mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kSourceProfile);

  auto expect_refused = [&](auto request) {
    int callbacks = 0;
    manager->backup_protocol().InspectBackupRestoreRecovery(
        std::move(request),
        base::BindLambdaForTesting(
            [&](core_mojom::BackupRestoreRecoveryInspectionResultPtr result) {
              ++callbacks;
              ASSERT_TRUE(result);
              EXPECT_EQ(result->status,
                        core_mojom::BackupRestoreRecoveryInspectionStatus::
                            kUnavailable);
              EXPECT_FALSE(result->classification);
              EXPECT_FALSE(result->failure);
            }));
    EXPECT_EQ(1, callbacks);
  };
  expect_refused(Request(manager->service_generation(), "another-source"));
  auto malformed = Request(manager->service_generation());
  malformed->records.front()->intent.reset();
  expect_refused(std::move(malformed));
  EXPECT_TRUE(manager->is_quiescent_for_testing());
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreBackupProtocolRecoveryTest,
     PendingInspectionPinsGenerationAndDuplicateIdentityIsRefused) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  mojo::PendingReceiver<core_mojom::TaffyCoreService> service;
  auto manager = MakeManager(tail, context.get(), service);
  mojo::PendingReceiver<core_mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kSourceProfile);

  int pending_callbacks = 0;
  manager->backup_protocol().InspectBackupRestoreRecovery(
      Request(manager->service_generation()),
      base::BindLambdaForTesting(
          [&](core_mojom::BackupRestoreRecoveryInspectionResultPtr result) {
            ++pending_callbacks;
            ASSERT_TRUE(result);
            EXPECT_EQ(result->status,
                      core_mojom::BackupRestoreRecoveryInspectionStatus::
                          kUnavailable);
            EXPECT_EQ(result->operation->operation_id, "inspect-recovery");
          }));
  EXPECT_EQ(0, pending_callbacks);
  EXPECT_FALSE(manager->is_quiescent_for_testing());

  int duplicate_callbacks = 0;
  manager->backup_protocol().InspectBackupRestoreRecovery(
      Request(manager->service_generation()),
      base::BindLambdaForTesting(
          [&](core_mojom::BackupRestoreRecoveryInspectionResultPtr result) {
            ++duplicate_callbacks;
            ASSERT_TRUE(result);
            EXPECT_EQ(result->status,
                      core_mojom::BackupRestoreRecoveryInspectionStatus::
                          kUnavailable);
          }));
  EXPECT_EQ(1, duplicate_callbacks);
  EXPECT_EQ(0, pending_callbacks);

  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  task_environment.RunUntilIdle();
  EXPECT_EQ(1, pending_callbacks);
  static_cast<void>(session);
}

}  // namespace
}  // namespace taffy
