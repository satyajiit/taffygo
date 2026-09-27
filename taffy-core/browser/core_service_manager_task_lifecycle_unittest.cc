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
namespace {

namespace service_mojom = core_service::mojom;

constexpr uint64_t kGeneration = 1u;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

service_mojom::EffectResultPtr AmbiguousRefreshResult(uint64_t generation) {
  auto result = service_mojom::EffectResult::New();
  result->operation = service_mojom::OperationEnvelope::New(
      "account-refresh", generation, 0u, NowMonotonicMillis() + 30'000u,
      "account-refresh-key");
  result->effect_id = "account-refresh-effect";
  result->status = service_mojom::EffectStatus::kOutcomeUnknown;
  result->kind = service_mojom::EffectKind::kNetworkRequest;
  result->network = service_mojom::NetworkEffectResult::New();
  result->network->operation_kind =
      service_mojom::AccountNetworkOperation::kRefreshSession;
  result->network->refreshed_session =
      service_mojom::AccountSessionReceipt::New();
  return result;
}

service_mojom::CoreStateBrowserBindingsPtr SettlementBindings(
    uint64_t sequence,
    bool has_settlement) {
  auto bindings = service_mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = sequence;
  if (has_settlement) {
    bindings->task_revisions.push_back(service_mojom::TaskRevisionBinding::New(
        "task-1", kGeneration, 7u,
        std::vector<service_mojom::TaskControlKind>()));
    bindings->task_settlements.push_back(
        service_mojom::TaskSettlementBinding::New(
            "task-1", kGeneration, 7u,
            service_mojom::TaskSettlementKind::kCancel));
  }
  return bindings;
}

service_mojom::CoreStateUpdatePtr State(uint64_t sequence) {
  auto state = service_mojom::CoreStateUpdate::New();
  state->service_generation = kGeneration;
  state->sequence = sequence;
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

class RecordingAnswerObserver final : public CoreServiceManager::Observer {
 public:
  void OnTaskAnswerDelta(const std::string& task_id,
                         const std::string& call_id,
                         uint32_t sequence,
                         const std::optional<std::string>& text,
                         bool terminal,
                         bool complete) override {
    events.push_back(service_mojom::TaskAnswerEvent::New(
        task_id, call_id, sequence, text, terminal, complete));
  }

  std::vector<service_mojom::TaskAnswerEventPtr> events;
};

TEST(CoreServiceManagerTaskLifecycleTest,
     PublishesOnlyAtomicContiguousAnswerBatches) {
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

  RecordingAnswerObserver observer;
  manager->AddObserver(&observer);
  std::vector<service_mojom::TaskAnswerEventPtr> first;
  first.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 0u, std::optional<std::string>("Hel"), false, false));
  first.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 1u, std::optional<std::string>("lo"), false, false));
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(first)));
  ASSERT_EQ(observer.events.size(), 2u);

  std::vector<service_mojom::TaskAnswerEventPtr> malformed;
  malformed.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 2u, std::optional<std::string>("!"), false, false));
  malformed.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 4u, std::nullopt, true, true));
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(malformed)));
  EXPECT_EQ(observer.events.size(), 2u);

  std::vector<service_mojom::TaskAnswerEventPtr> terminal;
  terminal.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 2u, std::nullopt, true, true));
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(terminal)));
  ASSERT_EQ(observer.events.size(), 3u);
  EXPECT_TRUE(observer.events.back()->terminal);
  EXPECT_TRUE(observer.events.back()->complete);

  std::vector<service_mojom::TaskAnswerEventPtr> replay;
  replay.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 2u, std::nullopt, true, true));
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(replay)));
  EXPECT_EQ(observer.events.size(), 3u);

  manager->RemoveObserver(&observer);
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskLifecycleTest,
     AnswerBatchOverlayReusesCapacityWithoutCommittingARejectedPrefix) {
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

  for (size_t index = 0; index < service_mojom::kMaxPendingTaskPolicyPerProfile;
       ++index) {
    std::vector<service_mojom::TaskAnswerEventPtr> opening;
    opening.push_back(service_mojom::TaskAnswerEvent::New(
        "task-" + std::to_string(index), "call-" + std::to_string(index), 0u,
        std::optional<std::string>("open"), false, false));
    ASSERT_TRUE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
        *manager, std::move(opening)));
  }
  EXPECT_EQ(
      service_mojom::kMaxPendingTaskPolicyPerProfile,
      CoreServiceManagerTaskEffectTestPeer::ActiveTaskAnswerCount(*manager));

  std::vector<service_mojom::TaskAnswerEventPtr> replace_at_capacity;
  replace_at_capacity.push_back(service_mojom::TaskAnswerEvent::New(
      "task-0", "call-0", 1u, std::nullopt, true, true));
  replace_at_capacity.push_back(service_mojom::TaskAnswerEvent::New(
      "task-new", "call-new", 0u, std::optional<std::string>("open"), false,
      false));
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(replace_at_capacity)));
  EXPECT_EQ(
      service_mojom::kMaxPendingTaskPolicyPerProfile,
      CoreServiceManagerTaskEffectTestPeer::ActiveTaskAnswerCount(*manager));

  std::vector<service_mojom::TaskAnswerEventPtr> malformed;
  malformed.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 1u, std::nullopt, true, true));
  malformed.push_back(service_mojom::TaskAnswerEvent::New(
      "task-invalid", "call-invalid", 1u, std::optional<std::string>("gap"),
      false, false));
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(malformed)));
  EXPECT_EQ(
      service_mojom::kMaxPendingTaskPolicyPerProfile,
      CoreServiceManagerTaskEffectTestPeer::ActiveTaskAnswerCount(*manager));

  std::vector<service_mojom::TaskAnswerEventPtr> terminal_after_rejection;
  terminal_after_rejection.push_back(service_mojom::TaskAnswerEvent::New(
      "task-1", "call-1", 1u, std::nullopt, true, true));
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::PublishAnswers(
      *manager, std::move(terminal_after_rejection)));
  EXPECT_EQ(
      service_mojom::kMaxPendingTaskPolicyPerProfile - 1u,
      CoreServiceManagerTaskEffectTestPeer::ActiveTaskAnswerCount(*manager));

  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskLifecycleTest,
     AmbiguousAccountMutationRetiresGenerationBeforeFurtherWork) {
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

  CoreServiceManagerTaskEffectTestPeer::CompleteEffect(
      *manager, kGeneration, AmbiguousRefreshResult(kGeneration));

  EXPECT_EQ(kGeneration + 1u, manager->service_generation());
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager->availability());
  const uint64_t late_before = manager->late_reply_count_for_testing();
  CoreServiceManagerTaskEffectTestPeer::CompleteEffect(
      *manager, kGeneration, AmbiguousRefreshResult(kGeneration));
  EXPECT_EQ(late_before + 1u, manager->late_reply_count_for_testing());
  EXPECT_EQ(kGeneration + 1u, manager->service_generation());

  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskLifecycleTest,
     SettlementReplayKeysFollowTheAuthoritativeBindingSnapshot) {
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

  EXPECT_EQ(service_mojom::PendingApprovalRegistrationStatus::kRegistered,
            CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, SettlementBindings(1u, true)));
  EXPECT_EQ(1u,
            CoreServiceManagerTaskEffectTestPeer::CompletedTaskSettlementCount(
                *manager));

  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, State(1u));
  EXPECT_EQ(service_mojom::PendingApprovalRegistrationStatus::kRegistered,
            CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, SettlementBindings(2u, false)));
  EXPECT_EQ(0u,
            CoreServiceManagerTaskEffectTestPeer::CompletedTaskSettlementCount(
                *manager));

  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerTaskLifecycleTest,
     FieldValueEmissionIdentityIsReleasedOnImmediateTerminal) {
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

  for (uint32_t ordinal = 0u; ordinal < 100u; ++ordinal) {
    CoreServiceManagerTaskEffectTestPeer::OpenFieldValueRequestWithoutSurface(
        *manager, "field-request-" + std::to_string(ordinal));
    EXPECT_EQ(
        0u, CoreServiceManagerTaskEffectTestPeer::EmittedFieldValueRequestCount(
                *manager));
  }

  manager->Shutdown();
  static_cast<void>(session);
}

}  // namespace
}  // namespace taffy
