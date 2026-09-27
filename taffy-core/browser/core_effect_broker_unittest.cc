// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_broker.h"

#include <utility>

#include "base/test/bind.h"
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

mojom::EffectEnvelopePtr ConsequentialObservation(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->effect_id = "effect-observation";
  effect->kind = mojom::EffectKind::kPageObservation;
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->page_observation = mojom::PageObservationEffect::New();
  effect->page_observation->tab_id = "tab-1";
  effect->page_observation->frame_id = "frame-1";
  effect->page_observation->page_epoch = "epoch-1";
  effect->page_observation->task_id = "task-1";
  effect->page_observation->action_id = "action-1";
  effect->page_observation->authority_subject = mojom::AuthoritySubject::New();
  effect->page_observation->authority_subject->kind =
      mojom::AuthoritySubjectKind::kTask;
  effect->page_observation->authority_subject->authority_subject_id =
      effect->page_observation->task_id;
  effect->page_observation->capability_id = "capability-1";
  effect->page_observation->proposal_digest = std::string(64, 'a');
  effect->page_observation->idempotency_key =
      effect->operation->idempotency_key;
  effect->page_observation->scope = mojom::ObservationScope::kCurrentDocument;
  effect->page_observation->max_bytes = 4096;
  return effect;
}

mojom::EffectEnvelopePtr DirectObservation(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->operation->task_revision = 0u;
  effect->effect_id = "effect-direct-observation";
  effect->kind = mojom::EffectKind::kPageObservation;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->page_observation = mojom::PageObservationEffect::New();
  effect->page_observation->authority_subject = mojom::AuthoritySubject::New();
  effect->page_observation->authority_subject->kind =
      mojom::AuthoritySubjectKind::kDirectUserIntent;
  effect->page_observation->authority_subject->authority_subject_id =
      "direct-intent-1";
  effect->page_observation->tab_id = "tab-1";
  effect->page_observation->frame_id = "frame-1";
  effect->page_observation->page_epoch = "epoch-1";
  effect->page_observation->capability_id = "capability-direct";
  effect->page_observation->proposal_digest = std::string(64u, 'a');
  effect->page_observation->idempotency_key =
      effect->operation->idempotency_key;
  effect->page_observation->scope = mojom::ObservationScope::kCurrentDocument;
  effect->page_observation->max_bytes = mojom::kMaxDirectObservationTotalBytes;
  effect->page_observation->max_nodes = mojom::kMaxDirectObservationNodes;
  effect->page_observation->max_text_bytes =
      mojom::kMaxDirectObservationTextBytes;
  effect->page_observation->max_frames = mojom::kMaxDirectObservationFrames;
  effect->page_observation->deadline_ms =
      mojom::kMaxDirectObservationDeadlineMs;
  return effect;
}

mojom::EffectEnvelopePtr SecureEntropyEffect(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->effect_id = "effect-account-entropy";
  effect->kind = mojom::EffectKind::kSecureStore;
  effect->retry_class = mojom::RetryClass::kNever;
  effect->secure_store = mojom::SecureStoreEffect::New();
  effect->secure_store->operation_kind =
      mojom::SecureStoreOperation::kGenerateEntropy;
  effect->secure_store->generate_entropy = mojom::GenerateEntropyRequest::New();
  effect->secure_store->generate_entropy->flow_id = "flow-1";
  effect->secure_store->generate_entropy->byte_count = 64u;
  return effect;
}

TEST(CoreEffectBrokerTest, ConsequentialDisconnectIsOutcomeUnknownExactlyOnce) {
  CoreEffectBroker::CompletionCallback adapter_completion;
  CoreEffectBroker::JournalCallback result_commit;
  mojom::EffectStatus journalled_status = mojom::EffectStatus::kCompleted;
  uint64_t cancelled_generation = 0;
  std::vector<std::string> cancelled_effect_ids;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback done) {
        adapter_completion = std::move(done);
      });
  handlers.commit_result =
      base::BindLambdaForTesting([&](const mojom::EffectResult& result,
                                     CoreEffectBroker::JournalCallback done) {
        journalled_status = result.status;
        result_commit = std::move(done);
      });
  handlers.cancel_generation = base::BindLambdaForTesting(
      [&](uint64_t generation, std::vector<std::string> effect_ids) {
        cancelled_generation = generation;
        cancelled_effect_ids = std::move(effect_ids);
      });

  CoreEffectBroker broker(std::move(handlers));
  int pending_transitions = 0;
  broker.SetPendingChangedCallback(
      base::BindLambdaForTesting([&] { ++pending_transitions; }));
  broker.SetActiveGeneration(11);
  int completion_count = 0;
  mojom::EffectStatus status = mojom::EffectStatus::kCompleted;
  broker.Dispatch(
      ConsequentialObservation(11),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ++completion_count;
        ASSERT_TRUE(result);
        status = result->status;
      }));
  ASSERT_TRUE(adapter_completion);
  ASSERT_EQ(1u, broker.pending_count_for_testing());
  EXPECT_EQ(1, pending_transitions);

  broker.OnGenerationDisconnected(11);
  EXPECT_EQ(11u, cancelled_generation);
  EXPECT_EQ(std::vector<std::string>({"effect-observation"}),
            cancelled_effect_ids);
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, journalled_status);
  EXPECT_EQ(1u, broker.pending_count_for_testing());
  ASSERT_TRUE(result_commit);

  auto late = mojom::EffectResult::New();
  late->operation = Operation(11);
  late->effect_id = "effect-observation";
  late->status = mojom::EffectStatus::kCompleted;
  late->kind = mojom::EffectKind::kPageObservation;
  late->observation = mojom::ObservationEffectResult::New();
  std::move(adapter_completion).Run(std::move(late));
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(1u, broker.late_completion_count_for_testing());

  std::move(result_commit).Run(true);
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, status);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
  EXPECT_EQ(2, pending_transitions);
  EXPECT_EQ(1u, broker.late_completion_count_for_testing());
}

