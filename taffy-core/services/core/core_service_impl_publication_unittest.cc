// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace mojom = core_service::mojom;

class CoreServiceImplPublicationTestPeer final {
 public:
  static void InitializeFromBatch(
      CoreServiceImpl& impl,
      mojo::PendingRemote<mojom::CoreHost> host,
      CoreInitializationBatch batch,
      CoreServiceImpl::InitializeCallback callback) {
    impl.generation_ = batch.result->accepted_generation;
    impl.initialize_in_flight_ = true;
    impl.host_.Bind(std::move(host));
    impl.OnInitialized(std::move(callback), std::move(batch));
  }

  static bool Publish(CoreServiceImpl& impl, CoreResponseBatch batch) {
    impl.PublishBatch(std::move(batch));
    return impl.ready_;
  }

  static size_t RetainedItems(const CoreServiceImpl& impl) {
    return impl.publication_queue_.retained_items();
  }

  static size_t RetainedBytes(const CoreServiceImpl& impl) {
    return impl.publication_queue_.retained_bytes();
  }

  static size_t FillSubmissionReservations(CoreServiceImpl& impl) {
    size_t reservations = 0u;
    while (impl.publication_queue_.TryReserveSubmission()) {
      ++reservations;
    }
    return reservations;
  }
};

namespace {

constexpr uint64_t kGeneration = 31u;

mojom::CoreBootstrapPtr ValidBootstrap() {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = kGeneration;
  bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0; index < 32u; ++index) {
    bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(index + 1u);
  }
  bootstrap->browser_profile_id = "publication-profile";
  bootstrap->browser_session_id = "publication-browser-session";
  bootstrap->available_account_methods = {
      mojom::AccountAuthMethod::kGoogle, mojom::AccountAuthMethod::kEmailLink,
      mojom::AccountAuthMethod::kGithub, mojom::AccountAuthMethod::kFacebook};
  return bootstrap;
}

mojom::EffectEnvelopePtr ModelEffect(std::string effect_id,
                                     size_t payload_bytes) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = effect_id + "-operation";
  effect->operation->service_generation = kGeneration;
  effect->operation->deadline_monotonic_ms =
      (base::TimeTicks::Now().since_origin() + base::Minutes(1))
          .InMilliseconds();
  effect->operation->idempotency_key = effect_id + "-key";
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kModelRequest;
  effect->retry_class = mojom::RetryClass::kNever;
  effect->model_request = mojom::ModelRequestEffect::New();
  effect->model_request->route_id = "managed";
  effect->model_request->model_id = "model";
  effect->model_request->request_body.resize(payload_bytes, 0x61u);
  effect->model_request->max_output_bytes = 1u;
  effect->model_request->provider_id = "provider";
  effect->model_request->endpoint = "https://edge.invalid/model";
  return effect;
}

CoreResponseBatch PublicationBatch(std::string effect_id,
                                   size_t payload_bytes,
                                   bool answer_first) {
  CoreResponseBatch batch;
  batch.effects.push_back(ModelEffect(effect_id, payload_bytes));
  if (answer_first) {
    auto event = mojom::TaskAnswerEvent::New();
    event->task_id = "task-1";
    event->call_id = "call-1";
    event->sequence = 0u;
    event->text = "answer";
    batch.task_answer_events.push_back(std::move(event));
  }
  return batch;
}

CoreResponseBatch StateAndTaskEffectBatch() {
  CoreResponseBatch batch;
  CoreStatePublication state;
  state.state = mojom::CoreStateUpdate::New();
  state.state->payload = {1u, 2u, 3u};
  state.state->service_generation = kGeneration;
  state.state->sequence = 1u;
  state.browser_bindings = mojom::CoreStateBrowserBindings::New();
  state.browser_bindings->service_generation = kGeneration;
  state.browser_bindings->state_sequence = 1u;
  state.browser_bindings->task_revisions.push_back(
      mojom::TaskRevisionBinding::New("task-1", kGeneration, 1u,
                                      std::vector<mojom::TaskControlKind>()));
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "task-operation";
  effect->operation->service_generation = kGeneration;
  effect->operation->task_revision = 1u;
  effect->operation->deadline_monotonic_ms =
      (base::TimeTicks::Now().since_origin() + base::Minutes(1))
          .InMilliseconds();
  effect->effect_id = "task-effect";
  effect->task_id = "task-1";
  effect->kind = mojom::TaskReducerEffectKind::kReconcileAction;
  effect->reconcile = mojom::TaskReconcileEffect::New(
      "action-1", mojom::TaskRecoveryRule::kReconcileFirst, "dispatch-1",
      mojom::TaskActionOperationKind::kDomClick);
  state.task_effects.push_back(std::move(effect));
  batch.states.push_back(std::move(state));
  return batch;
}

