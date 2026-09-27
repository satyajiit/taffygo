// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/test/bind.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::OperationEnvelopePtr Operation(uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New();
  operation->operation_id = "operation-1";
  operation->service_generation = generation;
  operation->task_revision = 2;
  operation->deadline_monotonic_ms = 10'000;
  operation->idempotency_key = "idempotency-1";
  return operation;
}

mojom::EffectEnvelopePtr StorageEffect(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->effect_id = "effect-storage";
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = "task-1";
  effect->storage_commit->expected_revision = 1;
  effect->storage_commit->resulting_revision = 2;
  effect->storage_commit->transaction_batch = {1, 2, 3};
  effect->storage_commit->task_id_seed.assign(32u, 0u);
  effect->storage_commit->task_id_seed[0] = 1;
  return effect;
}

mojom::EffectEnvelopePtr AtomicWorkspaceStorageEffect(uint64_t generation) {
  mojom::EffectEnvelopePtr effect = StorageEffect(generation);
  effect->storage_commit->workspace = mojom::WorkspacePersistEffect::New();
  effect->storage_commit->workspace->workspace_id =
      "11111111111111111111111111111111";
  effect->storage_commit->workspace->expected_revision = 0u;
  effect->storage_commit->workspace->resulting_revision = 1u;
  effect->storage_commit->workspace->snapshot = {1u};
  return effect;
}

mojom::EffectEnvelopePtr IndependentWorkspaceStorageEffect(
    uint64_t generation) {
  mojom::EffectEnvelopePtr effect = AtomicWorkspaceStorageEffect(generation);
  effect->operation->task_revision = 0u;
  effect->effect_id = effect->operation->operation_id;
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kUpsertWorkspace;
  effect->storage_commit->task_id.clear();
  effect->storage_commit->expected_revision = 0u;
  effect->storage_commit->resulting_revision = 0u;
  effect->storage_commit->transaction_batch.clear();
  effect->storage_commit->task_id_seed.assign(
      storage_internal::kTaskIdSeedBytes, 0u);
  return effect;
}

mojom::EffectResultPtr SuccessfulStorageResult(
    const mojom::EffectEnvelope& effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = effect.kind;
  result->storage = mojom::StorageEffectResult::New();
  result->storage->committed_revision =
      effect.storage_commit->resulting_revision;
  return result;
}

TEST(CoreEffectBrokerStorageTest, StorageCommitIsTheOnlyJournalLayer) {
  int intent_commits = 0;
  int result_commits = 0;
  int storage_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([&](const mojom::EffectEnvelope&,
                                     CoreEffectBroker::JournalCallback done) {
        ++intent_commits;
        std::move(done).Run(true);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [&](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        ++result_commits;
        std::move(done).Run(true);
      });
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++storage_dispatches;
        std::move(done).Run(SuccessfulStorageResult(*effect));
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7);
  mojom::EffectResultPtr terminal;
  broker.Dispatch(StorageEffect(7), base::BindLambdaForTesting(
                                        [&](mojom::EffectResultPtr result) {
                                          terminal = std::move(result);
                                        }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  EXPECT_EQ(0, intent_commits);
  EXPECT_EQ(0, result_commits);
  EXPECT_EQ(1, storage_dispatches);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
}

TEST(CoreEffectBrokerStorageTest,
     TaskCancellationDrainsTheRealAtomicStorageTerminal) {
  CoreEffectBroker::CompletionCallback storage_completion;
  mojom::EffectResultPtr storage_terminal;
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        storage_terminal = SuccessfulStorageResult(*effect);
        storage_completion = std::move(done);
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7);
  int completion_count = 0;
  int drain_count = 0;
  mojom::EffectStatus status = mojom::EffectStatus::kCancelled;
  broker.Dispatch(StorageEffect(7), base::BindLambdaForTesting(
                                        [&](mojom::EffectResultPtr result) {
                                          ++completion_count;
                                          ASSERT_TRUE(result);
                                          status = result->status;
                                        }));
  ASSERT_TRUE(storage_completion);
  ASSERT_TRUE(storage_terminal);

  broker.CancelTask("task-1", 7,
                    base::BindLambdaForTesting([&]() { ++drain_count; }));
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(0, drain_count);
  EXPECT_EQ(1u, broker.pending_count_for_testing());

  std::move(storage_completion).Run(std::move(storage_terminal));
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(1, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, status);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
  EXPECT_EQ(0u, broker.late_completion_count_for_testing());
}

TEST(CoreEffectBrokerStorageTest, AtomicWorkspaceBodyIsStrictlyValidated) {
  int storage_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++storage_dispatches;
        std::move(done).Run(SuccessfulStorageResult(*effect));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7);

  mojom::EffectResultPtr accepted;
  broker.Dispatch(
      AtomicWorkspaceStorageEffect(7),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        accepted = std::move(result);
      }));
  ASSERT_TRUE(accepted);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, accepted->status);
  EXPECT_EQ(1, storage_dispatches);

  auto invalid_id = AtomicWorkspaceStorageEffect(7);
  invalid_id->storage_commit->workspace->workspace_id.front() = 'A';
  auto stale_revision = AtomicWorkspaceStorageEffect(7);
  stale_revision->storage_commit->workspace->resulting_revision = 2u;
  auto empty_snapshot = AtomicWorkspaceStorageEffect(7);
  empty_snapshot->storage_commit->workspace->snapshot.clear();
  auto expect_refused = [&](mojom::EffectEnvelopePtr effect) {
    mojom::EffectResultPtr refused;
    broker.Dispatch(std::move(effect), base::BindLambdaForTesting(
                                           [&](mojom::EffectResultPtr result) {
                                             refused = std::move(result);
                                           }));
    ASSERT_TRUE(refused);
    EXPECT_EQ(mojom::EffectStatus::kInvalidResult, refused->status);
  };
  expect_refused(std::move(invalid_id));
  expect_refused(std::move(stale_revision));
  expect_refused(std::move(empty_snapshot));
  EXPECT_EQ(1, storage_dispatches);
}

