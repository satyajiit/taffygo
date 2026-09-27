// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

class CoreServiceManagerCallModelTestPeer final {
public:
  static mojo::PendingReceiver<service_mojom::CoreSession>
  MakeReady(CoreServiceManager &manager) {
    manager.availability_ = CoreServiceManager::Availability::kReady;
    manager.disconnect_handled_ = false;
    return manager.session_.BindNewPipeAndPassReceiver();
  }

  static void SetEffectGeneration(CoreServiceManager &manager,
                                  uint64_t generation) {
    manager.effect_broker_->SetActiveGeneration(generation);
  }

  static service_mojom::PendingApprovalRegistrationStatus
  RegisterBindings(CoreServiceManager &manager,
                   service_mojom::CoreStateBrowserBindingsPtr bindings) {
    service_mojom::PendingApprovalRegistrationStatus status =
        service_mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    manager.RegisterPendingApprovals(
        std::move(bindings),
        base::BindLambdaForTesting(
            [&](service_mojom::PendingApprovalRegistrationStatus result) {
              status = result;
            }));
    return status;
  }

  static void Execute(CoreServiceManager &manager,
                      service_mojom::TaskEffectBindingPtr binding,
                      CoreServiceManager::ExecuteTaskEffectCallback callback) {
    manager.ExecuteTaskEffect(std::move(binding), std::move(callback));
  }
};

namespace {

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kRevision = 4u;
constexpr char kEndpoint[] = "https://provider.taffy.test";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

void JournalIntentSucceeds(const service_mojom::EffectEnvelope &,
                           CoreEffectBroker::JournalCallback done) {
  std::move(done).Run(true);
}

void JournalResultSucceeds(const service_mojom::EffectResult &,
                           CoreEffectBroker::JournalCallback done) {
  std::move(done).Run(true);
}

service_mojom::CoreStateBrowserBindingsPtr TaskRevision() {
  auto bindings = service_mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  bindings->task_revisions.push_back(
      service_mojom::TaskRevisionBinding::New(
          "task-1", kGeneration, kRevision,
          std::vector<service_mojom::TaskControlKind>()));
  return bindings;
}

service_mojom::TaskEffectBindingPtr CallModelEffect(uint64_t deadline) {
  auto request = service_mojom::ModelRequestEffect::New();
  request->route_id = "direct_user_key";
  request->model_id = "fixture-model";
  request->disclosure = service_mojom::DisclosureClass::kUserSelectedContent;
  const std::string body = R"({"messages":[]})";
  request->request_body.assign(body.begin(), body.end());
  request->max_output_bytes = 4096;
  request->task_id = "task-1";
  request->provider_id = "fixture-provider";
  request->wire_api = service_mojom::ProviderWireApi::kAnthropicMessages;
  request->endpoint = kEndpoint;

  auto effect = service_mojom::TaskEffectBinding::New();
  effect->operation = service_mojom::OperationEnvelope::New(
      "model-operation", kGeneration, kRevision, deadline, "model-idempotency");
  effect->effect_id = "model-effect";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = service_mojom::TaskReducerEffectKind::kCallModel;
  effect->model = service_mojom::TaskModelEffect::New("call-1", std::move(request));
  return effect;
}

std::unique_ptr<CoreServiceManager>
MakeManager(test::QuietManagerTail &tail,
            content::TestBrowserContext *context,
            std::unique_ptr<ProfileToolSupervisor> tools,
            scoped_refptr<CorePageObservationBroker> observation,
            CoreEffectBroker::Handlers handlers) {
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      std::move(observation),
      std::make_unique<CoreEffectBroker>(std::move(handlers)));
}

} // namespace

