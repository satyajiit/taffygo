// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "taffy/services/core/core_service_impl_parallel_reads_test_peer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using core_service_impl_task_effect_test::CoreServiceImplTaskEffectTest;
using core_service_impl_task_effect_test::kGeneration;
using ParallelPeer = CoreServiceImplParallelReadsTestPeer;
using EffectPeer = CoreServiceImplTaskEffectTestPeer;

mojom::TaskEffectBindingPtr Read(size_t index, uint64_t deadline) {
  auto effect = mojom::TaskEffectBinding::New();
  const std::string suffix = std::to_string(index);
  effect->effect_id = "read-" + suffix;
  effect->task_id = "task-1";
  effect->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  effect->operation = mojom::OperationEnvelope::New(
      "operation-" + suffix, kGeneration, 17u + index, deadline,
      "operation-key-" + suffix);
  effect->action = mojom::TaskActionEffect::New();
  effect->action->idempotency_key = "agent-observation-" + suffix;
  effect->action->document = mojom::TaskFrozenDocument::New();
  effect->action->executable = mojom::TaskExecutableAction::New();
  effect->action->executable->input = mojom::TaskActionInput::New();
  effect->action->executable->tab_id = "tab-" + suffix;
  effect->action->executable->tool_name = "browser.dom.read";
  effect->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  effect->action->executable->action_class =
      mojom::PolicyActionClass::kObservePage;
  effect->action->observation = mojom::TaskObservationBounds::New();
  return effect;
}

std::vector<mojom::TaskEffectBindingPtr> Reads(size_t count,
                                               uint64_t deadline) {
  std::vector<mojom::TaskEffectBindingPtr> effects;
  for (size_t index = 0; index < count; ++index) {
    effects.push_back(Read(index, deadline));
  }
  return effects;
}

mojom::TaskEffectCompletionPtr Completion(
    const mojom::TaskEffectBinding& effect) {
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = effect.operation.Clone();
  completion->effect_id = effect.effect_id;
  completion->task_id = effect.task_id;
  completion->kind = effect.kind;
  completion->status = mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
  return completion;
}