mojom::CoreServiceCommandPtr Command(std::string operation_id) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New();
  command->operation->operation_id = std::move(operation_id);
  command->operation->service_generation = kGeneration;
  command->operation->deadline_monotonic_ms = 60'000u;
  command->operation->idempotency_key = "submission-key";
  return command;
}

class HoldingCoreHost final : public mojom::CoreHost {
 public:
  mojo::PendingRemote<mojom::CoreHost> BindNewPipe() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  const std::vector<std::string>& effects() const { return effects_; }
  const std::vector<std::string>& events() const { return events_; }
  bool has_held_answer() const { return !answer_callback_.is_null(); }

  void ReleaseAnswer(bool accepted) {
    ASSERT_FALSE(answer_callback_.is_null());
    std::move(answer_callback_).Run(accepted);
  }

 private:
  void RegisterCapability(mojom::MintedCapabilityGrantPtr grant,
                          RegisterCapabilityCallback callback) override {
    std::move(callback).Run(mojom::CapabilityRegistrationStatus::kRegistered);
  }
  void RegisterPendingApprovals(
      mojom::CoreStateBrowserBindingsPtr bindings,
      RegisterPendingApprovalsCallback callback) override {
    events_.push_back("register");
    std::move(callback).Run(
        mojom::PendingApprovalRegistrationStatus::kRegistered);
  }
  void EvaluateTaskPolicy(mojom::TaskPolicyEffectPtr effect,
                          EvaluateTaskPolicyCallback callback) override {
    std::move(callback).Run(mojom::PolicyEvaluationResult::New());
  }
  void ExecuteTaskEffect(mojom::TaskEffectBindingPtr effect,
                         ExecuteTaskEffectCallback callback) override {
    events_.push_back(effect->effect_id);
    task_callback_ = std::move(callback);
  }
  void EmitEffect(mojom::EffectEnvelopePtr effect) override {
    effects_.push_back(effect ? effect->effect_id : std::string());
  }
  void PublishTaskAnswerEvents(
      std::vector<mojom::TaskAnswerEventPtr> events,
      PublishTaskAnswerEventsCallback callback) override {
    ASSERT_TRUE(answer_callback_.is_null());
    answer_callback_ = std::move(callback);
  }
  void PublishState(mojom::CoreStateUpdatePtr update) override {
    events_.push_back("state");
  }

  std::vector<std::string> effects_;
  std::vector<std::string> events_;
  ExecuteTaskEffectCallback task_callback_;
  PublishTaskAnswerEventsCallback answer_callback_;
  mojo::Receiver<mojom::CoreHost> receiver_{this};
};

class CoreServiceImplPublicationTest : public testing::Test {
 protected:
  CoreServiceImplPublicationTest()
      : impl_(service_.BindNewPipeAndPassReceiver()) {}

  void InitializeRecovery() {
    bool initialized = false;
    CoreInitializationBatch batch;
    batch.result = mojom::CoreBootstrapResult::New(
        mojom::InitializationStatus::kReady, kGeneration);
    batch.states = std::move(StateAndTaskEffectBatch().states);
    batch.states.front().effects.push_back(
        ModelEffect("bootstrap-effect", 16u));
    CoreServiceImplPublicationTestPeer::InitializeFromBatch(
        impl_, host_.BindNewPipe(), std::move(batch),
        base::BindLambdaForTesting([&](mojom::CoreBootstrapResultPtr result) {
          initialized =
              result && result->status == mojom::InitializationStatus::kReady;
        }));
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(initialized);
  }

