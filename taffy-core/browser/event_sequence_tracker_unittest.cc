// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/event_sequence_tracker.h"

#include "testing/gtest/include/gtest/gtest.h"

// Protocol section 6.3's three outcomes, and the counters that make a
// transport problem visible instead of merely intermittent.

namespace taffy {
namespace {

using Verdict = EventSequenceTracker::Verdict;

TEST(EventSequenceTrackerTest, AcceptsAStrictlyIncreasingRun) {
  EventSequenceTracker tracker;
  for (EventSequence sequence = 1; sequence <= 5; ++sequence) {
    EXPECT_EQ(tracker.Classify(sequence), Verdict::kAccepted);
  }
  EXPECT_EQ(tracker.last_accepted(), 5u);
  EXPECT_EQ(tracker.counters().accepted, 5u);
  EXPECT_EQ(tracker.counters().gaps, 0u);
}

TEST(EventSequenceTrackerTest, DiscardsADuplicateWithoutMovingTheCursor) {
  EventSequenceTracker tracker;
  ASSERT_EQ(tracker.Classify(1), Verdict::kAccepted);
  EXPECT_EQ(tracker.Classify(1), Verdict::kDuplicate);
  EXPECT_EQ(tracker.last_accepted(), 1u);
  EXPECT_EQ(tracker.counters().duplicates, 1u);
  // A duplicate leaves the projection alive: the message it carried was
  // already applied.
  EXPECT_TRUE(VerdictKeepsProjection(Verdict::kDuplicate));
}

TEST(EventSequenceTrackerTest, DiscardsAReorderedArrival) {
  EventSequenceTracker tracker;
  ASSERT_EQ(tracker.Classify(1), Verdict::kAccepted);
  ASSERT_EQ(tracker.Classify(2), Verdict::kAccepted);
  EXPECT_EQ(tracker.Classify(1), Verdict::kOutOfOrder);
  EXPECT_EQ(tracker.last_accepted(), 2u);
  EXPECT_EQ(tracker.counters().out_of_order, 1u);
  EXPECT_TRUE(VerdictKeepsProjection(Verdict::kOutOfOrder));
}

TEST(EventSequenceTrackerTest, AGapKillsTheProjectionAndAdvancesPastIt) {
  EventSequenceTracker tracker;
  ASSERT_EQ(tracker.Classify(1), Verdict::kAccepted);
  EXPECT_EQ(tracker.Classify(4), Verdict::kGap);
  EXPECT_FALSE(VerdictKeepsProjection(Verdict::kGap));

  // Advancing past the gap is deliberate: leaving the cursor behind would turn
  // one lost message into a permanent stream of gap verdicts.
  EXPECT_EQ(tracker.last_accepted(), 4u);
  EXPECT_EQ(tracker.Classify(5), Verdict::kAccepted);
  EXPECT_EQ(tracker.counters().gaps, 1u);
}

TEST(EventSequenceTrackerTest, ResetStartsANewEpochButKeepsTheCounters) {
  EventSequenceTracker tracker;
  ASSERT_EQ(tracker.Classify(1), Verdict::kAccepted);
  ASSERT_EQ(tracker.Classify(9), Verdict::kGap);
  tracker.ResetForNewEpoch();

  EXPECT_EQ(tracker.last_accepted(), 0u);
  EXPECT_EQ(tracker.Classify(1), Verdict::kAccepted);
  // Counters describe the endpoint's behaviour, not one document's, so a new
  // epoch must not erase the evidence that the endpoint dropped a message.
  EXPECT_EQ(tracker.counters().gaps, 1u);
}

TEST(EventSequenceTrackerTest, RebaseMovesTheCursorWithoutInventingAcceptances) {
  EventSequenceTracker tracker;
  ASSERT_EQ(tracker.Classify(1), Verdict::kAccepted);
  ASSERT_EQ(tracker.Classify(7), Verdict::kGap);
  const uint32_t accepted_before = tracker.counters().accepted;

  // A fresh snapshot established where the stream actually is.
  tracker.RebaseTo(20);
  EXPECT_EQ(tracker.last_accepted(), 20u);
  EXPECT_EQ(tracker.Classify(21), Verdict::kAccepted);
  // The messages the resnapshot skipped past were never accepted, and counting
  // them would hide exactly the loss that forced it.
  EXPECT_EQ(tracker.counters().accepted, accepted_before + 1);
  EXPECT_EQ(tracker.counters().gaps, 1u);
}

TEST(EventSequenceTrackerTest, LateRepliesAreCountedSeparately) {
  EventSequenceTracker tracker;
  EXPECT_EQ(tracker.counters().late_after_terminal, 0u);
  tracker.RecordLateAfterTerminal();
  tracker.RecordLateAfterTerminal();
  // Protocol section 6.3 requires both halves: ignored, and counted.
  EXPECT_EQ(tracker.counters().late_after_terminal, 2u);
  EXPECT_EQ(tracker.counters().accepted, 0u);
}

}  // namespace
}  // namespace taffy
