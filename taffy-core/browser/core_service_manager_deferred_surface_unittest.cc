// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_deferred_task_surface_owner.h"
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

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kRevision = 4u;
constexpr char kTaskId[] = "task-1";
constexpr char kActionId[] = "action-1";
constexpr char kPermissionId[] = "permission-1";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

enum class SurfaceKind { kNone, kApproval, kPermission };

mojom::CoreStateBrowserBindingsPtr Bindings(
    uint64_t sequence,
    SurfaceKind kind,
    uint64_t permission_deadline,
    uint64_t permission_deadline_utc,
    const std::string& browser_session_id,
    bool include_task = true) {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = sequence;
  if (!include_task) {
    return bindings;
  }
  bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
      kTaskId, kGeneration, kRevision, std::vector<mojom::TaskControlKind>()));
  if (kind == SurfaceKind::kApproval) {
    bindings->pending_approvals.push_back(mojom::PendingApprovalBinding::New(
        kTaskId, kActionId, std::string(64u, 'a'), kGeneration, kRevision));
  } else if (kind == SurfaceKind::kPermission) {
    bindings->pending_permissions.push_back(
        mojom::PendingPermissionBinding::New(
            kTaskId, kPermissionId, mojom::PlatformPermission::kCamera,
            kGeneration, kRevision, permission_deadline,
            permission_deadline_utc, browser_session_id));
  }
  return bindings;
}

mojom::CoreStateUpdatePtr State(uint64_t sequence) {
  auto state = mojom::CoreStateUpdate::New();
  state->service_generation = kGeneration;
  state->sequence = sequence;
  state->core_status_schema_version = 1u;
  state->payload = std::vector<uint8_t>{1u};
  return state;
}

mojom::TaskEffectBindingPtr SurfaceEffect(
    SurfaceKind kind,
    uint64_t operation_deadline,
    uint64_t permission_deadline,
    uint64_t permission_deadline_utc,
    const std::string& browser_session_id) {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      kind == SurfaceKind::kApproval ? "approval-effect-operation"
                                     : "permission-effect-operation",
      kGeneration, kRevision, operation_deadline,
      kind == SurfaceKind::kApproval ? "approval-effect-idempotency"
                                     : "permission-effect-idempotency");
  effect->effect_id =
      kind == SurfaceKind::kApproval ? "approval-effect" : "permission-effect";
  effect->task_id = kTaskId;
  effect->ordinal = 0u;
  if (kind == SurfaceKind::kApproval) {
    effect->kind = mojom::TaskReducerEffectKind::kRequestApproval;
    effect->approval = mojom::TaskApprovalEffect::New(
        kActionId, std::string(64u, 'a'), nullptr);
  } else {
    effect->kind = mojom::TaskReducerEffectKind::kRequestPermission;
    effect->permission = mojom::TaskPermissionEffect::New(
        kPermissionId, mojom::PlatformPermission::kCamera, permission_deadline,
        permission_deadline_utc, browser_session_id);
  }
  return effect;
}

class ImmediateSurfaceAnswer final : public CoreServiceManager::Observer {
 public:
  ImmediateSurfaceAnswer(CoreServiceManager* manager, SurfaceKind kind)
      : manager_(manager), kind_(kind) {}

  void OnCoreState(const mojom::CoreStateUpdate& state) override {
    if (state.sequence > 1u) {
      deferred_counts_during_state_callback_.push_back(
          CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(
              *manager_));
    }
    if (kind_ == SurfaceKind::kApproval && state.sequence == 1u &&
        operation_id_.empty()) {
      SubmitApproval();
    }
  }

  void OnCorePermissionRequest(
      const std::string& request_id,
      core_api::mojom::PlatformPermission permission) override {
    EXPECT_EQ(request_id, kPermissionId);
    EXPECT_EQ(permission, core_api::mojom::PlatformPermission::kCamera);
    if (kind_ == SurfaceKind::kPermission && operation_id_.empty()) {
      SubmitPermission();
    }
  }

  std::string SubmitAnotherApproval(
      base::OnceCallback<void(mojom::AdmissionPtr)> callback) {
    CoreApiCommandFactory factory("profile-1", CreateCoreApiEntropySource());
    auto projected = factory.BuildApproveAction(
        kTaskId, kActionId, std::string(64u, 'a'), kRevision, kGeneration,
        NowMonotonicMillis(), NowUtcMillis(), manager_->browser_session_id());
    if (!projected) {
      ADD_FAILURE() << "approval command could not be built";
      return {};
    }
    const std::string operation_id =
        projected->core_service_command->operation->operation_id;
    manager_->Submit(std::move(projected->core_service_command),
                     std::move(callback));
    return operation_id;
  }

  const std::string& operation_id() const { return operation_id_; }
  const std::vector<mojom::AdmissionStatus>& admissions() const {
    return admissions_;
  }
  const std::vector<size_t>& deferred_counts_during_state_callback() const {
    return deferred_counts_during_state_callback_;
  }

