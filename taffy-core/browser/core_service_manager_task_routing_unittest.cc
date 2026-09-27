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
#include "taffy/browser/core_task_policy_test_support.h"
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

mojom::CoreStateBrowserBindingsPtr Bindings() {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
      "task-1", kGeneration, kRevision, std::vector<mojom::TaskControlKind>()));
  return bindings;
}

mojom::TaskEffectBindingPtr MemoryEffect(uint64_t deadline) {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation =
      mojom::OperationEnvelope::New("memory-operation", kGeneration, kRevision,
                                    deadline, "memory-idempotency");
  effect->effect_id = "memory-effect";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = mojom::TaskReducerEffectKind::kRunMemoryTool;
  effect->memory_tool = mojom::TaskMemoryToolEffect::New(
      "action-1", mojom::TaskActionOperationKind::kMemorySearch);
  return effect;
}

mojom::TaskEffectBindingPtr ReconcileEffect(uint64_t deadline) {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      "reconcile-operation", kGeneration, kRevision, deadline,
      "reconcile-idempotency");
  effect->effect_id = "reconcile-effect";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = mojom::TaskReducerEffectKind::kReconcileAction;
  effect->reconcile = mojom::TaskReconcileEffect::New(
      "action-1", mojom::TaskRecoveryRule::kReconcileFirst, "dispatch-1",
      mojom::TaskActionOperationKind::kDomClick);
  return effect;
}

mojom::TaskEffectBindingPtr ExactNodeEffect(
    uint64_t deadline,
    mojom::PolicyActionClass action_class) {
  const bool focus = action_class == mojom::PolicyActionClass::kMoveFocus;
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      focus ? "focus-operation" : "activation-operation", kGeneration,
      kRevision, deadline,
      focus ? "focus-idempotency" : "activation-idempotency");
  effect->effect_id = focus ? "focus-effect" : "activation-effect";
  effect->task_id = "task-1";
  effect->ordinal = 1u;
  effect->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  effect->action = mojom::TaskActionEffect::New();
  effect->action->action_id = "action-1";
  effect->action->proposal_digest = std::string(64u, 'a');
  effect->action->idempotency_key = effect->operation->idempotency_key;
  effect->action->capability_id = "capability-1";
  effect->action->dispatch_id = "dispatch-1";
  effect->action->document = mojom::TaskFrozenDocument::New(
      "frame-1", "epoch-1", 12u, "https://example.test", std::nullopt);
  effect->action->executable = mojom::TaskExecutableAction::New();
  effect->action->executable->action_class = action_class;
  effect->action->executable->operation_kind =
      focus ? mojom::TaskActionOperationKind::kDomFocus
            : mojom::TaskActionOperationKind::kDomClick;
  effect->action->executable->tool_name =
      focus ? "browser.dom.focus" : "browser.dom.click";
  effect->action->executable->canonical_intent =
      focus ? core_task_policy_test::DomFocusIntent("node-1")
            : core_task_policy_test::DomActivationIntent("node-1", true);
  effect->action->executable->input = mojom::TaskActionInput::New();
  effect->action->executable->input->kind = mojom::TaskActionInputKind::kNone;
  effect->action->executable->tab_id = "tab-1";
  effect->action->executable->node_id = "node-1";
  effect->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
      mojom::TaskActionPrecondition::kNodePresent,
  };
  effect->action->postcondition =
      mojom::TaskActionPostcondition::kNodeStateChanged;
  return effect;
}

mojom::TaskEffectBindingPtr ObservationEffect() {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      "observation-operation", kGeneration, kRevision,
      NowMonotonicMillis() + 30'000u, "observation-idempotency");
  effect->effect_id = "observation-effect";
  effect->task_id = "task-1";
  effect->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  effect->action = mojom::TaskActionEffect::New();
  effect->action->action_id = "observe-action";
  effect->action->document = mojom::TaskFrozenDocument::New(
      "frame-1", "epoch-1", 12u, "http://localhost:8765", std::nullopt);
  effect->action->executable = mojom::TaskExecutableAction::New();
  effect->action->executable->action_class =
      mojom::PolicyActionClass::kObservePage;
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  effect->action->executable->tab_id = "tab-1";
  effect->action->observation = mojom::TaskObservationBounds::New(
      mojom::ObservationScope::kCurrentDocument,
      mojom::kMaxTaskObservationTotalBytes, mojom::kMaxTaskObservationNodes,
      mojom::kMaxTaskObservationTextBytes, mojom::kMaxTaskObservationFrames,
      mojom::kMaxTaskObservationDeadlineMs);
  return effect;
}

