// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_broker.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

mojom::EffectEnvelopePtr ConsequentialModelRequest(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->effect_id = "effect-model";
  effect->kind = mojom::EffectKind::kModelRequest;
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->model_request = mojom::ModelRequestEffect::New();
  effect->model_request->route_id = "managed.primary";
  effect->model_request->model_id = "fixture-model";
  effect->model_request->disclosure =
      mojom::DisclosureClass::kUserSelectedContent;
  effect->model_request->request_body = {1, 0, 2, 7};
  effect->model_request->max_output_bytes = 4096;
  effect->model_request->task_id = "task-1";
  effect->model_request->provider_id = "fixture-provider";
  effect->model_request->wire_api = mojom::ProviderWireApi::kAnthropicMessages;
  effect->model_request->endpoint = "https://provider.taffy.test";
  return effect;
}

mojom::EffectEnvelopePtr ConsequentialToolJob(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->effect_id = "effect-tool";
  effect->kind = mojom::EffectKind::kToolJob;
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->tool_job = mojom::ToolJobEffect::New();
  effect->tool_job->job_id = "job-tool";
  effect->tool_job->runtime = mojom::ToolRuntimeKind::kPython;
  effect->tool_job->tool_id = "bundled-tool";
  effect->tool_job->tool_version = "1";
  effect->tool_job->operation_kind =
      mojom::ToolOperation::kRunBundledPythonModule;
  effect->tool_job->budget = mojom::ToolResourceBudget::New(
      1024u, 1024u, 1024u * 1024u, 1000u, 4096u, 4u);
  effect->tool_job->bundled_python = mojom::BundledPythonArguments::New(
      "spreadsheet.build", std::vector<uint8_t>{1u});
  effect->tool_job->task_id = "task-1";
  return effect;
}

// The model request is the effect a cancellation most needs to reach, and
// until the contract carried a task on it there was no field to reach it by:
// the broker read a task identity from three effect bodies and this was not
// one of them, so CancelTask walked past a call that was already being paid
// for and reported that it had cancelled the task.
TEST(CoreEffectBrokerCancellationTest, TaskCancellationClaimsAModelRequest) {
  CoreEffectBroker::CompletionCallback adapter_completion;
  CoreEffectBroker::JournalCallback result_commit;
  mojom::EffectStatus journalled_status = mojom::EffectStatus::kCompleted;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([](const mojom::EffectEnvelope &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.commit_result =
      base::BindLambdaForTesting([&](const mojom::EffectResult &result,
                                     CoreEffectBroker::JournalCallback done) {
        journalled_status = result.status;
        result_commit = std::move(done);
      });
  handlers.model = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback done) {
        adapter_completion = std::move(done);
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(31);
  int completion_count = 0;
  int drain_count = 0;
  mojom::EffectStatus status = mojom::EffectStatus::kCompleted;
  broker.Dispatch(
      ConsequentialModelRequest(31),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ++completion_count;
        ASSERT_TRUE(result);
        status = result->status;
      }));
  ASSERT_TRUE(adapter_completion);
  EXPECT_EQ(1u, broker.pending_count_for_testing());

  broker.CancelTask("task-1", 31,
                    base::BindLambdaForTesting([&]() { ++drain_count; }));
  // The drain has not run, which is the whole assertion: an unclaimed effect
  // would have let CancelTask return immediately with the call still in
  // flight.
  EXPECT_EQ(0, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, journalled_status);
  ASSERT_TRUE(result_commit);

  std::move(result_commit).Run(true);
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(1, drain_count);
  // A consequential effect that was stopped mid-flight settles as unknown
  // rather than cancelled, because the provider may already have charged for
  // it.
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, status);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
}

