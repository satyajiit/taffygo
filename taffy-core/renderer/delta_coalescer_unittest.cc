// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/delta_coalescer.h"

#include <algorithm>

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

// Protocol section 10. The property under test is the drop order: optional
// signals go first, node removals and lifecycle signals never go at all, and
// when they cannot fit the stream invalidates rather than thins out.

namespace taffy {
namespace {

DeltaCoalescer::Signal MakeSignal(DeltaCoalescer::SignalClass signal_class,
                                  int node_ordinal,
                                  size_t bytes = 16) {
  DeltaCoalescer::Signal signal;
  signal.signal_class = signal_class;
  signal.node_id = SemanticNodeId("n" + base::NumberToString(node_ordinal));
  signal.revision = GraphRevision(1);
  signal.estimated_bytes = bytes;
  return signal;
}

// Small enough to overflow in four statements. Production code cannot build
// one of these: the only other constructor takes the limits policy object,
// and nothing in this component can produce a value for that.
DeltaCoalescer::Limits TinyLimits() {
  return DeltaCoalescer::Limits::ForTesting(/*max_queued_signals=*/3,
                                            /*max_queued_bytes=*/1024,
                                            base::Milliseconds(100));
}

TEST(DeltaCoalescerTest, CoalescesRepeatedSignalsForOneNode) {
  DeltaCoalescer coalescer(TinyLimits());
  EXPECT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1)));
  EXPECT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1)));
  EXPECT_EQ(coalescer.queued_signal_count(), 1u);
}

TEST(DeltaCoalescerTest, CoalescedSignalKeepsItsLatestArrivalOrder) {
  DeltaCoalescer coalescer(DeltaCoalescer::Limits::ForTesting(
      /*max_queued_signals=*/16, /*max_queued_bytes=*/1024,
      base::Milliseconds(100)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 2)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1)));

  DeltaCoalescer::Batch batch =
      coalescer.Flush(GraphRevision(1), GraphRevision(2));
  ASSERT_EQ(batch.signals.size(), 2u);
  EXPECT_EQ(batch.signals[0].node_id, SemanticNodeId("n2"));
  EXPECT_EQ(batch.signals[1].node_id, SemanticNodeId("n1"));
}

TEST(DeltaCoalescerTest, CoalescingIndexSurvivesOverflowCompaction) {
  DeltaCoalescer coalescer(TinyLimits());
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kStateChanged, 2)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 3)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 4)));

  // Overflow removed n1 and compacted the vector. Replacing n2 must still
  // find the state signal at its new slot rather than appending a duplicate
  // through a stale index.
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kStateChanged, 2)));
  const DeltaCoalescer::Batch batch =
      coalescer.Flush(GraphRevision(1), GraphRevision(2));
  EXPECT_EQ(std::ranges::count_if(
                batch.signals,
                [](const auto& signal) {
                  return signal.node_id == SemanticNodeId("n2") &&
                         signal.signal_class ==
                             DeltaCoalescer::SignalClass::kStateChanged;
                }),
            1);
}

TEST(DeltaCoalescerTest,
     GrowingCoalescedRequiredSignalCannotBypassTheByteBudget) {
  DeltaCoalescer coalescer(DeltaCoalescer::Limits::ForTesting(
      /*max_queued_signals=*/3, /*max_queued_bytes=*/32,
      base::Milliseconds(100)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 1, /*bytes=*/16)));

  // The second signal replaces rather than appends the first one. It is still
  // subject to the byte ceiling: a required signal too large to represent
  // invalidates the projection instead of sitting in an over-budget queue.
  EXPECT_FALSE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 1, /*bytes=*/33)));
  EXPECT_TRUE(coalescer.projection_invalid());
}

TEST(DeltaCoalescerTest, GrowingCoalescedOptionalSignalIsDropped) {
  DeltaCoalescer coalescer(DeltaCoalescer::Limits::ForTesting(
      /*max_queued_signals=*/3, /*max_queued_bytes=*/32,
      base::Milliseconds(100)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1, /*bytes=*/16)));

  // The new observation supersedes the old one, but cannot itself fit. The
  // coalescer may shed this optional class; retaining the stale 16-byte signal
  // would claim the earlier revision still represented the latest mutation.
  EXPECT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1, /*bytes=*/33)));
  EXPECT_EQ(coalescer.queued_signal_count(), 0u);
  EXPECT_EQ(coalescer.queued_bytes(), 0u);
  EXPECT_EQ(coalescer.Flush(GraphRevision(1), GraphRevision(2))
                .dropped_optional_signals,
            1u);
}