mojom::EffectResultPtr ObservationResult(
    const mojom::TaskEffectBinding& effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->kind = mojom::EffectKind::kPageObservation;
  result->status = mojom::EffectStatus::kCompleted;
  result->observation = mojom::ObservationEffectResult::New();
  auto& observation = *result->observation;
  observation.status = mojom::BipObservationStatus::kOk;
  observation.tab_id = effect.action->executable->tab_id;
  observation.frame_id = effect.action->document->frame_id;
  observation.page_epoch = effect.action->document->page_epoch;
  observation.origin = effect.action->document->normalized_origin;
  observation.graph_revision = effect.action->document->graph_revision;
  observation.node_count = 1u;
  observation.total_bytes = 3u;
  observation.graph_encoding = mojom::BipGraphEncoding::kBipContract;
  observation.graph_payload = {1u, 2u, 3u};
  return result;
}

class CoreServiceManagerTaskRoutingTest : public testing::Test {
 protected:
  void SetUp() override {
    tools_ = std::make_unique<ProfileToolSupervisor>(
        kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    observation_ = base::MakeRefCounted<CorePageObservationBroker>(&context_);
    manager_ = MakeManager(tail_, &context_, std::move(tools_), observation_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, Bindings()),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
  }

  void TearDown() override { manager_->Shutdown(); }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<ProfileToolSupervisor> tools_;
  scoped_refptr<CorePageObservationBroker> observation_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
};

TEST_F(CoreServiceManagerTaskRoutingTest,
       CoreOwnedMemoryToolReceivesTheSameExecutionTriggerAsLibrary) {
  std::optional<mojom::TaskEffectCompletionStatus> completion;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager_, MemoryEffect(NowMonotonicMillis() + 30'000u),
      base::BindLambdaForTesting([&](mojom::TaskEffectCompletionPtr result) {
        ASSERT_TRUE(result);
        completion = result->status;
      }));
  EXPECT_EQ(completion, mojom::TaskEffectCompletionStatus::kSucceeded);
}

TEST_F(CoreServiceManagerTaskRoutingTest,
       ReconciliationReachesTheExactJournalReader) {
  std::optional<mojom::TaskEffectCompletionStatus> completion;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager_, ReconcileEffect(NowMonotonicMillis() + 30'000u),
      base::BindLambdaForTesting([&](mojom::TaskEffectCompletionPtr result) {
        ASSERT_TRUE(result);
        completion = result->status;
      }));
  // This fixture deliberately has no storage broker. OutcomeUnknown proves
  // the effect reached reconciliation's fail-closed journal read; the old
  // no-op route reported Unavailable and advanced the workflow unchanged.
  EXPECT_EQ(completion, mojom::TaskEffectCompletionStatus::kOutcomeUnknown);
}

TEST_F(CoreServiceManagerTaskRoutingTest,
       ExactNodeActivationAndFocusReachThePageExecutor) {
  const uint64_t deadline = NowMonotonicMillis() + 30'000u;
  for (const mojom::PolicyActionClass action_class : {
           mojom::PolicyActionClass::kSyntheticClick,
           mojom::PolicyActionClass::kMoveFocus,
       }) {
    std::optional<mojom::TaskEffectCompletionStatus> completion;
    CoreServiceManagerTaskEffectTestPeer::Execute(
        *manager_, ExactNodeEffect(deadline, action_class),
        base::BindLambdaForTesting([&](mojom::TaskEffectCompletionPtr result) {
          ASSERT_TRUE(result);
          completion = result->status;
        }));
    // No live tab exists in this fixture. Refused proves the request reached
    // ExecuteTaskPageAction's live-document gate; the old routing gap fell
    // through to the generic unavailable terminal instead.
    EXPECT_EQ(completion, mojom::TaskEffectCompletionStatus::kRefused);
  }
}

TEST_F(CoreServiceManagerTaskRoutingTest,
       FailedObservationCompletionsPreserveStatusWithoutPayload) {
  struct FailureCase {
    mojom::EffectStatus effect_status;
    mojom::BipObservationStatus observation_status;
    mojom::TaskEffectCompletionStatus expected;
  };
  const FailureCase cases[] = {
      {mojom::EffectStatus::kDenied, mojom::BipObservationStatus::kUnsupported,
       mojom::TaskEffectCompletionStatus::kRefused},
      {mojom::EffectStatus::kInvalidResult,
       mojom::BipObservationStatus::kResourcePressure,
       mojom::TaskEffectCompletionStatus::kRefused},
      {mojom::EffectStatus::kCancelled, mojom::BipObservationStatus::kCancelled,
       mojom::TaskEffectCompletionStatus::kCancelled},
      {mojom::EffectStatus::kOutcomeUnknown,
       mojom::BipObservationStatus::kInternalError,
       mojom::TaskEffectCompletionStatus::kOutcomeUnknown},
      {mojom::EffectStatus::kDeadlineExceeded,
       mojom::BipObservationStatus::kDeadlineExceeded,
       mojom::TaskEffectCompletionStatus::kUnavailable},
      {mojom::EffectStatus::kResourceLimit,
       mojom::BipObservationStatus::kResourcePressure,
       mojom::TaskEffectCompletionStatus::kUnavailable},
      {mojom::EffectStatus::kUnavailable,
       mojom::BipObservationStatus::kInternalError,
       mojom::TaskEffectCompletionStatus::kUnavailable},
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(static_cast<int>(test.effect_status));
    auto binding = ObservationEffect();
    auto result = ObservationResult(*binding);
    result->status = test.effect_status;
    result->observation->status = test.observation_status;
    auto completion = CoreServiceManagerTaskEffectTestPeer::CompleteObservation(
        *manager_, binding.Clone(), std::move(result));
    ASSERT_TRUE(completion);
    EXPECT_EQ(completion->status, test.expected);
    EXPECT_EQ(completion->operation, binding->operation);
    EXPECT_EQ(completion->effect_id, binding->effect_id);
    EXPECT_EQ(completion->task_id, binding->task_id);
    EXPECT_EQ(completion->kind, binding->kind);
    EXPECT_FALSE(completion->effect_result);
    EXPECT_FALSE(completion->tool_output);
  }
}

TEST_F(CoreServiceManagerTaskRoutingTest,
       SuccessfulObservationCompletionKeepsItsExactResult) {
  auto binding = ObservationEffect();
  auto result = ObservationResult(*binding);
  auto expected = result.Clone();
  auto completion = CoreServiceManagerTaskEffectTestPeer::CompleteObservation(
      *manager_, std::move(binding), std::move(result));
  ASSERT_TRUE(completion);
  EXPECT_EQ(completion->status, mojom::TaskEffectCompletionStatus::kSucceeded);
  EXPECT_EQ(completion->effect_result, expected);
}

TEST_F(CoreServiceManagerTaskRoutingTest,
       MismatchedObservationCompletionRemainsRefusedWithoutPayload) {
  auto binding = ObservationEffect();
  auto result = ObservationResult(*binding);
  result->observation->origin = "https://different.example";
  auto completion = CoreServiceManagerTaskEffectTestPeer::CompleteObservation(
      *manager_, std::move(binding), std::move(result));
  ASSERT_TRUE(completion);
  EXPECT_EQ(completion->status, mojom::TaskEffectCompletionStatus::kRefused);
  EXPECT_FALSE(completion->effect_result);
}

}  // namespace
}  // namespace taffy
