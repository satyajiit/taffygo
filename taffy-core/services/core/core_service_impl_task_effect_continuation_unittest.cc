// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl_task_effect_test_peer.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: a terminal whose commit chains the next reducer command
// before it exposes a state — the utility follows the exact storage identity
// and publishes the durable state beside it, and refuses a state that claims
// a revision not yet committed.

namespace taffy {
namespace {

using core_service_impl_task_effect_test::AppendBatch;
using core_service_impl_task_effect_test::CoreServiceImplTaskEffectTest;
using core_service_impl_task_effect_test::DomainStorageBatch;
using core_service_impl_task_effect_test::kCommittedRevision;
using core_service_impl_task_effect_test::kGeneration;
using core_service_impl_task_effect_test::kTaskRevision;
using core_service_impl_task_effect_test::StateBatch;

TEST_F(CoreServiceImplTaskEffectTest,
       LibraryMutationContinuationAcknowledgesOnlyItsFinalState) {
  CoreServiceImplTaskEffectTestPeer::SetActiveTaskEffectKind(
      impl_, mojom::TaskReducerEffectKind::kRunLibraryTool);
  CoreServiceImplTaskEffectTestPeer::SubmitCompletion(
      impl_, DomainStorageBatch("library-storage", "library-storage-key",
                                mojom::StorageOperation::kUpsertLibraryEntry));
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(host_.emitted_appends().empty());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  CoreServiceImplTaskEffectTestPeer::DeliverCommitResult(
      impl_, kGeneration, "library-storage", "library-storage-key", 17u,
      AppendBatch("completion-action", "completion-action-key", 17u, 18u));
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<std::pair<uint64_t, uint64_t>>{{17u, 18u}}),
            host_.emitted_appends());
  EXPECT_TRUE(host_.registered_sequences().empty());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  CoreServiceImplTaskEffectTestPeer::DeliverCommitResult(
      impl_, kGeneration, "completion-action", "completion-action-key", 18u,
      StateBatch(2u, 18u));
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.registered_sequences());
  EXPECT_TRUE(host_.published_sequences().empty());
  ASSERT_TRUE(host_.has_held_registration());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  host_.ReleaseRegistration();
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.published_sequences());
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       ContinuationCarryingTheDurableStatePublishesItAndFollowsTheCommit) {
  ASSERT_TRUE(CoreServiceImplTaskEffectTestPeer::StagePolicyCommit(
      impl_, kGeneration, kTaskRevision, kCommittedRevision));

  // The policy commit landed at 18. The bridge chains the walk's next command
  // (18 -> 19) and publishes the state it just made durable, at 18, beside it.
  CoreResponseBatch continuation =
      AppendBatch("completion-action", "completion-action-key",
                  kCommittedRevision, kCommittedRevision + 1u);
  CoreResponseBatch durable_state = StateBatch(2u, kCommittedRevision);
  continuation.states.push_back(std::move(durable_state.states.front()));
  CoreServiceImplTaskEffectTestPeer::DeliverPolicyCommitResult(
      impl_, kGeneration, kCommittedRevision, std::move(continuation));
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<std::pair<uint64_t, uint64_t>>{
                {kCommittedRevision, kCommittedRevision + 1u}}),
            host_.emitted_appends());
  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.registered_sequences());
  EXPECT_TRUE(host_.published_sequences().empty());
  ASSERT_TRUE(host_.has_held_registration());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  host_.ReleaseRegistration();
  task_environment_.RunUntilIdle();

  // The durable state is on screen and the effect is still open: the state
  // acknowledged nothing, and the utility follows the chained commit.
  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.published_sequences());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  CoreServiceImplTaskEffectTestPeer::DeliverCommitResult(
      impl_, kGeneration, "completion-action", "completion-action-key",
      kCommittedRevision + 1u, StateBatch(3u, kCommittedRevision + 1u));
  task_environment_.RunUntilIdle();

  // The host holds only its first registration; this one is answered at once,
  // and the acknowledging state retires the effect.
  EXPECT_EQ((std::vector<uint64_t>{2u, 3u}), host_.registered_sequences());
  EXPECT_EQ((std::vector<uint64_t>{2u, 3u}), host_.published_sequences());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       ContinuationCarryingTheStagedRevisionFailsClosedBeforePublication) {
  // A memory mutation leaves the task revision where it was, so the state a
  // continuation may carry is the task at 17. This one names 18, the revision
  // the continuation is about to make durable and has not.
  CoreServiceImplTaskEffectTestPeer::SetActiveTaskEffectKind(
      impl_, mojom::TaskReducerEffectKind::kRunMemoryTool);
  CoreServiceImplTaskEffectTestPeer::SubmitCompletion(
      impl_, DomainStorageBatch("memory-storage", "memory-storage-key",
                                mojom::StorageOperation::kUpsertMemory));
  task_environment_.RunUntilIdle();

  CoreResponseBatch continuation =
      AppendBatch("completion-action", "completion-action-key", 17u, 18u);
  CoreResponseBatch speculative_state = StateBatch(2u, 18u);
  continuation.states.push_back(std::move(speculative_state.states.front()));
  CoreServiceImplTaskEffectTestPeer::DeliverCommitResult(
      impl_, kGeneration, "memory-storage", "memory-storage-key", 17u,
      std::move(continuation));
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<mojom::StorageOperation>{
                mojom::StorageOperation::kUpsertMemory}),
            host_.emitted_storage_operations());
  EXPECT_TRUE(host_.registered_sequences().empty());
  EXPECT_TRUE(host_.published_sequences().empty());
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       RecordedDraftBesideFinalStateKeepsItsOwnStorageCompletion) {
  ASSERT_TRUE(CoreServiceImplTaskEffectTestPeer::StagePolicyCommit(
      impl_, kGeneration, kTaskRevision, kCommittedRevision));

  CoreResponseBatch completed = StateBatch(2u, kCommittedRevision);
  CoreResponseBatch recorded =
      DomainStorageBatch("recorded-flow-1", "recorded-flow-1",
                         mojom::StorageOperation::kInstallSkill);
  auto& effect = recorded.effects.front();
  effect->operation->task_revision = 0u;
  effect->storage_commit->install_skill = mojom::SkillInstallEffect::New();
  auto& install = *effect->storage_commit->install_skill;
  install.skill_id = "flow-1";
  install.origin = "https://example.test";
  install.provenance = mojom::SkillProvenance::kRecordedFromTask;
  install.version = 1u;
  install.definition = {0x01u};
  install.step_count = 1u;
  install.recorded_at_utc_ms = 100u;
  completed.effects.push_back(std::move(effect));

  CoreServiceImplTaskEffectTestPeer::DeliverPolicyCommitResult(
      impl_, kGeneration, kCommittedRevision, std::move(completed));
  task_environment_.RunUntilIdle();

  // The inactive installation is ordinary storage beside the final state,
  // not another task-journal continuation. It may reach storage while the
  // browser still holds registration of that already durable task state.
  EXPECT_EQ((std::vector<mojom::StorageOperation>{
                mojom::StorageOperation::kInstallSkill}),
            host_.emitted_storage_operations());
  EXPECT_TRUE(host_.emitted_appends().empty());
  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.registered_sequences());
  EXPECT_TRUE(host_.published_sequences().empty());
  ASSERT_TRUE(host_.has_held_registration());

  CoreResponseBatch installed = StateBatch(3u, kCommittedRevision);
  installed.admission->operation_id = "recorded-flow-1";
  CoreServiceImplTaskEffectTestPeer::DeliverCommitResult(
      impl_, kGeneration, "recorded-flow-1", "recorded-flow-1", 0u,
      std::move(installed));
  task_environment_.RunUntilIdle();

  // Its acknowledgment publishes the catalogue without consuming the
  // pending task acknowledgment or demanding the task's revision on an
  // installation whose storage revision is zero.
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.registered_sequences());

  host_.ReleaseRegistration();
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<uint64_t>{2u, 3u}), host_.published_sequences());
  EXPECT_EQ((std::vector<uint64_t>{2u, 3u}), host_.registered_sequences());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
}

}  // namespace
}  // namespace taffy