// A tool job is the effect a cancellation has to reach for the longest. A
// worker holds a process, a memory and CPU budget, and every descriptor the
// browser opened for it, and it holds them until something says stop; before
// ToolJobEffect carried a task there was no field to say it by, so CancelTask
// walked past a running worker and reported the task cancelled.
TEST(CoreEffectBrokerCancellationTest, TaskCancellationClaimsAToolJob) {
  CoreEffectBroker::ToolCompletionCallback tool_completion;
  CoreEffectBroker::JournalCallback result_commit;
  int adapter_cancellations = 0;
  mojom::EffectStatus journalled_status = mojom::EffectStatus::kCompleted;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([](const mojom::EffectEnvelope &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.commit_result =
      base::BindLambdaForTesting([&](const mojom::EffectResult &result,
                                     CoreEffectBroker::JournalCallback done) {
        journalled_status = result.status;
        result_commit = std::move(done);
      });
  handlers.tool = base::BindLambdaForTesting(
      [&](mojom::OperationEnvelopePtr, std::string, mojom::ToolJobEffectPtr,
          CoreEffectBroker::ToolCompletionCallback done) {
        tool_completion = std::move(done);
      });
  handlers.cancel_task = base::BindLambdaForTesting(
      [&](std::string_view task_id, uint64_t) {
        EXPECT_EQ("task-1", task_id);
        ++adapter_cancellations;
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(31);
  int completion_count = 0;
  int drain_count = 0;
  mojom::EffectStatus status = mojom::EffectStatus::kCompleted;
  broker.Dispatch(
      ConsequentialToolJob(31),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ++completion_count;
        ASSERT_TRUE(result);
        status = result->status;
      }));
  ASSERT_TRUE(tool_completion);
  EXPECT_EQ(1u, broker.pending_count_for_testing());

  broker.CancelTask("task-1", 31,
                    base::BindLambdaForTesting([&]() { ++drain_count; }));
  // The adapter was asked to stop its exact worker, which is the half a
  // synthesized terminal cannot do on its own: the process would otherwise
  // keep running behind a completed effect.
  EXPECT_EQ(1, adapter_cancellations);
  EXPECT_EQ(0, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, journalled_status);
  ASSERT_TRUE(result_commit);

  std::move(result_commit).Run(true);
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(1, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, status);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
}

// The other direction, and the reason the broker never guesses. A job no task
// owns is a direct request; claiming it for whichever task happened to be
// cancelled would stop work the person is still waiting on.
TEST(CoreEffectBrokerCancellationTest, AToolJobNoTaskOwnsIsNeverClaimed) {
  CoreEffectBroker::ToolCompletionCallback tool_completion;
  int completion_count = 0;
  int drain_count = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([](const mojom::EffectEnvelope &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.commit_result =
      base::BindLambdaForTesting([](const mojom::EffectResult &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.tool = base::BindLambdaForTesting(
      [&](mojom::OperationEnvelopePtr, std::string, mojom::ToolJobEffectPtr,
          CoreEffectBroker::ToolCompletionCallback done) {
        tool_completion = std::move(done);
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(31);
  auto effect = ConsequentialToolJob(31);
  effect->tool_job->task_id.clear();
  broker.Dispatch(std::move(effect),
                  base::BindLambdaForTesting(
                      [&](mojom::EffectResultPtr) { ++completion_count; }));
  ASSERT_TRUE(tool_completion);

  broker.CancelTask("task-1", 31,
                    base::BindLambdaForTesting([&]() { ++drain_count; }));
  // Nothing was claimed, so the drain settles at once and the job is still
  // owed its own terminal.
  EXPECT_EQ(1, drain_count);
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(1u, broker.pending_count_for_testing());
}

TEST(CoreEffectBrokerCancellationTest,
     TaskCancellationDrainsAfterTerminalJournal) {
  CoreEffectBroker::CompletionCallback adapter_completion;
  CoreEffectBroker::JournalCallback result_commit;
  mojom::EffectStatus journalled_status = mojom::EffectStatus::kCompleted;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([](const mojom::EffectEnvelope &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.commit_result =
      base::BindLambdaForTesting([&](const mojom::EffectResult &result,
                                     CoreEffectBroker::JournalCallback done) {
        journalled_status = result.status;
        result_commit = std::move(done);
      });
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback done) {
        adapter_completion = std::move(done);
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(19);
  int completion_count = 0;
  int drain_count = 0;
  mojom::EffectStatus status = mojom::EffectStatus::kCompleted;
  broker.Dispatch(
      ConsequentialObservation(19),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ++completion_count;
        ASSERT_TRUE(result);
        status = result->status;
      }));
  ASSERT_TRUE(adapter_completion);

  broker.CancelTask("task-1", 19,
                    base::BindLambdaForTesting([&]() { ++drain_count; }));
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(0, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, journalled_status);
  EXPECT_EQ(1u, broker.pending_count_for_testing());
  ASSERT_TRUE(result_commit);

  auto late = mojom::EffectResult::New();
  late->operation = Operation(19);
  late->effect_id = "effect-observation";
  late->status = mojom::EffectStatus::kCompleted;
  late->kind = mojom::EffectKind::kPageObservation;
  late->observation = mojom::ObservationEffectResult::New();
  std::move(adapter_completion).Run(std::move(late));
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(0, drain_count);

  std::move(result_commit).Run(true);
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(1, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, status);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
  EXPECT_EQ(1u, broker.late_completion_count_for_testing());
}

TEST(CoreEffectBrokerCancellationTest,
     TaskCancellationStopsAdapterBeforeJournalDrain) {
  CoreEffectBroker::CompletionCallback adapter_completion;
  CoreEffectBroker::JournalCallback result_commit;
  int adapter_cancellations = 0;
  mojom::EffectStatus journalled_status = mojom::EffectStatus::kCompleted;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([](const mojom::EffectEnvelope &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.commit_result =
      base::BindLambdaForTesting([&](const mojom::EffectResult &result,
                                     CoreEffectBroker::JournalCallback done) {
        journalled_status = result.status;
        result_commit = std::move(done);
      });
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback done) {
        adapter_completion = std::move(done);
      });
  handlers.cancel_task = base::BindLambdaForTesting(
      [&](std::string_view task_id, uint64_t generation) {
        EXPECT_EQ("task-1", task_id);
        EXPECT_EQ(29u, generation);
        ++adapter_cancellations;
        auto result = mojom::EffectResult::New();
        result->operation = Operation(generation);
        result->effect_id = "effect-observation";
        result->status = mojom::EffectStatus::kCancelled;
        result->kind = mojom::EffectKind::kPageObservation;
        result->observation = mojom::ObservationEffectResult::New();
        std::move(adapter_completion).Run(std::move(result));
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(29);
  int completion_count = 0;
  int drain_count = 0;
  mojom::EffectStatus terminal_status = mojom::EffectStatus::kCompleted;
  broker.Dispatch(
      ConsequentialObservation(29),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        ++completion_count;
        ASSERT_TRUE(result);
        terminal_status = result->status;
      }));
  ASSERT_TRUE(adapter_completion);

  broker.CancelTask("task-1", 29,
                    base::BindLambdaForTesting([&]() { ++drain_count; }));
  EXPECT_EQ(1, adapter_cancellations);
  EXPECT_EQ(mojom::EffectStatus::kCancelled, journalled_status);
  EXPECT_EQ(0, completion_count);
  EXPECT_EQ(0, drain_count);
  EXPECT_EQ(1u, broker.pending_count_for_testing());
  ASSERT_TRUE(result_commit);

  std::move(result_commit).Run(true);
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(1, drain_count);
  EXPECT_EQ(mojom::EffectStatus::kCancelled, terminal_status);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
  EXPECT_EQ(0u, broker.late_completion_count_for_testing());
}

} // namespace
} // namespace taffy
