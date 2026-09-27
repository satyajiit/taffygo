// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_broker.h"

#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::EffectEnvelopePtr ToolEffect(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New(
      "operation-tool", generation, 1u, 10'000u, "idempotency-tool");
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

mojom::EffectEnvelopePtr LocalModelToolEffect(uint64_t generation) {
  auto effect = ToolEffect(generation);
  effect->tool_job->runtime = mojom::ToolRuntimeKind::kLocalModel;
  effect->tool_job->operation_kind = mojom::ToolOperation::kGenerateLocalModel;
  effect->tool_job->bundled_python.reset();
  effect->tool_job->local_model = mojom::LocalModelArguments::New(
      "conversation", std::vector<uint8_t>{'h', 'i'}, 4u,
      mojom::ToolModelArtifact::New(
          "model.small", "2026-09-01",
          mojom::ToolModelArtifactKind::kGguf, 1u,
          std::vector<uint8_t>(32u, 1u), "", 0u,
          std::vector<uint8_t>(32u, 0u)));
  return effect;
}

// The tool adapter is reached the same way every other one is, and it is
// reached at all. While `handlers.tool` was never assigned, this effect
// journalled its intent and came straight back UNAVAILABLE - the terminal a
// deliberate refusal produces - so the browser was indistinguishable from one
// that had decided not to run tools.
TEST(CoreEffectBrokerToolTest, AToolJobReachesTheAdapterThatRunsIt) {
  int tool_dispatches = 0;
  std::string dispatched_job;
  std::string dispatched_effect;
  std::string dispatched_operation;
  CoreEffectBroker::ToolCompletionCallback tool_completion;
  mojom::EffectStatus journalled_status = mojom::EffectStatus::kUnavailable;
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
        std::move(done).Run(true);
      });
  handlers.tool = base::BindLambdaForTesting(
      [&](mojom::OperationEnvelopePtr operation, std::string effect_id,
          mojom::ToolJobEffectPtr job,
          CoreEffectBroker::ToolCompletionCallback done) {
        ++tool_dispatches;
        dispatched_effect = std::move(effect_id);
        dispatched_operation = operation->operation_id;
        dispatched_job = job->job_id;
        tool_completion = std::move(done);
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(31u);
  mojom::EffectResultPtr terminal;
  broker.Dispatch(ToolEffect(31u), base::BindLambdaForTesting(
                                       [&](mojom::EffectResultPtr result) {
                                         terminal = std::move(result);
                                       }));

  EXPECT_EQ(1, tool_dispatches);
  EXPECT_EQ("effect-tool", dispatched_effect);
  EXPECT_EQ("job-tool", dispatched_job);
  EXPECT_EQ("operation-tool", dispatched_operation);
  // Nothing is terminal until the worker's own result arrives, which is the
  // difference between dispatching a job and refusing one.
  EXPECT_FALSE(terminal);

  ASSERT_TRUE(tool_completion);
  auto result = mojom::ToolEffectResult::New();
  result->job_id = "job-tool";
  result->status = mojom::ToolTerminalStatus::kCompleted;
  result->success = mojom::ToolSuccess::New();
  result->success->operation_kind =
      mojom::ToolOperation::kRunBundledPythonModule;
  result->success->bundled_python =
      mojom::BundledPythonResult::New(std::vector<uint8_t>{7u});
  std::move(tool_completion).Run(std::move(result));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  EXPECT_EQ(mojom::EffectKind::kToolJob, terminal->kind);
  ASSERT_TRUE(terminal->tool);
  EXPECT_EQ("job-tool", terminal->tool->job_id);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, journalled_status);
}

// The absence that is still a refusal. A profile with no supervisor installed
// has nothing to run the job on, and the effect has to reach exactly one
// terminal rather than wait for a callback nobody holds.
TEST(CoreEffectBrokerToolTest, WithNoAdapterTheJobStillSettlesExactlyOnce) {
  int completions = 0;
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

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(31u);
  mojom::EffectResultPtr terminal;
  broker.Dispatch(ToolEffect(31u), base::BindLambdaForTesting(
                                       [&](mojom::EffectResultPtr result) {
                                         ++completions;
                                         terminal = std::move(result);
                                       }));

  EXPECT_EQ(1, completions);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, terminal->status);
  ASSERT_TRUE(terminal->tool);
  EXPECT_EQ(mojom::ToolTerminalStatus::kRuntimeCrashed, terminal->tool->status);
}

TEST(CoreEffectBrokerToolTest,
     LocalRuntimeRefusalNeverEntersProviderModelAccounting) {
  int tool_dispatches = 0;
  int provider_model_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  handlers.model = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback) {
        ++provider_model_dispatches;
      });
  handlers.tool = base::BindLambdaForTesting(
      [&](mojom::OperationEnvelopePtr, std::string,
          mojom::ToolJobEffectPtr job,
          CoreEffectBroker::ToolCompletionCallback done) {
        ++tool_dispatches;
        auto refusal = mojom::ToolEffectResult::New();
        refusal->job_id = job->job_id;
        refusal->status =
            mojom::ToolTerminalStatus::kLocalRuntimeUnavailable;
        std::move(done).Run(std::move(refusal));
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(31u);
  mojom::EffectResultPtr terminal;
  broker.Dispatch(LocalModelToolEffect(31u), base::BindLambdaForTesting(
                                             [&](mojom::EffectResultPtr result) {
                                               terminal = std::move(result);
                                             }));

  EXPECT_EQ(1, tool_dispatches);
  EXPECT_EQ(0, provider_model_dispatches);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, terminal->status);
  ASSERT_TRUE(terminal->tool);
  EXPECT_EQ(mojom::ToolTerminalStatus::kLocalRuntimeUnavailable,
            terminal->tool->status);
}

}  // namespace
}  // namespace taffy