TEST(CoreEffectBrokerTest, DuplicatePendingEffectIdDoesNotDispatchTwice) {
  CoreEffectBroker::CompletionCallback adapter_completion;
  int intent_commits = 0;
  int observation_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([&](const mojom::EffectEnvelope&,
                                     CoreEffectBroker::JournalCallback done) {
        ++intent_commits;
        std::move(done).Run(true);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback done) {
        ++observation_dispatches;
        adapter_completion = std::move(done);
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(23);
  int original_completions = 0;
  mojom::EffectStatus duplicate_status = mojom::EffectStatus::kCompleted;
  broker.Dispatch(
      ConsequentialObservation(23),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ++original_completions;
        ASSERT_TRUE(result);
      }));
  ASSERT_TRUE(adapter_completion);

  broker.Dispatch(
      ConsequentialObservation(23),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ASSERT_TRUE(result);
        duplicate_status = result->status;
      }));
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, duplicate_status);
  EXPECT_EQ(1, intent_commits);
  EXPECT_EQ(1, observation_dispatches);
  EXPECT_EQ(1u, broker.pending_count_for_testing());

  auto result = mojom::EffectResult::New();
  result->operation = Operation(23);
  result->effect_id = "effect-observation";
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kPageObservation;
  result->observation = mojom::ObservationEffectResult::New();
  std::move(adapter_completion).Run(std::move(result));

  EXPECT_EQ(1, original_completions);
  EXPECT_EQ(1, intent_commits);
  EXPECT_EQ(1, observation_dispatches);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
}

TEST(CoreEffectBrokerTest, ObservationGrantBindingMustMatchOperation) {
  int observation_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback) {
        ++observation_dispatches;
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(9);
  auto effect = ConsequentialObservation(9);
  effect->page_observation->idempotency_key = "different-key";

  mojom::EffectResultPtr terminal;
  broker.Dispatch(std::move(effect), base::BindLambdaForTesting(
                                         [&](mojom::EffectResultPtr result) {
                                           terminal = std::move(result);
                                         }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  EXPECT_EQ(0, observation_dispatches);
}

TEST(CoreEffectBrokerTest, DirectObservationUsesIntentAndResultJournal) {
  int intent_commits = 0;
  int result_commits = 0;
  int observation_dispatches = 0;
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
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++observation_dispatches;
        auto result = mojom::EffectResult::New();
        result->operation = effect->operation.Clone();
        result->effect_id = effect->effect_id;
        result->status = mojom::EffectStatus::kCompleted;
        result->kind = mojom::EffectKind::kPageObservation;
        result->observation = mojom::ObservationEffectResult::New();
        std::move(done).Run(std::move(result));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(17u);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      DirectObservation(17u),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        terminal = std::move(result);
      }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  EXPECT_EQ(1, intent_commits);
  EXPECT_EQ(1, observation_dispatches);
  EXPECT_EQ(1, result_commits);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
}

TEST(CoreEffectBrokerTest, MissingAccountAdapterReturnsMatchingTypedTerminal) {
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(13);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      SecureEntropyEffect(13),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        terminal = std::move(result);
      }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, terminal->status);
  ASSERT_TRUE(terminal->secure_store);
  EXPECT_EQ(mojom::SecureStoreOperation::kGenerateEntropy,
            terminal->secure_store->operation_kind);
  ASSERT_TRUE(terminal->secure_store->generated_entropy);
  EXPECT_EQ("flow-1", terminal->secure_store->generated_entropy->flow_id);
  EXPECT_FALSE(terminal->secure_store->transient_write);
  EXPECT_FALSE(terminal->secure_store->deleted_handle);
}

}  // namespace
}  // namespace taffy