TEST_F(CoreServiceImplTaskEffectTest,
       DurableReadDispatchesBeforeTheNextSourceCommitAdvancesItsRevision) {
  using core_service_impl_task_effect_test::AppendBatch;
  using core_service_impl_task_effect_test::StateBatch;
  host_.HoldTaskEffects();
  ASSERT_TRUE(EffectPeer::StagePolicyCommit(impl_, kGeneration, 17u, 18u));

  // The first read is durable at 18. The walk has staged only the following
  // source's proposal at 19; holding that append must not delay the read.
  auto continuation = AppendBatch("next-source", "next-source-key", 18u, 19u);
  auto durable = StateBatch(2u, 18u);
  durable.states.front().task_effects.push_back(
      Read(1u, ParallelPeer::Now(impl_) + 60'000u));
  continuation.states.push_back(std::move(durable.states.front()));
  EffectPeer::DeliverPolicyCommitResult(impl_, kGeneration, 18u,
                                         std::move(continuation));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(host_.has_held_registration());
  EXPECT_TRUE(host_.executed_effects().empty());

  host_.ReleaseRegistration();
  task_environment_.RunUntilIdle();
  ASSERT_EQ(host_.executed_effects().size(), 1u);
  EXPECT_EQ(host_.executed_effects().front()->operation->task_revision, 18u);
  EXPECT_EQ(host_.published_sequences(), (std::vector<uint64_t>{2u}));
  EXPECT_TRUE(EffectPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(EffectPeer::HasPendingTaskEffectCommit(impl_));
  EXPECT_EQ(ParallelPeer::Pending(impl_), 1u);
  EXPECT_FALSE(ParallelPeer::HasCompletion(impl_, 0u));

  // Only now does the next source's proposal become durable. The first read
  // was already sent through the CoreHost pipe under its registered revision.
  EffectPeer::DeliverCommitResult(impl_, kGeneration, "next-source",
                                   "next-source-key", 19u, StateBatch(3u, 19u));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(host_.executed_effects().size(), 1u);
  EXPECT_EQ(host_.published_sequences(), (std::vector<uint64_t>{2u, 3u}));
  EXPECT_TRUE(EffectPeer::IsReady(impl_));
  EXPECT_FALSE(EffectPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(EffectPeer::HasPendingTaskEffectCommit(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       SourceReadsReachTwoHeldBrowserCallbacksBeforeEitherCompletes) {
  host_.HoldTaskEffects();
  ParallelPeer::Start(impl_, Reads(2u, ParallelPeer::Now(impl_) + 60'000u));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(host_.executed_effects().size(), 2u);
  EXPECT_EQ(ParallelPeer::Pending(impl_), 2u);
  EXPECT_FALSE(EffectPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(ParallelPeer::HasCompletion(impl_, 0u));
  EXPECT_FALSE(ParallelPeer::HasCompletion(impl_, 1u));

  // The later callback can arrive first without opening a reducer command.
  host_.ReleaseTaskEffect(1u, Completion(*host_.executed_effects()[1]));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(ParallelPeer::HasCompletion(impl_, 1u));
  EXPECT_FALSE(ParallelPeer::Activate(impl_));

  // Registration gates even a complete front read. Both replies are now
  // held while the separately tested storage boundary advances the task.
  ParallelPeer::HoldRegistration(impl_, true);
  host_.ReleaseTaskEffect(0u, Completion(*host_.executed_effects()[0]));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(ParallelPeer::Activate(impl_));
  ASSERT_TRUE(ParallelPeer::RememberRevision(impl_, 21u));
  ParallelPeer::HoldRegistration(impl_, false);
  auto first = ParallelPeer::Activate(impl_);
  ASSERT_TRUE(first);
  EXPECT_EQ(first->effect_id, "read-0");
  EXPECT_EQ(first->operation->task_revision, 17u);
  EXPECT_EQ(ParallelPeer::Active(impl_).operation->task_revision, 17u);
  EXPECT_FALSE(EffectPeer::StagePolicyCommit(impl_, kGeneration, 17u, 18u));
  EXPECT_FALSE(EffectPeer::StagePolicyCommit(impl_, kGeneration, 20u, 22u));
  ASSERT_TRUE(EffectPeer::StagePolicyCommit(impl_, kGeneration, 21u, 22u));
  EXPECT_FALSE(ParallelPeer::Activate(impl_));
  EXPECT_FALSE(ParallelPeer::Acknowledge(impl_, 23u));
  ASSERT_TRUE(ParallelPeer::Acknowledge(impl_, 22u));
  auto second = ParallelPeer::Activate(impl_);
  ASSERT_TRUE(second);
  EXPECT_EQ(second->effect_id, "read-1");
  EXPECT_EQ(second->operation->task_revision, 18u);
  EXPECT_FALSE(EffectPeer::StagePolicyCommit(impl_, kGeneration, 21u, 23u));
  EXPECT_TRUE(EffectPeer::StagePolicyCommit(impl_, kGeneration, 22u, 23u));
}

TEST_F(CoreServiceImplTaskEffectTest, SourceReadDispatchNeverExceedsFour) {
  host_.HoldTaskEffects();
  ParallelPeer::Start(impl_, Reads(5u, ParallelPeer::Now(impl_) + 60'000u));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(host_.executed_effects().size(), 4u);
  EXPECT_EQ(ParallelPeer::Pending(impl_), 4u);
  EXPECT_FALSE(EffectPeer::HasActiveTaskEffect(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest, SourceReadsNeverOverlapOneTab) {
  host_.HoldTaskEffects();
  auto effects = Reads(2u, ParallelPeer::Now(impl_) + 60'000u);
  effects[1]->action->executable->tab_id = "tab-0";
  ParallelPeer::Start(impl_, std::move(effects));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(host_.executed_effects().size(), 1u);
  EXPECT_EQ(ParallelPeer::Pending(impl_), 1u);
}

TEST_F(CoreServiceImplTaskEffectTest, OrdinaryModelToolsWaitForSourceReads) {
  host_.HoldTaskEffects();
  auto effects = Reads(2u, ParallelPeer::Now(impl_) + 60'000u);
  // Even a semantic read requested by the model remains an ordinary tool.
  effects[1]->action->idempotency_key = "model-call-action";
  ParallelPeer::Start(impl_, std::move(effects));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(host_.executed_effects().size(), 1u);
  EXPECT_FALSE(EffectPeer::HasActiveTaskEffect(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       SourceReadDeadlineSynthesizesOnceAndLateBrowserReplyCannotReplaceIt) {
  host_.HoldTaskEffects();
  const uint64_t deadline = ParallelPeer::Now(impl_) + 60'000u;
  ParallelPeer::Start(impl_, Reads(2u, deadline));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(host_.executed_effects().size(), 2u);
  ParallelPeer::HoldRegistration(impl_, true);
  EXPECT_EQ(ParallelPeer::EarliestDeadline(impl_), deadline);
  ParallelPeer::Expire(impl_, deadline);
  EXPECT_EQ(ParallelPeer::EarliestDeadline(impl_), 0u);
  EXPECT_TRUE(ParallelPeer::Synthesized(impl_, 0u));
  EXPECT_TRUE(ParallelPeer::Synthesized(impl_, 1u));
  host_.ReleaseTaskEffect(0u, Completion(*host_.executed_effects()[0]));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(ParallelPeer::Synthesized(impl_, 0u));
  EXPECT_EQ(ParallelPeer::Pending(impl_), 2u);
}

TEST_F(CoreServiceImplTaskEffectTest,
       SourceReadTerminalCannotSubstituteDispatchRevision) {
  host_.HoldTaskEffects();
  ParallelPeer::Start(impl_, Reads(2u, ParallelPeer::Now(impl_) + 60'000u));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(host_.executed_effects().size(), 2u);
  auto completion = Completion(*host_.executed_effects()[1]);
  ++completion->operation->task_revision;
  host_.ReleaseTaskEffect(1u, std::move(completion));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(EffectPeer::IsReady(impl_));
  EXPECT_FALSE(EffectPeer::IsHostBound(impl_));
  EXPECT_EQ(ParallelPeer::Pending(impl_), 0u);
}

TEST_F(CoreServiceImplTaskEffectTest,
       SourceReadFailureWithdrawsEveryHeldReadAndRevisionCannotRegress) {
  host_.HoldTaskEffects();
  ParallelPeer::Start(impl_, Reads(2u, ParallelPeer::Now(impl_) + 60'000u));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(host_.executed_effects().size(), 2u);
  ASSERT_TRUE(ParallelPeer::RememberRevision(impl_, 21u));
  EXPECT_FALSE(ParallelPeer::RememberRevision(impl_, 20u));
  ParallelPeer::Fail(impl_);
  EXPECT_FALSE(EffectPeer::IsReady(impl_));
  EXPECT_EQ(ParallelPeer::Pending(impl_), 0u);
  EXPECT_EQ(ParallelPeer::EarliestDeadline(impl_), 0u);
  host_.ReleaseTaskEffect(0u, Completion(*host_.executed_effects()[0]));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(EffectPeer::HasActiveTaskEffect(impl_));
}

}  // namespace
}  // namespace taffy
