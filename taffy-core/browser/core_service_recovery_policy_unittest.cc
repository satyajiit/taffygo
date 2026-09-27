// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_recovery_policy.h"

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

TEST(CoreServiceRecoveryPolicyTest, UsesBoundedBackoffAndOpensThirdCrash) {
  CoreServiceRecoveryPolicy policy;
  const base::TimeTicks start = base::TimeTicks::Now();

  const auto first = policy.RecordUnexpectedDisconnect(start);
  EXPECT_TRUE(first.schedule_automatic_restart);
  EXPECT_EQ(base::Milliseconds(250), first.delay);

  const auto second =
      policy.RecordUnexpectedDisconnect(start + base::Seconds(10));
  EXPECT_TRUE(second.schedule_automatic_restart);
  EXPECT_EQ(base::Seconds(1), second.delay);

  const auto third =
      policy.RecordUnexpectedDisconnect(start + base::Seconds(20));
  EXPECT_FALSE(third.schedule_automatic_restart);
  EXPECT_EQ(base::Seconds(4), third.delay);
  EXPECT_TRUE(policy.circuit_open());
}

TEST(CoreServiceRecoveryPolicyTest, WindowAndExplicitRetryStartFreshAudit) {
  CoreServiceRecoveryPolicy policy;
  const base::TimeTicks start = base::TimeTicks::Now();

  policy.RecordUnexpectedDisconnect(start);
  policy.RecordUnexpectedDisconnect(start + base::Seconds(1));
  policy.RecordUnexpectedDisconnect(start + base::Seconds(2));
  ASSERT_TRUE(policy.circuit_open());

  policy.RetryExplicitly();
  EXPECT_FALSE(policy.circuit_open());
  EXPECT_EQ(0u, policy.unexpected_disconnect_count_for_testing(start));

  const auto after_window = policy.RecordUnexpectedDisconnect(
      start + CoreServiceRecoveryPolicy::kRestartWindow + base::Seconds(1));
  EXPECT_TRUE(after_window.schedule_automatic_restart);
  EXPECT_EQ(base::Milliseconds(250), after_window.delay);
}

TEST(CoreServiceRecoveryPolicyTest, CleanTeardownHasNoMutationApi) {
  CoreServiceRecoveryPolicy policy;
  const base::TimeTicks now = base::TimeTicks::Now();

  // Manager classifies clean shutdown and idle teardown without calling
  // RecordUnexpectedDisconnect. The policy intentionally exposes no method
  // that could count either event.
  EXPECT_EQ(0u, policy.unexpected_disconnect_count_for_testing(now));
  EXPECT_FALSE(policy.circuit_open());
}

TEST(CoreServiceRecoveryPolicyTest, GenerationExhaustionCannotBeRetried) {
  CoreServiceRecoveryPolicy policy;
  policy.OpenCircuitForSession();
  ASSERT_TRUE(policy.circuit_open());

  policy.RetryExplicitly();
  EXPECT_TRUE(policy.circuit_open());
}

}  // namespace taffy
