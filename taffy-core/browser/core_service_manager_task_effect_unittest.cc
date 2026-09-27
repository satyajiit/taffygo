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

#include "base/functional/callback_helpers.h"
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
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kRevision = 4u;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

service_mojom::CoreStateBrowserBindingsPtr Bindings(
    uint64_t permission_deadline,
    uint64_t permission_deadline_utc,
    const std::string& browser_session_id) {
  auto bindings = service_mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  bindings->task_revisions.push_back(service_mojom::TaskRevisionBinding::New(
      "task-1", kGeneration, kRevision,
      std::vector<service_mojom::TaskControlKind>()));
  bindings->pending_approvals.push_back(
      service_mojom::PendingApprovalBinding::New(
          "task-1", "action-1", std::string(64u, 'a'), kGeneration, kRevision));
  bindings->pending_permissions.push_back(
      service_mojom::PendingPermissionBinding::New(
          "task-1", "permission-1", service_mojom::PlatformPermission::kCamera,
          kGeneration, kRevision, permission_deadline, permission_deadline_utc,
          browser_session_id));
  return bindings;
}

service_mojom::TaskEffectBindingPtr ApprovalEffect(uint64_t deadline) {
  auto effect = service_mojom::TaskEffectBinding::New();
  effect->operation = service_mojom::OperationEnvelope::New(
      "approval-operation", kGeneration, kRevision, deadline,
      "approval-idempotency");
  effect->effect_id = "approval-effect";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = service_mojom::TaskReducerEffectKind::kRequestApproval;
  effect->approval =
      service_mojom::TaskApprovalEffect::New("action-1", std::string(64u, 'a'),
                                             nullptr);
  return effect;
}

service_mojom::TaskEffectBindingPtr PermissionEffect(
    uint64_t deadline,
    uint64_t permission_deadline,
    uint64_t permission_deadline_utc,
    const std::string& browser_session_id) {
  auto effect = service_mojom::TaskEffectBinding::New();
  effect->operation = service_mojom::OperationEnvelope::New(
      "permission-operation", kGeneration, kRevision, deadline,
      "permission-idempotency");
  effect->effect_id = "permission-effect";
  effect->task_id = "task-1";
  effect->ordinal = 1u;
  effect->kind = service_mojom::TaskReducerEffectKind::kRequestPermission;
  effect->permission = service_mojom::TaskPermissionEffect::New(
      "permission-1", service_mojom::PlatformPermission::kCamera,
      permission_deadline, permission_deadline_utc, browser_session_id);
  return effect;
}

service_mojom::CoreStateUpdatePtr State() {
  auto state = service_mojom::CoreStateUpdate::New();
  state->service_generation = kGeneration;
  state->sequence = 1u;
  state->core_status_schema_version = 1u;
  state->payload = std::vector<uint8_t>{1u};
  return state;
}

std::unique_ptr<CoreServiceManager> MakeManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context,
    std::unique_ptr<ProfileToolSupervisor> tools,
    scoped_refptr<CorePageObservationBroker> observation) {
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      std::move(observation),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
}

class RecordingObserver final : public CoreServiceManager::Observer {
 public:
  void OnCoreState(const service_mojom::CoreStateUpdate&) override {
    events.push_back("state");
  }

  void OnCorePermissionRequest(
      const std::string& request_id,
      core_api::mojom::PlatformPermission permission) override {
    EXPECT_EQ(events, std::vector<std::string>({"state"}));
    EXPECT_EQ(request_id, "permission-1");
    EXPECT_EQ(permission, core_api::mojom::PlatformPermission::kCamera);
    events.push_back("permission");
  }

  std::vector<std::string> events;
};

