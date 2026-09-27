// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_task_discovery_test_support.h"

#include <utility>
#include <vector>

#include "base/test/test_future.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace {

using discovery_test::CoreServiceManagerTaskDiscoveryTest;
using discovery_test::DiscoveryEffect;
using discovery_test::DiscoveryPolicyEffect;
using discovery_test::kNewSourceCap;
using discovery_test::NowMonotonicMillis;

// The bootstrap's own lifetime, and the verdict a policy evaluation answers.

TEST_F(CoreServiceManagerTaskDiscoveryTest,
       AnAuthorizedBootstrapWaitsForTheFirstActiveWindowAndThenRuns) {
  ExecuteBootstrap();
  // No window is active: the bootstrap is parked, not refused, and its
  // identity stays pending so idle release cannot run under it.
  EXPECT_TRUE(completions_.empty());
  EXPECT_EQ(DeferredCount(), 1u);
  EXPECT_EQ(PendingCount(), 1u);

  const uint64_t window = manager_->RegisterTaskSourceWindow();
  ASSERT_NE(window, 0u);
  EXPECT_TRUE(completions_.empty());
  EXPECT_EQ(DeferredCount(), 1u);

  // The first activation drains it. The window has no platform bound, so
  // the registry cannot open a tab and the executor now answers with the
  // one status a definite answer takes: refused, from a named branch.
  ASSERT_TRUE(manager_->ActivateTaskSourceWindow(window));
  EXPECT_EQ(completions_,
            std::vector<service_mojom::TaskEffectCompletionStatus>(
                {service_mojom::TaskEffectCompletionStatus::kRefused}));
  EXPECT_EQ(DeferredCount(), 0u);
  EXPECT_EQ(PendingCount(), 0u);
  manager_->UnregisterTaskSourceWindow(window);
}

TEST_F(CoreServiceManagerTaskDiscoveryTest,
       ShutdownAnswersAWaitingBootstrapUnavailableRatherThanDroppingIt) {
  ExecuteBootstrap();
  ASSERT_EQ(DeferredCount(), 1u);

  manager_->Shutdown();
  EXPECT_EQ(completions_,
            std::vector<service_mojom::TaskEffectCompletionStatus>(
                {service_mojom::TaskEffectCompletionStatus::kUnavailable}));
  EXPECT_EQ(DeferredCount(), 0u);
  EXPECT_EQ(PendingCount(), 0u);
}

TEST_F(CoreServiceManagerTaskDiscoveryTest,
       AnUnauthorizedBootstrapIsRefusedAtOnceWithoutWaitingForAWindow) {
  auto effect = DiscoveryEffect(NowMonotonicMillis() + 30'000u,
                                manager_->browser_session_id());
  // A cap other than the one consented to is not this task's authority.
  effect->discovery_bootstrap->remaining_new_source_cap = kNewSourceCap + 1u;
  Execute(std::move(effect));
  EXPECT_EQ(completions_,
            std::vector<service_mojom::TaskEffectCompletionStatus>(
                {service_mojom::TaskEffectCompletionStatus::kRefused}));
  EXPECT_EQ(DeferredCount(), 0u);
  EXPECT_EQ(PendingCount(), 0u);
}

// A refusal the browser understands is a decision about the proposal — it
// names the code the action is settled with, and the task engine and the
// model read it. INVALID_REQUEST is kept for an effect the browser could not
// read at all; answering it for a refusal ended the core.
TEST_F(CoreServiceManagerTaskDiscoveryTest,
       AProposalTheLedgerHoldsNoAuthorityForIsDeniedWithACode) {
  base::test::TestFuture<service_mojom::PolicyEvaluationResultPtr> future;
  // A cap other than the one consented to is not this task's authority.
  CoreServiceManagerTaskEffectTestPeer::EvaluatePolicy(
      *manager_,
      DiscoveryPolicyEffect(NowMonotonicMillis() + 30'000u,
                            manager_->browser_session_id(), kNewSourceCap + 1u),
      future.GetCallback());
  service_mojom::PolicyEvaluationResultPtr result = future.Take();
  ASSERT_TRUE(result);
  EXPECT_EQ(service_mojom::PolicyEvaluationStatus::kDenied, result->status);
  EXPECT_FALSE(result->minted_grant);
  ASSERT_TRUE(result->denial);
  EXPECT_EQ(service_mojom::TaskActionResultCode::kActorLeaseMissing,
            result->denial->code);
}

TEST_F(CoreServiceManagerTaskDiscoveryTest,
       AnAuthorizedProposalOnATabThatIsNotThereIsDeniedAsTabGone) {
  base::test::TestFuture<service_mojom::PolicyEvaluationResultPtr> future;
  // The authority is exact, but no window ever opened the discovery tab.
  CoreServiceManagerTaskEffectTestPeer::EvaluatePolicy(
      *manager_,
      DiscoveryPolicyEffect(NowMonotonicMillis() + 30'000u,
                            manager_->browser_session_id(), kNewSourceCap),
      future.GetCallback());
  service_mojom::PolicyEvaluationResultPtr result = future.Take();
  ASSERT_TRUE(result);
  EXPECT_EQ(service_mojom::PolicyEvaluationStatus::kDenied, result->status);
  ASSERT_TRUE(result->denial);
  EXPECT_EQ(service_mojom::TaskActionResultCode::kTabGone,
            result->denial->code);
}

TEST_F(CoreServiceManagerTaskDiscoveryTest,
       AnEffectTheBrowserCannotReadIsStillAnInvalidRequest) {
  base::test::TestFuture<service_mojom::PolicyEvaluationResultPtr> future;
  auto effect =
      DiscoveryPolicyEffect(NowMonotonicMillis() + 30'000u,
                            manager_->browser_session_id(), kNewSourceCap);
  // A query other than the one the canonical intent digests is not a
  // proposal at all.
  effect->transient_search_query = "something else";
  CoreServiceManagerTaskEffectTestPeer::EvaluatePolicy(
      *manager_, std::move(effect), future.GetCallback());
  service_mojom::PolicyEvaluationResultPtr result = future.Take();
  ASSERT_TRUE(result);
  EXPECT_EQ(service_mojom::PolicyEvaluationStatus::kInvalidRequest,
            result->status);
  EXPECT_FALSE(result->denial);
}

}  // namespace

}  // namespace taffy
