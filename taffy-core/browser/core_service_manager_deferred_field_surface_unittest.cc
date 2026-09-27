// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/callback_helpers.h"
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

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kRevision = 4u;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::CoreStateBrowserBindingsPtr Bindings(uint64_t sequence,
                                            bool include_task) {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = sequence;
  if (include_task) {
    bindings->task_revisions.push_back(
        mojom::TaskRevisionBinding::New("task-1", kGeneration, kRevision,
                                        std::vector<mojom::TaskControlKind>()));
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

mojom::TaskEffectBindingPtr FieldValueEffect() {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      "field-operation", kGeneration, kRevision, NowMonotonicMillis() + 30'000u,
      "field-idempotency");
  effect->effect_id = "field-effect";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = mojom::TaskReducerEffectKind::kRequestFieldValues;
  effect->field_values =
      mojom::TaskFieldValuesEffect::New("field-request-1", "tab-1", "node-1",
                                        std::vector<std::string>());
  return effect;
}

class SilentObserver final : public CoreServiceManager::Observer {};

class CoreServiceManagerDeferredFieldSurfaceTest : public testing::Test {
 protected:
  void SetUp() override {
    auto tools = std::make_unique<ProfileToolSupervisor>(
        kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        &context_, /*storage_broker=*/nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(&context_),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    manager_->AddObserver(&observer_);
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, Bindings(1u, true)),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
    CoreServiceManagerTaskEffectTestPeer::QueueTaskSurface(
        *manager_, FieldValueEffect(), base::DoNothing());
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, State(1u));
    ASSERT_EQ(
        CoreServiceManagerTaskEffectTestPeer::PublishedStateSequence(*manager_),
        1u);
    ASSERT_EQ(
        CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
        1u);
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(
                  *manager_),
              1u);
    ASSERT_FALSE(
        CoreServiceManagerTaskEffectTestPeer::AnyPendingAdmissionWasSent(
            *manager_));
  }

  void TearDown() override {
    manager_->RemoveObserver(&observer_);
    manager_->Shutdown();
  }

  void PublishNext(bool include_task) {
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, Bindings(2u, include_task)),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, State(2u));
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
  test::QuietManagerTail tail_;
  SilentObserver observer_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
};

TEST_F(CoreServiceManagerDeferredFieldSurfaceTest,
       ImmediateAnswerWaitsForTheAdvancedStateSequence) {
  // With no field client, the real coordinator immediately answers zero and
  // closes the presentation. The answer is still retained until the Core
  // publishes its distinct effect-completion witness.
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      1u);
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::AnyPendingAdmissionWasSent(
      *manager_));
  EXPECT_EQ(CoreServiceManagerTaskEffectTestPeer::EmittedFieldValueRequestCount(
                *manager_),
            0u);

  PublishNext(/*include_task=*/true);
  EXPECT_TRUE(CoreServiceManagerTaskEffectTestPeer::AnyPendingAdmissionWasSent(
      *manager_));
}

TEST_F(CoreServiceManagerDeferredFieldSurfaceTest,
       AnswerIsNotReleasedAfterItsTaskDisappears) {
  PublishNext(/*include_task=*/false);
  EXPECT_FALSE(CoreServiceManagerTaskEffectTestPeer::AnyPendingAdmissionWasSent(
      *manager_));
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
      0u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::DeferredTaskSurfaceCount(*manager_),
      0u);
}

}  // namespace
}  // namespace taffy
