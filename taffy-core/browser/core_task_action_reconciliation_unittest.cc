// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action_reconciliation.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class AppendCallback final : public TaskJournalAppendCallback {
 public:
  explicit AppendCallback(base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {}

  void Run(bool committed) override {
    CHECK(callback_);
    std::move(callback_).Run(committed);
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

std::unique_ptr<TaskJournalAppendCallback> Completion(
    base::OnceCallback<void(bool)> callback) {
  return std::make_unique<AppendCallback>(std::move(callback));
}

DispatchIntentRecord Intent() {
  DispatchIntentRecord record;
  record.dispatch_id = DispatchId{"dispatch-1"};
  record.task_id = TaskId{"task-1"};
  record.action_id = ActionId{"action-1"};
  record.action_type = ActionType::kActivate;
  record.capability_reference = CapabilityReference{"capability-1"};
  record.actor_lease_id = ActorLeaseId{"lease-1"};
  record.tab_id = TabId{"tab-1"};
  record.frame_id = FrameId{"frame-1"};
  record.page_epoch = PageEpoch{"epoch-1"};
  record.graph_revision = 7u;
  record.origin = Origin{
      .kind = OriginKind::kTuple,
      .serialization = "https://example.test",
  };
  record.idempotency_policy = IdempotencyPolicy::kIdempotentWrite;
  record.recorded_at_monotonic_ms = 42u;
  return record;
}

ActionResult Terminal(ActionResultCode code) {
  ActionResult result;
  result.schema_version = "0.9";
  result.request_id = RequestId{"request-1"};
  result.action_id = ActionId{"action-1"};
  result.dispatch_id = DispatchId{"dispatch-1"};
  result.result_code = code;
  result.dispatched = true;
  result.terminal = true;
  result.completed_at_monotonic_ms = 84u;
  result.repeat_may_duplicate_effect = code == ActionResultCode::kVerified;
  return result;
}

bool AppendIntent(CoreStorageBroker* broker) {
  base::RunLoop loop;
  std::optional<bool> answer;
  broker->RecordDispatching(
      Intent(), Completion(base::BindLambdaForTesting([&](bool committed) {
        answer = committed;
        loop.Quit();
      })));
  loop.Run();
  return answer.value_or(false);
}

bool AppendTerminal(CoreStorageBroker* broker, ActionResultCode code) {
  base::RunLoop loop;
  std::optional<bool> answer;
  broker->RecordTerminalResult(
      Terminal(code), Completion(base::BindLambdaForTesting([&](bool committed) {
        answer = committed;
        loop.Quit();
      })));
  loop.Run();
  return answer.value_or(false);
}

mojom::TaskEffectBindingPtr Effect(mojom::TaskActionOperationKind operation) {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      "operation-1", 1u, 4u, 1000u, "key-1");
  effect->effect_id = "effect-1";
  effect->task_id = "task-1";
  effect->kind = mojom::TaskReducerEffectKind::kReconcileAction;
  effect->reconcile = mojom::TaskReconcileEffect::New(
      "action-1", mojom::TaskRecoveryRule::kReconcileFirst, "dispatch-1",
      operation);
  return effect;
}

mojom::TaskEffectCompletionPtr RunReconciliation(
    CoreStorageBroker* broker,
    mojom::TaskActionOperationKind operation) {
  base::RunLoop loop;
  mojom::TaskEffectCompletionPtr answer;
  ReconcileTaskAction(
      broker, Effect(operation),
      base::BindLambdaForTesting([&](mojom::TaskEffectCompletionPtr result) {
        answer = std::move(result);
        loop.Quit();
      }));
  loop.Run();
  return answer;
}

class CoreTaskActionReconciliationTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    broker_ = std::make_unique<CoreStorageBroker>(
        directory_.GetPath().AppendASCII("core.sqlite3"), false);
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir directory_;
  std::unique_ptr<CoreStorageBroker> broker_;
};

TEST_F(CoreTaskActionReconciliationTest,
       MissingAndOpenClaimsRemainOutcomeUnknown) {
  mojom::TaskEffectCompletionPtr missing = RunReconciliation(
      broker_.get(), mojom::TaskActionOperationKind::kDomClick);
  ASSERT_TRUE(missing);
  EXPECT_EQ(mojom::TaskEffectCompletionStatus::kOutcomeUnknown,
            missing->status);
  EXPECT_FALSE(missing->reconciled_action_result);

  ASSERT_TRUE(AppendIntent(broker_.get()));
  mojom::TaskEffectCompletionPtr open = RunReconciliation(
      broker_.get(), mojom::TaskActionOperationKind::kDomClick);
  ASSERT_TRUE(open);
  EXPECT_EQ(mojom::TaskEffectCompletionStatus::kOutcomeUnknown, open->status);
  EXPECT_FALSE(open->reconciled_action_result);
}

TEST_F(CoreTaskActionReconciliationTest,
       ExactTerminalRefusalCrossesAsDurableEvidence) {
  ASSERT_TRUE(AppendIntent(broker_.get()));
  ASSERT_TRUE(AppendTerminal(broker_.get(), ActionResultCode::kDeniedByPolicy));

  mojom::TaskEffectCompletionPtr completion = RunReconciliation(
      broker_.get(), mojom::TaskActionOperationKind::kDomClick);

  ASSERT_TRUE(completion);
  EXPECT_EQ(mojom::TaskEffectCompletionStatus::kSucceeded,
            completion->status);
  ASSERT_TRUE(completion->reconciled_action_result);
  EXPECT_EQ(static_cast<uint32_t>(ActionResultCode::kDeniedByPolicy),
            completion->reconciled_action_result->result_code);
}

TEST_F(CoreTaskActionReconciliationTest,
       VerifiedPayloadFreePageMutationMayBeRecovered) {
  ASSERT_TRUE(AppendIntent(broker_.get()));
  ASSERT_TRUE(AppendTerminal(broker_.get(), ActionResultCode::kVerified));

  mojom::TaskEffectCompletionPtr completion = RunReconciliation(
      broker_.get(), mojom::TaskActionOperationKind::kDomClick);

  ASSERT_TRUE(completion);
  EXPECT_EQ(mojom::TaskEffectCompletionStatus::kSucceeded,
            completion->status);
  ASSERT_TRUE(completion->reconciled_action_result);
  EXPECT_EQ(static_cast<uint32_t>(ActionResultCode::kVerified),
            completion->reconciled_action_result->result_code);
}

TEST_F(CoreTaskActionReconciliationTest,
       VerifiedNavigationWithoutItsSourceEvidenceRemainsUnknown) {
  ASSERT_TRUE(AppendIntent(broker_.get()));
  ASSERT_TRUE(AppendTerminal(broker_.get(), ActionResultCode::kVerified));

  mojom::TaskEffectCompletionPtr completion = RunReconciliation(
      broker_.get(), mojom::TaskActionOperationKind::kNavigate);

  ASSERT_TRUE(completion);
  EXPECT_EQ(mojom::TaskEffectCompletionStatus::kOutcomeUnknown,
            completion->status);
  EXPECT_FALSE(completion->reconciled_action_result);
}

}  // namespace
}  // namespace taffy