 private:
  void SubmitApproval() {
    operation_id_ = SubmitAnotherApproval(
        base::BindLambdaForTesting([this](mojom::AdmissionPtr admission) {
          Record(std::move(admission));
        }));
    if (operation_id_.empty()) {
      ADD_FAILURE() << "approval command was not retained";
    }
  }

  void SubmitPermission() {
    CoreApiCommandFactory factory("profile-1", CreateCoreApiEntropySource());
    auto projected = factory.BuildPermissionResult(
        kTaskId, kPermissionId, core_api::mojom::PlatformPermission::kCamera,
        core_api::mojom::PermissionDecision::kGranted, kRevision, kGeneration,
        NowMonotonicMillis());
    ASSERT_TRUE(projected);
    operation_id_ = projected->core_service_command->operation->operation_id;
    manager_->Submit(
        std::move(projected->core_service_command),
        base::BindLambdaForTesting([this](mojom::AdmissionPtr admission) {
          Record(std::move(admission));
        }));
  }

  void Record(mojom::AdmissionPtr admission) {
    ASSERT_TRUE(admission);
    if (operation_id_.empty()) {
      operation_id_ = admission->operation_id;
    }
    EXPECT_EQ(admission->operation_id, operation_id_);
    admissions_.push_back(admission->status);
  }

  const raw_ptr<CoreServiceManager> manager_;
  const SurfaceKind kind_;
  std::string operation_id_;
  std::vector<mojom::AdmissionStatus> admissions_;
  std::vector<size_t> deferred_counts_during_state_callback_;
};

class CoreServiceManagerDeferredSurfaceTest : public testing::Test {
 protected:
  CoreServiceManagerDeferredSurfaceTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    auto tools = std::make_unique<ProfileToolSupervisor>(
        kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    auto observation =
        base::MakeRefCounted<CorePageObservationBroker>(&context_);
    manager_ = tail_.MakeManager(
        &context_, /*storage_broker=*/nullptr, std::move(tools), observation,
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    CoreServiceManagerTaskEffectTestPeer::BeginBrowserAuthorityGeneration(
        *manager_, "profile-1");
  }

  void TearDown() override { manager_->Shutdown(); }

  void Open(SurfaceKind kind, ImmediateSurfaceAnswer& observer) {
    manager_->AddObserver(&observer);
    const uint64_t now = NowMonotonicMillis();
    permission_deadline_ = now + 20'000u;
    permission_deadline_utc_ = NowUtcMillis() + 20'000u;
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, Bindings(1u, kind, permission_deadline_,
                                      permission_deadline_utc_,
                                      manager_->browser_session_id())),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
    CoreServiceManagerTaskEffectTestPeer::Execute(
        *manager_,
        SurfaceEffect(kind, now + 30'000u, permission_deadline_,
                      permission_deadline_utc_, manager_->browser_session_id()),
        base::BindLambdaForTesting(
            [this](mojom::TaskEffectCompletionPtr result) {
              ASSERT_TRUE(result);
              surface_completions_.push_back(result->status);
            }));
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, State(1u));
  }

  void PublishNext(SurfaceKind kind,
                   bool include_task = true,
                   uint64_t sequence = 2u) {
    ASSERT_EQ(
        CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
            *manager_, Bindings(sequence, kind, permission_deadline_,
                                permission_deadline_utc_,
                                manager_->browser_session_id(), include_task)),
        mojom::PendingApprovalRegistrationStatus::kRegistered);
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, State(sequence));
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
  uint64_t permission_deadline_ = 0u;
  uint64_t permission_deadline_utc_ = 0u;
  std::vector<mojom::TaskEffectCompletionStatus> surface_completions_;
};

TEST_F(CoreServiceManagerDeferredSurfaceTest,
       FastApprovalWaitsForAdvancedExactBindingAndRealAdmission) {
  ImmediateSurfaceAnswer observer(manager_.get(), SurfaceKind::kApproval);
  Open(SurfaceKind::kApproval, observer);
  ASSERT_FALSE(observer.operation_id().empty());
  EXPECT_EQ(surface_completions_,
            std::vector{mojom::TaskEffectCompletionStatus::kSucceeded});
  EXPECT_TRUE(observer.admissions().empty());
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::PendingAdmissionWasSent(
      *manager_, observer.operation_id()));

  CoreDeferredTaskSurfaceOwner::Reconcile(*manager_);
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::PendingAdmissionWasSent(
      *manager_, observer.operation_id()));
  PublishNext(SurfaceKind::kApproval);
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::PendingAdmissionWasSent(
      *manager_, observer.operation_id()));
  EXPECT_TRUE(observer.admissions().empty());

  CoreServiceManagerTaskEffectTestPeer::CompleteAdmission(
      *manager_, observer.operation_id(), mojom::AdmissionStatus::kAccepted);
  EXPECT_EQ(observer.admissions(),
            std::vector{mojom::AdmissionStatus::kAccepted});
  std::optional<mojom::AdmissionStatus> duplicate;
  static_cast<void>(observer.SubmitAnotherApproval(
      base::BindLambdaForTesting([&](mojom::AdmissionPtr admission) {
        ASSERT_TRUE(admission);
        duplicate = admission->status;
      })));
  EXPECT_EQ(duplicate, mojom::AdmissionStatus::kDuplicate);
  EXPECT_EQ(observer.admissions().size(), 1u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(*manager_),
      1u);
  CoreServiceManagerTaskEffectTestPeer::CompleteAdmission(
      *manager_, observer.operation_id(), mojom::AdmissionStatus::kAccepted);
  EXPECT_EQ(observer.admissions().size(), 1u);
  PublishNext(SurfaceKind::kNone, /*include_task=*/false, /*sequence=*/3u);
  ASSERT_FALSE(observer.deferred_counts_during_state_callback().empty());
  EXPECT_EQ(observer.deferred_counts_during_state_callback().back(), 0u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(*manager_),
      0u);
  manager_->RemoveObserver(&observer);
}

