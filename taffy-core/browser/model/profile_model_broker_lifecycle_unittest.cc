// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/browser/model/profile_model_broker_test_support.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using model_broker_test::kCredential;
using model_broker_test::kGeneration;
using model_broker_test::kTaskId;
using model_broker_test::ModelEffect;
using model_broker_test::ProfileModelBrokerTest;

TEST_F(ProfileModelBrokerTest, CancellingAnOpenRequestAbortsItAndSaysSo) {
  // No armed response, so the request stays open the way a real one is while
  // the provider is thinking.
  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(factory_.NumPending(), 1);
  ASSERT_EQ(terminal_count_, 0);

  broker_->CancelTask(kTaskId, kGeneration);
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(terminal_);
  EXPECT_EQ(terminal_count_, 1);
  // Not CANCELLED. The request was on the wire, so it may have been received,
  // answered and billed already; a ledger told "cancelled" would be recording
  // that no money was spent, which this process does not know.
  EXPECT_EQ(terminal_->status, mojom::EffectStatus::kOutcomeUnknown);
  EXPECT_EQ(broker_->in_flight_count_for_testing(), 0u);
}

TEST_F(ProfileModelBrokerTest, CancellingBeforeAnythingLeftIsACancellation) {
  hold_phase_two_ = true;
  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(factory_.NumPending(), 0);

  broker_->CancelTask(kTaskId, kGeneration);
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(terminal_);
  EXPECT_EQ(terminal_->status, mojom::EffectStatus::kCancelled);

  // The credential the store was still fetching arrives for a call nobody is
  // waiting on. It must not turn into a request, and it must not turn into a
  // second terminal for an effect that already has one.
  std::move(held_).Run(std::string(kCredential), std::nullopt);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(terminal_count_, 1);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerTest, CancellingReachesOnlyTheTaskThatOwnsTheCall) {
  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(factory_.NumPending(), 1);

  broker_->CancelTask("some-other-task", kGeneration);
  broker_->CancelTask(kTaskId, kGeneration + 1u);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(terminal_count_, 0);
  EXPECT_EQ(broker_->in_flight_count_for_testing(), 1u);
}

TEST_F(ProfileModelBrokerTest, ALostGenerationDropsTheRequestWithoutAnswering) {
  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(factory_.NumPending(), 1);

  broker_->CancelGeneration(kGeneration);
  task_environment_.RunUntilIdle();

  // Deliberately no terminal. The effect broker resolves every effect it still
  // holds from a lost generation, so answering here would be a second terminal
  // for one effect — which a spend record would read as two calls.
  EXPECT_EQ(terminal_count_, 0);
  EXPECT_EQ(broker_->in_flight_count_for_testing(), 0u);
}

TEST_F(ProfileModelBrokerTest, ADeadlineAlreadyPastIsRefusedBeforeItIsSent) {
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->operation->deadline_monotonic_ms = 1u;

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDeadlineExceeded);
  // Refused rather than sent with no time left: a request that goes out past
  // its deadline can still be billed.
  EXPECT_EQ(factory_.NumPending(), 0);
}

}  // namespace
}  // namespace taffy
