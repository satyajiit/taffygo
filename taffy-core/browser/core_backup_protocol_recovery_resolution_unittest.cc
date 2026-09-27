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
#include "mojo/public/cpp/bindings/clone_traits.h"
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

core_mojom::BackupRestoreRecoveryBindingPtr Binding(std::string owner) {
  auto output = core_mojom::BackupRestoreRecoveryBinding::New();
  output->reservation_id = "reservation-1";
  output->owner_profile_id = std::move(owner);
  output->target_kind = core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  output->target_profile_id = "22222222-2222-4222-8222-222222222222";
  output->backup_id = "backup-1";
  output->snapshot_sha256.assign(32u, 1u);
  output->confirmation_sha256.assign(32u, 2u);
  output->selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  output->record_count = 1u;
  output->candidate_records_sha256.assign(32u, 3u);
  return output;
}

core_mojom::BackupRestoreRecoveryResolutionRequestPtr Request(
    uint64_t generation,
    std::string owner = kSourceProfile) {
  auto request = core_mojom::BackupRestoreRecoveryResolutionRequest::New();
  request->operation = core_mojom::OperationEnvelope::New(
      "resolve-recovery", generation, 0u, NowMonotonicMillis() + 30'000u,
      "resolve-recovery-once");
  auto binding = Binding(std::move(owner));
  request->history_prefix.push_back(
      core_mojom::BackupRestoreRecoveryRecord::New(
          1u, 1u, binding.Clone(),
          core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded,
          core_mojom::BackupRestoreRecoveryIntentFact::New(
              "commit-1",
              core_mojom::BackupRestorePhysicalIntent::kCommitCandidate),
          nullptr));
  request->history_prefix.push_back(
      core_mojom::BackupRestoreRecoveryRecord::New(
          1u, 2u, std::move(binding),
          core_mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved, nullptr,
          core_mojom::BackupRestoreRecoveryOutcomeFact::New(
              "commit-1",
              core_mojom::BackupRestoreObservedOutcome::kCompleted)));
  request->choice =
      core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate;
  request->intent_id = "discard-1";
  return request;
}

core_mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr Report(
    uint64_t generation) {
  auto request = Request(generation);
  auto authorization =
      core_mojom::BackupRestoreRecoveryResolutionAuthorization::New(
          request->history_prefix.front()->binding.Clone(),
          request->operation.Clone(), request->choice, request->intent_id,
          mojo::Clone(request->history_prefix));
  auto history = mojo::Clone(request->history_prefix);
  history.push_back(core_mojom::BackupRestoreRecoveryRecord::New(
      1u, 3u, authorization->binding.Clone(),
      core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded,
      core_mojom::BackupRestoreRecoveryIntentFact::New(
          "discard-1",
          core_mojom::BackupRestorePhysicalIntent::kDiscardCandidate),
      nullptr));
  return core_mojom::BackupRestoreRecoveryResolutionOutcomeReport::New(
      core_mojom::OperationEnvelope::New("report-recovery", generation, 0u,
                                         NowMonotonicMillis() + 30'000u,
                                         "report-recovery-once"),
      std::move(authorization), std::move(history));
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

TEST(CoreBackupProtocolRecoveryResolutionTest,
     ForeignOrMalformedAuthorityRequestNeverCrossesMojo) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  mojo::PendingReceiver<core_mojom::TaffyCoreService> service;
  auto manager = MakeManager(tail, context.get(), service);
  auto session = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kSourceProfile);

  std::vector<core_mojom::BackupRestoreRecoveryResolutionRequestPtr> requests;
  requests.push_back(Request(manager->service_generation(), "another-source"));
  requests.push_back(Request(manager->service_generation()));
  for (auto& request : requests) {
    if (request->history_prefix.front()->binding->owner_profile_id ==
        kSourceProfile) {
      request->intent_id = "bad\nintent";
    }
    int callbacks = 0;
    manager->backup_protocol().ChooseBackupRestoreRecoveryResolution(
        std::move(request),
        base::BindLambdaForTesting(
            [&](core_mojom::
                    BackupRestoreRecoveryResolutionAuthorizationResultPtr
                        result) {
              ++callbacks;
              ASSERT_TRUE(result);
              EXPECT_EQ(result->status,
                        core_mojom::BackupRestoreProtocolStatus::kUnavailable);
              EXPECT_FALSE(result->authorization);
            }));
    EXPECT_EQ(callbacks, 1);
  }
  EXPECT_TRUE(manager->is_quiescent_for_testing());
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreBackupProtocolRecoveryResolutionTest,
     PendingDecisionPinsGenerationAndSettlesOnDisconnect) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  mojo::PendingReceiver<core_mojom::TaffyCoreService> service;
  auto manager = MakeManager(tail, context.get(), service);
  auto session = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kSourceProfile);

  int callbacks = 0;
  manager->backup_protocol().ChooseBackupRestoreRecoveryResolution(
      Request(manager->service_generation()),
      base::BindLambdaForTesting(
          [&](core_mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
                  result) {
            ++callbacks;
            ASSERT_TRUE(result);
            EXPECT_EQ(result->status,
                      core_mojom::BackupRestoreProtocolStatus::kUnavailable);
          }));
  EXPECT_EQ(callbacks, 0);
  EXPECT_FALSE(manager->is_quiescent_for_testing());
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager);
  task_environment.RunUntilIdle();
  EXPECT_EQ(callbacks, 1);
  static_cast<void>(session);
}

TEST(CoreBackupProtocolRecoveryResolutionTest,
     MalformedOutcomeReportSettlesWithoutCrossingMojo) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  test::QuietManagerTail tail;
  mojo::PendingReceiver<core_mojom::TaffyCoreService> service;
  auto manager = MakeManager(tail, context.get(), service);
  auto session = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kSourceProfile);

  auto report = Report(manager->service_generation());
  report->durable_history.front()->binding->reservation_id = "different";
  int callbacks = 0;
  manager->backup_protocol().ReportBackupRestoreRecoveryResolutionOutcome(
      std::move(report),
      base::BindLambdaForTesting(
          [&](core_mojom::BackupRestoreProtocolResultPtr result) {
            ++callbacks;
            ASSERT_TRUE(result);
            EXPECT_EQ(result->status,
                      core_mojom::BackupRestoreProtocolStatus::kUnavailable);
          }));
  EXPECT_EQ(callbacks, 1);
  EXPECT_TRUE(manager->is_quiescent_for_testing());
  manager->Shutdown();
  static_cast<void>(session);
}

}  // namespace
}  // namespace taffy
