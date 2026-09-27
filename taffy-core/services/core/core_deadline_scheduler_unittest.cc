// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_deadline_scheduler.h"

#include <stdint.h>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(CoreDeadlineSchedulerTest, EmptyAuthorityKeepsTheTimerStopped) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  CoreDeadlineScheduler scheduler;
  int expiries = 0;

  scheduler.Replace(0u, 10u,
                    base::BindLambdaForTesting([&expiries] { ++expiries; }));
  task_environment.FastForwardBy(base::Hours(1));

  EXPECT_FALSE(scheduler.IsScheduled());
  EXPECT_EQ(0u, scheduler.scheduled_deadline_monotonic_ms());
  EXPECT_EQ(0, expiries);
}

TEST(CoreDeadlineSchedulerTest, ReplacementUsesTheNewExactDeadline) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  CoreDeadlineScheduler scheduler;
  int expiries = 0;

  scheduler.Replace(
      110u, 10u,
      base::BindLambdaForTesting([&expiries] { expiries += 100; }));
  scheduler.Replace(40u, 10u,
                    base::BindLambdaForTesting([&expiries] { ++expiries; }));

  task_environment.FastForwardBy(base::Milliseconds(29));
  EXPECT_EQ(0, expiries);
  EXPECT_TRUE(scheduler.IsScheduled());
  EXPECT_EQ(40u, scheduler.scheduled_deadline_monotonic_ms());

  task_environment.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(1, expiries);
  EXPECT_FALSE(scheduler.IsScheduled());
  EXPECT_EQ(0u, scheduler.scheduled_deadline_monotonic_ms());
}

TEST(CoreDeadlineSchedulerTest, ClearingTheAuthorityCancelsTheWakeUp) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  CoreDeadlineScheduler scheduler;
  int expiries = 0;

  scheduler.Replace(20u, 10u,
                    base::BindLambdaForTesting([&expiries] { ++expiries; }));
  ASSERT_TRUE(scheduler.IsScheduled());
  scheduler.Replace(0u, 10u, base::BindLambdaForTesting([] {}));
  task_environment.FastForwardBy(base::Hours(1));

  EXPECT_FALSE(scheduler.IsScheduled());
  EXPECT_EQ(0, expiries);
}

}  // namespace
}  // namespace taffy