TEST(DeltaCoalescerTest, DropsOptionalSignalsBeforeStructuralOnes) {
  DeltaCoalescer coalescer(TinyLimits());
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 1)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kStateChanged, 2)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 3)));
  // Over the signal limit: the text change is the one that goes.
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 4)));

  DeltaCoalescer::Batch batch =
      coalescer.Flush(GraphRevision(1), GraphRevision(2));
  EXPECT_FALSE(batch.requires_resnapshot);
  EXPECT_EQ(batch.dropped_optional_signals, 1u);
  for (const DeltaCoalescer::Signal& signal : batch.signals) {
    EXPECT_NE(signal.signal_class, DeltaCoalescer::SignalClass::kTextChanged);
  }
}

TEST(DeltaCoalescerTest, NeverDropsRemovalsOrLifecycle) {
  DeltaCoalescer coalescer(TinyLimits());
  for (int i = 1; i <= 3; ++i) {
    ASSERT_TRUE(coalescer.Queue(
        MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, i)));
  }
  // Nothing droppable is left, so the stream invalidates instead of losing a
  // removal. A subscriber that missed a removal would hold a handle to
  // something that is gone.
  EXPECT_FALSE(
      coalescer.Queue(MakeSignal(DeltaCoalescer::SignalClass::kLifecycle, 4)));
  EXPECT_TRUE(coalescer.projection_invalid());

  DeltaCoalescer::Batch batch =
      coalescer.Flush(GraphRevision(1), GraphRevision(2));
  EXPECT_TRUE(batch.requires_resnapshot);
  EXPECT_TRUE(batch.signals.empty());
}

TEST(DeltaCoalescerTest, EmitsInDropOrder) {
  // ForTesting, because Limits has no default constructor and no public one:
  // its only production constructor takes the policy object, so a call site
  // cannot invent a bound. That is deliberate — see the comment on Limits.
  DeltaCoalescer coalescer(DeltaCoalescer::Limits::ForTesting(
      /*max_queued_signals=*/16,
      /*max_queued_bytes=*/1024, base::Milliseconds(100)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 1)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kTextChanged, 2)));
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kStateChanged, 3)));

  DeltaCoalescer::Batch batch =
      coalescer.Flush(GraphRevision(1), GraphRevision(2));
  ASSERT_EQ(batch.signals.size(), 3u);
  EXPECT_TRUE(std::ranges::is_sorted(
      batch.signals,
      [](const DeltaCoalescer::Signal& a, const DeltaCoalescer::Signal& b) {
        return a.signal_class < b.signal_class;
      }));
}

TEST(DeltaCoalescerTest, PausedDropsOptionalSignalsOnArrival) {
  DeltaCoalescer coalescer(TinyLimits());
  coalescer.SetPaused(true);
  EXPECT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kLayoutChanged, 1)));
  EXPECT_EQ(coalescer.queued_signal_count(), 0u);

  // A removal still gets through while paused.
  EXPECT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kNodeRemoved, 2)));
  EXPECT_EQ(coalescer.queued_signal_count(), 1u);
}

TEST(DeltaCoalescerTest, ActionBarrierStopsBatching) {
  DeltaCoalescer coalescer(TinyLimits());
  ASSERT_TRUE(coalescer.Queue(
      MakeSignal(DeltaCoalescer::SignalClass::kStateChanged, 1)));
  const base::TimeTicks now = base::TimeTicks::Now();
  EXPECT_TRUE(coalescer.ShouldKeepBatching(now,
                                           /*action_barrier_active=*/false));
  // Spec section 5.3: no coalescing across an action preflight boundary.
  EXPECT_FALSE(coalescer.ShouldKeepBatching(now,
                                            /*action_barrier_active=*/true));
}

}  // namespace
}  // namespace taffy
