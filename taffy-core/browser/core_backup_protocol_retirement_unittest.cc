// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
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

class CoreBackupProtocolRetirementTestPeer final {
 public:
  static std::vector<std::string> PendingOperationIds(
      const CoreBackupProtocol& protocol) {
    return {protocol.pending_operation_ids_.begin(),
            protocol.pending_operation_ids_.end()};
  }

  static bool CompleteCancellation(
      CoreBackupProtocol& protocol,
      const std::string& operation_id,
      const core_service::mojom::BackupRestoreBinding& binding,
      core_service::mojom::BackupRestoreProtocolStatus status) {
    const bool removed =
        protocol.pending_operation_ids_.erase(operation_id) == 1u;
    protocol.ObserveBackupRestoreCancellationCompleted(operation_id, binding,
                                                       status);
    return removed;
  }
};

namespace {

namespace core_mojom = core_service::mojom;

constexpr char kSourceProfile[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTargetProfile[] = "22222222-2222-4222-8222-222222222222";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

core_mojom::OperationEnvelopePtr Operation(std::string id,
                                           uint64_t generation) {
  return core_mojom::OperationEnvelope::New(
      id, generation, 0u, NowMonotonicMillis() + 30'000u, id + "-once");
}

core_mojom::BackupRestoreBindingPtr Binding(uint64_t generation) {
  return core_mojom::BackupRestoreBinding::New(
      Operation("restore-plan", generation), kSourceProfile,
      core_mojom::BackupRestoreTarget::New(
          core_mojom::BackupRestoreTargetKind::kNewRegularProfile,
          kTargetProfile),
      "backup-1", std::vector<uint8_t>(32u, 1u), std::vector<uint8_t>(32u, 2u));
}

core_mojom::BackupRestorePlanRequestPtr RestoreRequest(uint64_t generation) {
  auto request = core_mojom::BackupRestorePlanRequest::New();
  request->operation = Operation("new-restore-plan", generation);
  request->manifest_plaintext = {1u};
  request->target = core_mojom::BackupRestoreTarget::New(
      core_mojom::BackupRestoreTargetKind::kNewRegularProfile,
      "33333333-3333-4333-8333-333333333333");
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

class CoreBackupProtocolRetirementTest : public testing::Test {
 protected:
  CoreBackupProtocolRetirementTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    manager_ = MakeManager(tail_, context_.get());
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_,
                                                              kSourceProfile);
  }

  void TearDown() override {
    manager_->Shutdown();
    task_environment_.RunUntilIdle();
  }

  CoreBackupProtocol& protocol() { return manager_->backup_protocol(); }

  std::string OnlyPendingOperation() {
    std::vector<std::string> pending =
        CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol());
    EXPECT_EQ(1u, pending.size());
    return pending.empty() ? std::string() : pending.front();
  }

  void Complete(const std::string& operation_id,
                const core_mojom::BackupRestoreBinding& binding,
                core_mojom::BackupRestoreProtocolStatus status) {
    EXPECT_TRUE(CoreBackupProtocolRetirementTestPeer::CompleteCancellation(
        protocol(), operation_id, binding, status));
  }

  content::BrowserTaskEnvironment task_environment_;
  test::QuietManagerTail tail_;
  std::unique_ptr<content::TestBrowserContext> context_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<core_mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<core_mojom::CoreSession> session_;
};

TEST_F(CoreBackupProtocolRetirementTest,
       ExactBindingIsDeduplicatedAndBlocksNewPlanning) {
  auto binding = Binding(manager_->service_generation());
  ASSERT_TRUE(
      protocol().RetireBackupRestorePlan("retire-one", binding.Clone()));
  const std::string pending = OnlyPendingOperation();
  EXPECT_FALSE(manager_->is_quiescent_for_testing());

  EXPECT_TRUE(protocol().RetireBackupRestorePlan("duplicate", binding.Clone()));
  EXPECT_EQ(
      std::vector<std::string>({pending}),
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol()));

  auto different = binding.Clone();
  different->backup_id = "backup-2";
  EXPECT_FALSE(
      protocol().RetireBackupRestorePlan("replacement", std::move(different)));

