// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_broker.h"

#include <string>
#include <utility>

#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::EffectEnvelopePtr ConsequentialObservation(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New(
      "identity-operation", generation, 2u, 10'000u, "identity-key");
  effect->effect_id = "identity-effect";
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
  effect->page_observation->authority_subject->authority_subject_id = "task-1";
  effect->page_observation->capability_id = "capability-1";
  effect->page_observation->proposal_digest = std::string(64u, 'a');
  effect->page_observation->idempotency_key = "identity-key";
  effect->page_observation->scope =
      mojom::ObservationScope::kCurrentDocument;
  effect->page_observation->max_bytes = 4096u;
  return effect;
}

TEST(CoreEffectBrokerIdentityTest,
     ExistingConsequentialIntentReportsOutcomeUnknownWithoutDispatch) {
  int observation_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(false);
      });
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback) {
        ++observation_dispatches;
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(11);
  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      ConsequentialObservation(11),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        terminal = std::move(result);
      }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, terminal->status);
  EXPECT_EQ(0, observation_dispatches);
}

TEST(CoreEffectBrokerIdentityTest,
     DisconnectBeforeIntentCallbackCannotDispatchRevokedWork) {
  CoreEffectBroker::JournalCallback intent_commit;
  CoreEffectBroker::JournalCallback result_commit;
  int observation_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent = base::BindLambdaForTesting(
      [&](const mojom::EffectEnvelope&, CoreEffectBroker::JournalCallback done) {
        intent_commit = std::move(done);
      });
  handlers.commit_result = base::BindLambdaForTesting(
      [&](const mojom::EffectResult&, CoreEffectBroker::JournalCallback done) {
        result_commit = std::move(done);
      });
  handlers.observation = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr, CoreEffectBroker::CompletionCallback) {
        ++observation_dispatches;
      });

  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(12);
  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      ConsequentialObservation(12),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        terminal = std::move(result);
      }));
  ASSERT_TRUE(intent_commit);

  broker.OnGenerationDisconnected(12);
  ASSERT_TRUE(result_commit);
  std::move(intent_commit).Run(true);
  EXPECT_EQ(0, observation_dispatches);
  EXPECT_FALSE(terminal);

  std::move(result_commit).Run(false);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown, terminal->status);
  EXPECT_EQ(0, observation_dispatches);
}

}  // namespace
}  // namespace taffy
