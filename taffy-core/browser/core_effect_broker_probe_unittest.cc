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

mojom::EffectEnvelopePtr ProbeEffect(mojom::RetryClass retry_class) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New(
      "probe-operation", 7u, 0u, 10'000u, "probe-idempotency");
  effect->effect_id = "provider-probe-fixture-session-7-1";
  effect->kind = mojom::EffectKind::kModelRequest;
  effect->retry_class = retry_class;
  effect->model_request = mojom::ModelRequestEffect::New();
  effect->model_request->route_id = "direct-user-key";
  effect->model_request->model_id = "fixture-model";
  effect->model_request->disclosure = mojom::DisclosureClass::kContentFree;
  effect->model_request->request_body = {1u};
  effect->model_request->max_output_bytes = 4096u;
  effect->model_request->provider_id = "fixture-provider";
  effect->model_request->wire_api = mojom::ProviderWireApi::kOpenAiResponses;
  effect->model_request->endpoint = "https://provider.example.test";
  effect->model_request->probe = true;
  effect->model_request->endpoint_kind =
      mojom::ModelEndpointKind::kCatalogOrigin;
  return effect;
}

mojom::EffectEnvelopePtr ComposerEffect() {
  mojom::EffectEnvelopePtr effect = ProbeEffect(mojom::RetryClass::kNever);
  effect->effect_id = "composer-completion-browser-session-7-1";
  effect->model_request->route_id = "composer";
  effect->model_request->disclosure =
      mojom::DisclosureClass::kUserSelectedContent;
  effect->model_request->probe = false;
  return effect;
}

mojom::EffectEnvelopePtr TaskModelEffect() {
  mojom::EffectEnvelopePtr effect = ComposerEffect();
  effect->effect_id = "task-model-effect";
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->model_request->task_id = "task-1";
  return effect;
}

mojom::EffectResultPtr SuccessfulProbe(const mojom::EffectEnvelope& effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kModelRequest;
  result->model = mojom::ModelEffectResult::New();
  result->model->model_id = effect.model_request->model_id;
  return result;
}

TEST(CoreEffectBrokerProbeTest,
     CredentialProbeUsesNoDurableIntentOrResultJournal) {
  int intent_commits = 0;
  int result_commits = 0;
  int model_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [&](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        ++intent_commits;
        std::move(done).Run(true);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [&](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        ++result_commits;
        std::move(done).Run(true);
      });
  handlers.model = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++model_dispatches;
        std::move(done).Run(SuccessfulProbe(*effect));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7u);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      ProbeEffect(mojom::RetryClass::kNever),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  EXPECT_EQ(0, intent_commits);
  EXPECT_EQ(0, result_commits);
  EXPECT_EQ(1, model_dispatches);
  EXPECT_EQ(0u, broker.pending_count_for_testing());
}

TEST(CoreEffectBrokerProbeTest,
     TasklessComposerUsesNoDurableIntentOrResultJournal) {
  int intent_commits = 0;
  int result_commits = 0;
  int model_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [&](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        ++intent_commits;
        std::move(done).Run(true);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [&](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        ++result_commits;
        std::move(done).Run(true);
      });
  handlers.model = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++model_dispatches;
        std::move(done).Run(SuccessfulProbe(*effect));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7u);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      ComposerEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  EXPECT_EQ(0, intent_commits);
  EXPECT_EQ(0, result_commits);
  EXPECT_EQ(1, model_dispatches);
}

TEST(CoreEffectBrokerProbeTest, TaskModelRequestRemainsDurablyJournalled) {
  int intent_commits = 0;
  int result_commits = 0;
  int model_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [&](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        ++intent_commits;
        std::move(done).Run(true);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [&](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        ++result_commits;
        std::move(done).Run(true);
      });
  handlers.model = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++model_dispatches;
        std::move(done).Run(SuccessfulProbe(*effect));
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7u);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      TaskModelEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  EXPECT_EQ(1, intent_commits);
  EXPECT_EQ(1, result_commits);
  EXPECT_EQ(1, model_dispatches);
}

TEST(CoreEffectBrokerProbeTest, ProbeWithRetryableSemanticsIsRejected) {
  int model_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.model = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr,
          CoreEffectBroker::CompletionCallback) { ++model_dispatches; });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(7u);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      ProbeEffect(mojom::RetryClass::kIdempotent),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  EXPECT_EQ(0, model_dispatches);
}

}  // namespace
}  // namespace taffy