TEST(CoreServiceManagerCallModelTest,
     CallModelIsPerformedThroughTheEffectBroker) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());

  bool dispatched = false;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindRepeating(&JournalIntentSucceeds);
  handlers.commit_result = base::BindRepeating(&JournalResultSucceeds);
  handlers.model = base::BindRepeating(
      [](bool *dispatched, service_mojom::EffectEnvelopePtr effect,
         CoreEffectBroker::CompletionCallback callback) {
        *dispatched = true;
        ASSERT_TRUE(effect);
        ASSERT_TRUE(effect->model_request);
        EXPECT_EQ(effect->kind, service_mojom::EffectKind::kModelRequest);
        EXPECT_EQ(effect->retry_class, service_mojom::RetryClass::kConsequential);
        auto result = service_mojom::EffectResult::New();
        result->operation = effect->operation.Clone();
        result->effect_id = effect->effect_id;
        result->status = service_mojom::EffectStatus::kCompleted;
        result->kind = service_mojom::EffectKind::kModelRequest;
        result->model = service_mojom::ModelEffectResult::New();
        result->model->model_id = effect->model_request->model_id;
        const std::string body = "provider-reply";
        result->model->completion.assign(body.begin(), body.end());
        std::move(callback).Run(std::move(result));
      },
      &dispatched);

  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get(), std::move(tools),
                             observation, std::move(handlers));
  mojo::PendingReceiver<service_mojom::CoreSession> session =
      CoreServiceManagerCallModelTestPeer::MakeReady(*manager);
  CoreServiceManagerCallModelTestPeer::SetEffectGeneration(*manager,
                                                           kGeneration);
  ASSERT_EQ(CoreServiceManagerCallModelTestPeer::RegisterBindings(
                *manager, TaskRevision()),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);

  std::optional<service_mojom::TaskEffectCompletionStatus> status;
  service_mojom::TaskEffectCompletionPtr completion;
  CoreServiceManagerCallModelTestPeer::Execute(
      *manager, CallModelEffect(NowMonotonicMillis() + 30'000u),
      base::BindLambdaForTesting([&](service_mojom::TaskEffectCompletionPtr result) {
        ASSERT_TRUE(result);
        status = result->status;
        completion = std::move(result);
      }));

  EXPECT_TRUE(dispatched);
  EXPECT_EQ(status, service_mojom::TaskEffectCompletionStatus::kSucceeded);
  ASSERT_TRUE(completion);
  ASSERT_TRUE(completion->effect_result);
  ASSERT_TRUE(completion->effect_result->model);
  EXPECT_EQ(completion->effect_result->kind, service_mojom::EffectKind::kModelRequest);
  EXPECT_EQ(completion->effect_result->model->model_id, "fixture-model");
  EXPECT_EQ(std::string(completion->effect_result->model->completion.begin(),
                        completion->effect_result->model->completion.end()),
            "provider-reply");

  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerCallModelTest,
     DefinitiveProviderFailureReachesRustWithoutBecomingPolicy) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());

  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindRepeating(&JournalIntentSucceeds);
  handlers.commit_result = base::BindRepeating(&JournalResultSucceeds);
  handlers.model = base::BindRepeating(
      [](service_mojom::EffectEnvelopePtr effect,
         CoreEffectBroker::CompletionCallback callback) {
        auto result = service_mojom::EffectResult::New();
        result->operation = effect->operation.Clone();
        result->effect_id = effect->effect_id;
        result->status = service_mojom::EffectStatus::kUnavailable;
        result->kind = service_mojom::EffectKind::kModelRequest;
        result->model = service_mojom::ModelEffectResult::New();
        result->model->model_id = effect->model_request->model_id;
        result->model->provider_http_status = 503u;
        result->model->failure = service_mojom::ModelFailure::New(
            service_mojom::ModelErrorClass::kOverloaded, true, 1500u);
        std::move(callback).Run(std::move(result));
      });

  test::QuietManagerTail tail;
  auto manager = MakeManager(tail, context.get(), std::move(tools),
                             observation, std::move(handlers));
  mojo::PendingReceiver<service_mojom::CoreSession> session =
      CoreServiceManagerCallModelTestPeer::MakeReady(*manager);
  CoreServiceManagerCallModelTestPeer::SetEffectGeneration(*manager,
                                                           kGeneration);
  ASSERT_EQ(CoreServiceManagerCallModelTestPeer::RegisterBindings(
                *manager, TaskRevision()),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);

  service_mojom::TaskEffectCompletionPtr completion;
  CoreServiceManagerCallModelTestPeer::Execute(
      *manager, CallModelEffect(NowMonotonicMillis() + 30'000u),
      base::BindLambdaForTesting(
          [&](service_mojom::TaskEffectCompletionPtr result) {
            completion = std::move(result);
          }));

  ASSERT_TRUE(completion);
  EXPECT_EQ(completion->status,
            service_mojom::TaskEffectCompletionStatus::kUnavailable);
  ASSERT_TRUE(completion->effect_result);
  ASSERT_TRUE(completion->effect_result->model);
  ASSERT_TRUE(completion->effect_result->model->failure);
  EXPECT_EQ(completion->effect_result->model->failure->error_class,
            service_mojom::ModelErrorClass::kOverloaded);
  EXPECT_EQ(completion->effect_result->model->failure->retry_after_millis,
            1500u);

  manager->Shutdown();
  static_cast<void>(session);
}

} // namespace taffy
