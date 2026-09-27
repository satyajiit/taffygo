// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_task_discovery_test_support.h"

#include <utility>
#include <vector>

#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace {

using discovery_test::LandedNavigateBinding;
using discovery_test::VerifiedResultFor;

// The same errand fixture under the name this suite is about.
using CoreServiceManagerTaskLandingTest =
    discovery_test::CoreServiceManagerTaskDiscoveryTest;

// What a dispatched move's completion says when the tab did not stay where
// the proposal named (decision 0165).

// Decision 0165. A navigation that committed on an origin the ledger holds no
// source for is refused — and the refusal names its code.
//
// It used to send a completion with no `effect_result` at all, which
// `refused_code` in the core's terminal decoder reads as the bare policy
// refusal: `DeniedByPolicy`, whose recovery is do-not-retry and whose sentence
// is "this move is not allowed for this task". A phone spent a
// `browser.navigate`, the navigation landed, and the journal recorded a policy
// denial for a tab that had simply arrived somewhere nobody had been asked
// about yet.
TEST_F(CoreServiceManagerTaskLandingTest,
       ALandingWithNoIssuableSourceNamesTheSiteRatherThanAPolicy) {
  auto binding = LandedNavigateBinding("https://www.search.test",
                                       "https://myaadhaar.uidai.test");
  ActionResult result = VerifiedResultFor(*binding);

  // No window ever opened a tab, so no source can be issued for where it
  // landed and the ledger is never even asked.
  service_mojom::TaskEffectCompletionPtr completion =
      CoreServiceManagerTaskEffectTestPeer::CompleteAction(
          *manager_, std::move(binding), std::move(result));

  ASSERT_TRUE(completion);
  EXPECT_EQ(completion->status,
            service_mojom::TaskEffectCompletionStatus::kRefused);
  ASSERT_TRUE(completion->effect_result);
  EXPECT_EQ(completion->effect_result->status,
            service_mojom::EffectStatus::kDenied);
  ASSERT_TRUE(completion->effect_result->browser_action);
  EXPECT_EQ(completion->effect_result->browser_action->outcome,
            service_mojom::BrowserActionOutcome::kRefused);
  ASSERT_TRUE(completion->effect_result->browser_action->refused_code);
  EXPECT_EQ(completion->effect_result->browser_action->refused_code->code,
            service_mojom::TaskActionResultCode::kEgressNotAuthorized);
}

// The same move that stayed on the origin it started from is not a landing at
// all: nothing is issued, nothing is refused, and the completion succeeds.
TEST_F(CoreServiceManagerTaskLandingTest,
       AMoveThatStayedOnItsOwnOriginNeitherIssuesNorRefuses) {
  auto binding = LandedNavigateBinding("https://myaadhaar.uidai.test",
                                       "https://myaadhaar.uidai.test");
  ActionResult result = VerifiedResultFor(*binding);

  service_mojom::TaskEffectCompletionPtr completion =
      CoreServiceManagerTaskEffectTestPeer::CompleteAction(
          *manager_, std::move(binding), std::move(result));

  ASSERT_TRUE(completion);
  EXPECT_EQ(completion->status,
            service_mojom::TaskEffectCompletionStatus::kSucceeded);
  ASSERT_TRUE(completion->effect_result);
  ASSERT_TRUE(completion->effect_result->browser_action);
  EXPECT_EQ(completion->effect_result->browser_action->outcome,
            service_mojom::BrowserActionOutcome::kCompleted);
  EXPECT_FALSE(completion->effect_result->browser_action->refused_code);
  EXPECT_FALSE(completion->effect_result->browser_action->discovered_source);
}

}  // namespace

}  // namespace taffy