  void OpenReadySession() {
    mojom::InitializationStatus status =
        mojom::InitializationStatus::kInvalidBootstrap;
    service_->Initialize(ValidBootstrap(), host_.BindNewPipe(),
                         base::BindLambdaForTesting(
                             [&status](mojom::CoreBootstrapResultPtr result) {
                               if (result) {
                                 status = result->status;
                               }
                             }));
    task_environment_.RunUntilIdle();
    ASSERT_EQ(mojom::InitializationStatus::kReady, status);
    service_->OpenSession("publication-session",
                          session_.BindNewPipeAndPassReceiver());
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(session_.is_connected());
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  mojo::Remote<mojom::CoreSession> session_;
  HoldingCoreHost host_;
  CoreServiceImpl impl_;
};

TEST(CorePublicationQueueTest, LaneTransfersRetainOneSharedReservation) {
  CorePublicationQueue queue;
  ASSERT_TRUE(queue.TryPushBatch(StateAndTaskEffectBatch()));
  const size_t batch_items = queue.retained_items();
  const size_t batch_bytes = queue.retained_bytes();

  CoreResponseBatch batch = queue.TakeBatchAndRetainStates();
  EXPECT_TRUE(batch.states.empty());
  ASSERT_TRUE(queue.HasStates());
  EXPECT_LT(queue.retained_items(), batch_items);
  // This fixture has no batch-local byte payload, so transferring its state
  // retains the exact byte reservation once while releasing wrapper items.
  EXPECT_EQ(batch_bytes, queue.retained_bytes());
  const size_t state_items = queue.retained_items();
  const size_t state_bytes = queue.retained_bytes();

  CoreStatePublication state = queue.TakeStateAndRetainTaskEffects();
  EXPECT_TRUE(state.task_effects.empty());
  ASSERT_TRUE(queue.HasTaskEffects());
  EXPECT_LT(queue.retained_items(), state_items);
  EXPECT_LT(queue.retained_bytes(), state_bytes);
  EXPECT_TRUE(queue.TakeTaskEffect());
  EXPECT_EQ(0u, queue.retained_items());
  EXPECT_EQ(0u, queue.retained_bytes());
}

TEST_F(CoreServiceImplPublicationTest,
       RestoredReconciliationWaitsForSessionBeforeItsState) {
  ASSERT_NO_FATAL_FAILURE(InitializeRecovery());
  EXPECT_EQ((std::vector<std::string>{"register"}), host_.events());
  EXPECT_TRUE(host_.effects().empty());
  EXPECT_GT(CoreServiceImplPublicationTestPeer::RetainedItems(impl_), 0u);
  service_->OpenSession("recovery-session",
                        session_.BindNewPipeAndPassReceiver());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(session_.is_connected());
  EXPECT_EQ((std::vector<std::string>{"bootstrap-effect"}), host_.effects());
  EXPECT_EQ((std::vector<std::string>{"register", "task-effect", "state"}),
            host_.events());
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedItems(impl_));
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedBytes(impl_));
}

TEST_F(CoreServiceImplPublicationTest,
       ShutdownBeforeSessionWithdrawsRestoredPublication) {
  ASSERT_NO_FATAL_FAILURE(InitializeRecovery());
  bool accepted = false;
  service_->PrepareForShutdown(base::BindLambdaForTesting(
      [&accepted](bool value) { accepted = value; }));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(accepted);
  service_->OpenSession("recovery-session",
                        session_.BindNewPipeAndPassReceiver());
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(session_.is_connected());
  EXPECT_TRUE(host_.effects().empty());
  EXPECT_EQ((std::vector<std::string>{"register"}), host_.events());
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedItems(impl_));
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedBytes(impl_));
}

TEST(CorePublicationQueueTest, WireSizingDoesNotConsumeQueuedPayload) {
  CorePublicationQueue queue;
  ASSERT_TRUE(queue.TryPushBatch(PublicationBatch("sized-effect", 37u, false)));

  const CoreResponseBatch& queued = queue.FrontBatch();
  ASSERT_EQ(1u, queued.effects.size());
  ASSERT_TRUE(queued.effects.front());
  ASSERT_TRUE(queued.effects.front()->model_request);
  EXPECT_EQ(37u, queued.effects.front()->model_request->request_body.size());
}