TEST_F(CoreServiceManagerDeferredSurfaceTest,
       ASecondDecisionForTheOwnedSurfaceIsRefused) {
  ImmediateSurfaceAnswer observer(manager_.get(), SurfaceKind::kApproval);
  Open(SurfaceKind::kApproval, observer);
  std::optional<mojom::AdmissionStatus> duplicate;
  static_cast<void>(observer.SubmitAnotherApproval(
      base::BindLambdaForTesting([&](mojom::AdmissionPtr admission) {
        ASSERT_TRUE(admission);
        duplicate = admission->status;
      })));
  EXPECT_EQ(duplicate, mojom::AdmissionStatus::kDuplicate);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      1u);
  EXPECT_TRUE(observer.admissions().empty());
  PublishNext(SurfaceKind::kNone, /*include_task=*/false);
  EXPECT_EQ(observer.admissions(),
            std::vector{mojom::AdmissionStatus::kStaleRevision});
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      0u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(*manager_),
      0u);
  manager_->RemoveObserver(&observer);
}

TEST_F(CoreServiceManagerDeferredSurfaceTest,
       ImmediatePermissionResultUsesTheSameBarrier) {
  ImmediateSurfaceAnswer observer(manager_.get(), SurfaceKind::kPermission);
  Open(SurfaceKind::kPermission, observer);
  ASSERT_FALSE(observer.operation_id().empty());
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::PendingAdmissionWasSent(
      *manager_, observer.operation_id()));
  PublishNext(SurfaceKind::kPermission);
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::PendingAdmissionWasSent(
      *manager_, observer.operation_id()));
  EXPECT_TRUE(observer.admissions().empty());
  CoreServiceManagerTaskEffectTestPeer::CompleteAdmission(
      *manager_, observer.operation_id(),
      mojom::AdmissionStatus::kInvalidCommand);
  EXPECT_EQ(observer.admissions(),
            std::vector{mojom::AdmissionStatus::kInvalidCommand});
  manager_->RemoveObserver(&observer);
}

TEST_F(CoreServiceManagerDeferredSurfaceTest,
       BindingDisappearanceFailsClosedExactlyOnce) {
  ImmediateSurfaceAnswer observer(manager_.get(), SurfaceKind::kApproval);
  Open(SurfaceKind::kApproval, observer);
  PublishNext(SurfaceKind::kNone, /*include_task=*/false);
  EXPECT_EQ(observer.admissions(),
            std::vector{mojom::AdmissionStatus::kStaleRevision});
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      0u);
  CoreServiceManagerTaskEffectTestPeer::CompleteAdmission(
      *manager_, observer.operation_id(), mojom::AdmissionStatus::kAccepted);
  EXPECT_EQ(observer.admissions().size(), 1u);
  manager_->RemoveObserver(&observer);
}

TEST_F(CoreServiceManagerDeferredSurfaceTest,
       GenerationLossFailsClosedExactlyOnce) {
  ImmediateSurfaceAnswer observer(manager_.get(), SurfaceKind::kApproval);
  Open(SurfaceKind::kApproval, observer);
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  EXPECT_EQ(manager_->service_generation(), kGeneration + 1u);
  EXPECT_EQ(observer.admissions(),
            std::vector{mojom::AdmissionStatus::kCoreUnavailable});
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      0u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(*manager_),
      0u);
  manager_->RemoveObserver(&observer);
}

TEST_F(CoreServiceManagerDeferredSurfaceTest, DeadlineFailsClosedExactlyOnce) {
  ImmediateSurfaceAnswer observer(manager_.get(), SurfaceKind::kApproval);
  Open(SurfaceKind::kApproval, observer);
  task_environment_.FastForwardBy(base::Seconds(31));
  EXPECT_EQ(observer.admissions(),
            std::vector{mojom::AdmissionStatus::kDeadlineExceeded});
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      0u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(*manager_),
      0u);
  manager_->RemoveObserver(&observer);
}

}  // namespace
}  // namespace taffy