  int plan_callbacks = 0;
  protocol().PlanBackupRestore(
      RestoreRequest(manager_->service_generation()),
      base::BindLambdaForTesting(
          [&](core_mojom::BackupRestorePlanResultPtr result) {
            ++plan_callbacks;
            ASSERT_TRUE(result);
            EXPECT_EQ(core_mojom::BackupPlanningStatus::kUnavailable,
                      result->status);
          }));
  EXPECT_EQ(1, plan_callbacks);
  EXPECT_EQ(
      std::vector<std::string>({pending}),
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol()));

  Complete(pending, *binding,
           core_mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_TRUE(manager_->is_quiescent_for_testing());
}

TEST_F(CoreBackupProtocolRetirementTest,
       ExistingExactCancellationIsJoinedWithoutRequestAccumulation) {
  auto binding = Binding(manager_->service_generation());
  auto cancellation_operation =
      Operation("ordinary-cancellation", manager_->service_generation());
  const std::string cancellation_id = cancellation_operation->operation_id;
  auto callbacks = std::make_shared<int>(0);
  protocol().CancelBackupRestoreBeforeCommit(
      core_mojom::BackupRestoreCancellationRequest::New(
          std::move(cancellation_operation), binding.Clone()),
      base::BindLambdaForTesting(
          [callbacks](core_mojom::BackupRestoreProtocolResultPtr) {
            ++*callbacks;
          }));
  EXPECT_EQ(cancellation_id, OnlyPendingOperation());

  ASSERT_TRUE(
      protocol().RetireBackupRestorePlan("join-existing", binding.Clone()));
  task_environment_.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(0, *callbacks);
  EXPECT_EQ(
      std::vector<std::string>({cancellation_id}),
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol()));

  Complete(cancellation_id, *binding,
           core_mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_TRUE(manager_->is_quiescent_for_testing());
}

TEST_F(CoreBackupProtocolRetirementTest,
       RefusalRetriesLaterWithFreshIdentityAndBoundedBackoff) {
  auto binding = Binding(manager_->service_generation());
  ASSERT_TRUE(protocol().RetireBackupRestorePlan("retry", binding.Clone()));
  const std::string first = OnlyPendingOperation();

  Complete(first, *binding,
           core_mojom::BackupRestoreProtocolStatus::kUnavailable);
  EXPECT_TRUE(
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol())
          .empty());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol())
          .empty());
  task_environment_.FastForwardBy(base::Milliseconds(99));
  EXPECT_TRUE(
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol())
          .empty());
  task_environment_.FastForwardBy(base::Milliseconds(1));
  const std::string second = OnlyPendingOperation();
  EXPECT_NE(first, second);

  Complete(second, *binding,
           core_mojom::BackupRestoreProtocolStatus::kInvalidOperation);
  task_environment_.FastForwardBy(base::Milliseconds(199));
  EXPECT_TRUE(
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol())
          .empty());
  task_environment_.FastForwardBy(base::Milliseconds(1));
  const std::string third = OnlyPendingOperation();
  EXPECT_NE(first, third);
  EXPECT_NE(second, third);
  Complete(third, *binding,
           core_mojom::BackupRestoreProtocolStatus::kSucceeded);
  EXPECT_TRUE(manager_->is_quiescent_for_testing());
}

TEST_F(CoreBackupProtocolRetirementTest,
       SourceDisconnectIsTerminalAndDoesNotPinSuccessorGeneration) {
  auto binding = Binding(manager_->service_generation());
  ASSERT_TRUE(
      protocol().RetireBackupRestorePlan("disconnect", binding.Clone()));
  EXPECT_FALSE(manager_->is_quiescent_for_testing());

  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  auto successor_service =
      CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
  auto successor_session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
  EXPECT_TRUE(manager_->is_quiescent_for_testing());
  EXPECT_FALSE(protocol().RetireBackupRestorePlan("stale", std::move(binding)));
  task_environment_.FastForwardBy(base::Minutes(5));
  EXPECT_TRUE(
      CoreBackupProtocolRetirementTestPeer::PendingOperationIds(protocol())
          .empty());
  static_cast<void>(successor_service);
  static_cast<void>(successor_session);
}

}  // namespace
}  // namespace taffy
