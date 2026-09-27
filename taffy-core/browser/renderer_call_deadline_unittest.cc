// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/functional/callback_helpers.h"
#include "taffy/components/intelligence/content/renderer_call_deadline.h"

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// "A renderer timeout produces a bounded failure; it does not stall UI or
// policy cancellation" (protocol section 15), and the half of protocol section
// 6.3 that says a late reply is dropped and counted.
//
// The property under test is exactly-once: whichever of the reply and the
// deadline arrives first settles the call, and the other does nothing.

namespace taffy {
namespace {

class RendererCallDeadlineTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(RendererCallDeadlineTest, TheReplyClaimsTheCallAndTheTimerDoesNot) {
  int timeouts = 0;
  scoped_refptr<RendererCallDeadline> guard = RendererCallDeadline::Arm(
      base::Seconds(2),
      base::BindOnce([](int* counter) { ++*counter; }, &timeouts));

  EXPECT_TRUE(guard->Claim());
  task_environment_.FastForwardBy(base::Seconds(5));
  EXPECT_EQ(timeouts, 0);
}

TEST_F(RendererCallDeadlineTest, TheDeadlineClaimsTheCallWhenNothingElseDoes) {
  int timeouts = 0;
  scoped_refptr<RendererCallDeadline> guard = RendererCallDeadline::Arm(
      base::Seconds(2),
      base::BindOnce([](int* counter) { ++*counter; }, &timeouts));

  task_environment_.FastForwardBy(base::Seconds(3));
  EXPECT_EQ(timeouts, 1);
  EXPECT_TRUE(guard->claimed());
  // The late reply that follows finds the call already settled and is dropped.
  EXPECT_FALSE(guard->Claim());
}

TEST_F(RendererCallDeadlineTest, ClaimIsTrueExactlyOnce) {
  scoped_refptr<RendererCallDeadline> guard =
      RendererCallDeadline::Arm(base::Seconds(2), base::DoNothing());
  EXPECT_TRUE(guard->Claim());
  EXPECT_FALSE(guard->Claim());
  EXPECT_FALSE(guard->Claim());
}

TEST_F(RendererCallDeadlineTest, AWrappedReplyRunsOnlyIfItClaimsFirst) {
  int replies = 0;
  int timeouts = 0;
  scoped_refptr<RendererCallDeadline> guard = RendererCallDeadline::Arm(
      base::Seconds(2),
      base::BindOnce([](int* counter) { ++*counter; }, &timeouts));

  base::OnceCallback<void(int)> wrapped = BindReplyWithDeadline(
      guard, base::BindOnce([](int* counter, int value) { *counter += value; },
                            &replies));

  std::move(wrapped).Run(1);
  EXPECT_EQ(replies, 1);
  task_environment_.FastForwardBy(base::Seconds(5));
  EXPECT_EQ(timeouts, 0);
}

TEST_F(RendererCallDeadlineTest, AWrappedReplyAfterTheDeadlineIsDropped) {
  int replies = 0;
  int timeouts = 0;
  scoped_refptr<RendererCallDeadline> guard = RendererCallDeadline::Arm(
      base::Seconds(2),
      base::BindOnce([](int* counter) { ++*counter; }, &timeouts));

  base::OnceCallback<void(int)> wrapped = BindReplyWithDeadline(
      guard, base::BindOnce([](int* counter, int value) { *counter += value; },
                            &replies));

  task_environment_.FastForwardBy(base::Seconds(3));
  ASSERT_EQ(timeouts, 1);

  std::move(wrapped).Run(1);
  // Ignored, because the deadline already produced the one terminal result.
  EXPECT_EQ(replies, 0);
}

}  // namespace
}  // namespace taffy