TEST(CorePublicationQueueTest,
     SubmittedBatchCannotConsumeRemainingReservationShares) {
  CorePublicationQueue queue;
  ASSERT_TRUE(queue.TryReserveSubmission());
  ASSERT_TRUE(queue.TryReserveSubmission());
  ASSERT_TRUE(queue.TryReserveSubmission());

  constexpr size_t kRemainingReservations = 2u;
  const size_t oversized_payload =
      CorePublicationQueue::kMaxRetainedBytes -
      kRemainingReservations *
          CorePublicationQueue::kSubmissionReservationBytes +
      1u;
  EXPECT_FALSE(queue.TryPushSubmittedBatch(
      PublicationBatch("oversized-return", oversized_payload, false)));
  EXPECT_EQ(kRemainingReservations, queue.submission_reservations());
  EXPECT_EQ(kRemainingReservations *
                CorePublicationQueue::kSubmissionReservationBytes,
            queue.retained_bytes());

  constexpr size_t kFairPayload =
      CorePublicationQueue::kSubmissionReservationBytes / 2u;
  CorePublicationQueue measurement;
  ASSERT_TRUE(measurement.TryPushBatch(
      PublicationBatch("fair-return-a", kFairPayload, false)));
  ASSERT_LE(measurement.retained_bytes(),
            CorePublicationQueue::kSubmissionReservationBytes);
  EXPECT_TRUE(queue.TryPushSubmittedBatch(
      PublicationBatch("fair-return-a", kFairPayload, false)));
  EXPECT_TRUE(queue.TryPushSubmittedBatch(
      PublicationBatch("fair-return-b", kFairPayload, false)));
}

TEST_F(CoreServiceImplPublicationTest,
       HeldAnswerDrainsEveryLaterBatchInOriginalOrder) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());

  EXPECT_TRUE(CoreServiceImplPublicationTestPeer::Publish(
      impl_, PublicationBatch("effect-a", 16u, true)));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(host_.has_held_answer());
  EXPECT_TRUE(CoreServiceImplPublicationTestPeer::Publish(
      impl_, PublicationBatch("effect-b", 16u, false)));
  EXPECT_TRUE(CoreServiceImplPublicationTestPeer::Publish(
      impl_, PublicationBatch("effect-c", 16u, false)));
  EXPECT_TRUE(host_.effects().empty());

  host_.ReleaseAnswer(true);
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<std::string>{"effect-a", "effect-b", "effect-c"}),
            host_.effects());
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedItems(impl_));
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedBytes(impl_));
}

TEST_F(CoreServiceImplPublicationTest,
       HeldAnswerOverflowFailsClosedAndWithdrawsQueuedEffects) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());
  constexpr size_t kPayloadBytes = 700u * 1024u;

  ASSERT_TRUE(CoreServiceImplPublicationTestPeer::Publish(
      impl_, PublicationBatch("effect-held", kPayloadBytes, true)));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(host_.has_held_answer());
  size_t accepted = 1u;
  while (CoreServiceImplPublicationTestPeer::Publish(
      impl_, PublicationBatch("effect-queued-" + std::to_string(accepted),
                              kPayloadBytes, false))) {
    ++accepted;
    ASSERT_LT(accepted, 16u);
    EXPECT_LE(CoreServiceImplPublicationTestPeer::RetainedItems(impl_),
              CorePublicationQueue::kMaxRetainedItems);
    EXPECT_LE(CoreServiceImplPublicationTestPeer::RetainedBytes(impl_),
              CorePublicationQueue::kMaxRetainedBytes);
  }

  EXPECT_LT(accepted, 16u);
  EXPECT_TRUE(host_.effects().empty());
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedItems(impl_));
  EXPECT_EQ(0u, CoreServiceImplPublicationTestPeer::RetainedBytes(impl_));
  host_.ReleaseAnswer(true);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(host_.effects().empty());
}

TEST_F(CoreServiceImplPublicationTest,
       SubmissionReservationsReturnBackpressureBeforeRuntimeMutation) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());
  EXPECT_EQ(
      mojom::kMaxInFlightPerProfile,
      CoreServiceImplPublicationTestPeer::FillSubmissionReservations(impl_));
  mojom::AdmissionStatus status = mojom::AdmissionStatus::kAccepted;

  session_->Submit(
      Command("backpressured-operation"),
      base::BindLambdaForTesting([&status](mojom::AdmissionPtr admission) {
        ASSERT_TRUE(admission);
        status = admission->status;
      }));
  task_environment_.RunUntilIdle();

  EXPECT_EQ(mojom::AdmissionStatus::kBackpressure, status);
  EXPECT_TRUE(host_.effects().empty());
}

}  // namespace
}  // namespace taffy