TEST(CoreEffectBrokerStorageTest,
     IndependentWorkspaceRequiresFixedNeutralTaskSeed) {
  int storage_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++storage_dispatches;
        std::move(done).Run(SuccessfulStorageResult(*effect));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7);

  mojom::EffectResultPtr accepted;
  broker.Dispatch(
      IndependentWorkspaceStorageEffect(7),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        accepted = std::move(result);
      }));
  ASSERT_TRUE(accepted);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, accepted->status);
  EXPECT_EQ(1, storage_dispatches);

  auto empty_seed = IndependentWorkspaceStorageEffect(7);
  empty_seed->storage_commit->task_id_seed.clear();
  auto nonneutral_seed = IndependentWorkspaceStorageEffect(7);
  nonneutral_seed->storage_commit->task_id_seed.front() = 1u;
  auto expect_refused = [&](mojom::EffectEnvelopePtr effect) {
    mojom::EffectResultPtr refused;
    broker.Dispatch(std::move(effect), base::BindLambdaForTesting(
                                           [&](mojom::EffectResultPtr result) {
                                             refused = std::move(result);
                                           }));
    ASSERT_TRUE(refused);
    EXPECT_EQ(mojom::EffectStatus::kInvalidResult, refused->status);
  };
  expect_refused(std::move(empty_seed));
  expect_refused(std::move(nonneutral_seed));
  EXPECT_EQ(1, storage_dispatches);
}

TEST(CoreEffectBrokerStorageTest, InvalidBodiesFailBeforeDispatch) {
  int storage_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback) {
        ++storage_dispatches;
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(5);

  auto mismatched = StorageEffect(5);
  mismatched->page_observation = mojom::PageObservationEffect::New();
  auto degenerate_seed = StorageEffect(5);
  degenerate_seed->storage_commit->task_id_seed.assign(32u, 0u);

  auto expect_refused = [&](mojom::EffectEnvelopePtr effect) {
    mojom::EffectResultPtr terminal;
    broker.Dispatch(std::move(effect), base::BindLambdaForTesting(
                                           [&](mojom::EffectResultPtr result) {
                                             terminal = std::move(result);
                                           }));
    ASSERT_TRUE(terminal);
    EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  };
  expect_refused(std::move(mismatched));
  expect_refused(std::move(degenerate_seed));
  EXPECT_EQ(0, storage_dispatches);
}

}  // namespace
}  // namespace taffy