TEST(CoreServiceManagerTaskEffectTest,
     SurfacesCompleteOnlyAfterMatchingStateReachesNativeOwner) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  test::QuietManagerTail tail;
  auto manager =
      MakeManager(tail, context.get(), std::move(tools), observation);
  mojo::PendingReceiver<service_mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);

  RecordingObserver observer;
  manager->AddObserver(&observer);
  const uint64_t now = NowMonotonicMillis();
  const uint64_t operation_deadline = now + 30'000u;
  const uint64_t permission_deadline = now + 20'000u;
  const uint64_t permission_deadline_utc = NowUtcMillis() + 20'000u;
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, Bindings(permission_deadline, permission_deadline_utc,
                                   manager->browser_session_id())),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);

  std::vector<service_mojom::TaskEffectCompletionStatus> completions;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager, ApprovalEffect(operation_deadline),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completions.push_back(result->status);
          }));
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager,
      PermissionEffect(operation_deadline, permission_deadline,
                       permission_deadline_utc, manager->browser_session_id()),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completions.push_back(result->status);
          }));
  EXPECT_TRUE(completions.empty());
  EXPECT_TRUE(observer.events.empty());
  EXPECT_EQ(2u,
            CoreServiceManagerTaskEffectTestPeer::PendingHostTaskEffectCount(
                *manager));

  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, State());
  EXPECT_EQ(observer.events, std::vector<std::string>({"state", "permission"}));
  EXPECT_EQ(completions,
            std::vector<service_mojom::TaskEffectCompletionStatus>(
                2u, service_mojom::TaskEffectCompletionStatus::kSucceeded));
  EXPECT_EQ(0u,
            CoreServiceManagerTaskEffectTestPeer::PendingHostTaskEffectCount(
                *manager));

  manager->RemoveObserver(&observer);
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskEffectTest,
     SurfacesSettleWhenTheirStateWasAlreadyPublished) {
  // The service sends ExecuteTaskEffect and PublishState on one CoreHost pipe,
  // so the browser sees them in send order. This drives the reverse of
  // SurfacesCompleteOnlyAfterMatchingStateReachesNativeOwner: the state that
  // carries the surface has already reached the native owner by the time the
  // effect arrives. There is no later sequence to wait for -- the service will
  // not register one while the effect is outstanding -- so a surface parked
  // here would never be answered and the profile's core service would publish
  // no further state for any task. It must settle now instead.
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  test::QuietManagerTail tail;
  auto manager =
      MakeManager(tail, context.get(), std::move(tools), observation);
  mojo::PendingReceiver<service_mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);

  RecordingObserver observer;
  manager->AddObserver(&observer);
  const uint64_t now = NowMonotonicMillis();
  const uint64_t operation_deadline = now + 30'000u;
  const uint64_t permission_deadline = now + 20'000u;
  const uint64_t permission_deadline_utc = NowUtcMillis() + 20'000u;
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, Bindings(permission_deadline, permission_deadline_utc,
                                   manager->browser_session_id())),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);

  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, State());
  EXPECT_EQ(observer.events, std::vector<std::string>({"state"}));

  std::vector<service_mojom::TaskEffectCompletionStatus> completions;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager, ApprovalEffect(operation_deadline),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completions.push_back(result->status);
          }));
  ASSERT_EQ(completions.size(), 1u);
  EXPECT_EQ(completions.front(),
            service_mojom::TaskEffectCompletionStatus::kSucceeded);

  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager,
      PermissionEffect(operation_deadline, permission_deadline,
                       permission_deadline_utc, manager->browser_session_id()),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completions.push_back(result->status);
          }));
  ASSERT_EQ(completions.size(), 2u);
  EXPECT_EQ(completions.back(),
            service_mojom::TaskEffectCompletionStatus::kSucceeded);
  EXPECT_EQ(0u,
            CoreServiceManagerTaskEffectTestPeer::PendingHostTaskEffectCount(
                *manager));
  EXPECT_EQ(observer.events, std::vector<std::string>({"state", "permission"}));

  manager->RemoveObserver(&observer);
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskEffectTest,
     ApprovalIsUnavailableWithoutAStateObserver) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  test::QuietManagerTail tail;
  auto manager =
      MakeManager(tail, context.get(), std::move(tools), observation);
  mojo::PendingReceiver<service_mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);

  const uint64_t now = NowMonotonicMillis();
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, Bindings(now + 20'000u, NowUtcMillis() + 20'000u,
                                   manager->browser_session_id())),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);
  std::optional<service_mojom::TaskEffectCompletionStatus> completion;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager, ApprovalEffect(now + 30'000u),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completion = result->status;
          }));
  EXPECT_EQ(completion,
            service_mojom::TaskEffectCompletionStatus::kUnavailable);

  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskEffectTest,
     PermissionRequiresCurrentBrowserSessionAndUnexpiredUtcDeadline) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  test::QuietManagerTail tail;
  auto manager =
      MakeManager(tail, context.get(), std::move(tools), observation);
  mojo::PendingReceiver<service_mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);

  const uint64_t now = NowMonotonicMillis();
  const uint64_t now_utc = NowUtcMillis();
  const uint64_t operation_deadline = now + 30'000u;
  const uint64_t permission_deadline = now + 20'000u;
  const uint64_t future_utc = now_utc + 20'000u;

  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, Bindings(permission_deadline, future_utc,
                                   "stale-browser-session")),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);
  std::optional<service_mojom::TaskEffectCompletionStatus> completion;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager,
      PermissionEffect(operation_deadline, permission_deadline, future_utc,
                       "stale-browser-session"),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completion = result->status;
          }));
  EXPECT_EQ(completion, service_mojom::TaskEffectCompletionStatus::kRefused);

  // Bindings belong to the state that is about to be published, so sequence 2
  // only becomes registerable once sequence 1 has actually reached the native
  // owner. Publishing here is not scene-setting: it is the same ordering the
  // service holds itself to, and without it the second registration is stale
  // rather than expired, which is a different refusal from the one under test.
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, State());

  auto expired =
      Bindings(permission_deadline, now_utc, manager->browser_session_id());
  expired->state_sequence = 2u;
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, std::move(expired)),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);
  completion.reset();
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager,
      PermissionEffect(operation_deadline, permission_deadline, now_utc,
                       manager->browser_session_id()),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completion = result->status;
          }));
  EXPECT_EQ(completion, service_mojom::TaskEffectCompletionStatus::kRefused);

  manager->Shutdown();
  static_cast<void>(session);
}

}  // namespace
}  // namespace taffy
