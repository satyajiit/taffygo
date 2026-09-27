// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/observation_limits.h"

#include "testing/gtest/include/gtest/gtest.h"

// What is under test here is not the values - they are placeholders owned by
// `[Open (OD-031)]` and they will all change. It is the two properties that
// have to hold whatever the values become: a request can narrow and can never
// widen, and a request that states nothing gets the ceiling rather than zero.

namespace taffy {
namespace {

TEST(ObservationLimitsTest, CeilingIsStableAndNonZero) {
  const ObservationLimits& first = ObservationLimits::ProcessSafeCeiling();
  const ObservationLimits& second = ObservationLimits::ProcessSafeCeiling();
  // Returned by reference and identical, so an adapter that held one cannot
  // be handed a different policy half way through an extraction.
  EXPECT_EQ(&first, &second);

  // A zero bound would make every extraction empty while still reporting
  // success, which is exactly the failure shape protocol section 15 is
  // written against.
  EXPECT_GT(first.snapshot().max_nodes(), 0u);
  EXPECT_GT(first.snapshot().max_text_bytes(), 0u);
  EXPECT_GT(first.snapshot().max_total_bytes(), 0u);
  EXPECT_GT(first.snapshot().max_depth(), 0u);
  EXPECT_GT(first.snapshot().max_frames(), 0u);
  EXPECT_FALSE(first.snapshot().deadline().is_zero());
  EXPECT_GT(first.delta().max_queued_signals(), 0u);
  EXPECT_GT(first.delta().max_queued_bytes(), 0u);
  EXPECT_GT(first.delta().estimated_signal_bytes(), 0u);
  EXPECT_GT(first.delta().max_tracked_cancellations(), 0u);
  EXPECT_GT(first.structured_data().max_block_bytes(), 0u);
  EXPECT_GT(first.structured_data().max_json_depth(), 0);
  EXPECT_GT(first.structured_data().max_blocks(), 0u);
  EXPECT_GT(first.forms().max_forms(), 0u);
  EXPECT_GT(first.forms().max_controls_per_form(), 0u);
  EXPECT_GT(first.forms().max_formless_controls(), 0u);
  EXPECT_GT(first.selection().max_text_bytes(), 0u);
  EXPECT_GT(first.selection().max_nodes(), 0u);
  EXPECT_GT(first.layout().max_occlusion_probes(), 0u);
  EXPECT_GT(first.redaction().min_digit_run(), 0u);
  EXPECT_GT(first.redaction().min_hex_run(), 0u);
  EXPECT_GT(first.redaction().min_token_run(), 0u);
  EXPECT_GT(first.redaction().min_seed_phrase_words(), 0u);
  EXPECT_GT(first.redaction().max_text_scan_bytes(), 0u);
}

TEST(ObservationLimitsTest, RequestCanNarrow) {
  const ObservationLimits& ceiling = ObservationLimits::ProcessSafeCeiling();
  RequestedSnapshotBudget requested;
  requested.max_nodes = 7;
  requested.max_text_bytes = 11;
  requested.max_total_bytes = 13;
  requested.max_depth = 3;
  requested.deadline_ms = 5;

  const ObservationLimits narrowed = ceiling.NarrowedTo(requested);
  EXPECT_EQ(narrowed.snapshot().max_nodes(), 7u);
  EXPECT_EQ(narrowed.snapshot().max_text_bytes(), 11u);
  EXPECT_EQ(narrowed.snapshot().max_total_bytes(), 13u);
  EXPECT_EQ(narrowed.snapshot().max_depth(), 3u);
  EXPECT_EQ(narrowed.snapshot().deadline_ms(), 5u);
}

TEST(ObservationLimitsTest, RequestCanNeverWiden) {
  const ObservationLimits& ceiling = ObservationLimits::ProcessSafeCeiling();
  RequestedSnapshotBudget requested;
  requested.max_nodes = ceiling.snapshot().max_nodes() + 1000;
  requested.max_text_bytes = ceiling.snapshot().max_text_bytes() + 1000;
  requested.max_total_bytes = ceiling.snapshot().max_total_bytes() + 1000;
  requested.max_depth = ceiling.snapshot().max_depth() + 1000;
  requested.deadline_ms = ceiling.snapshot().deadline_ms() + 1000;

  const ObservationLimits narrowed = ceiling.NarrowedTo(requested);
  EXPECT_EQ(narrowed.snapshot().max_nodes(), ceiling.snapshot().max_nodes());
  EXPECT_EQ(narrowed.snapshot().max_text_bytes(),
            ceiling.snapshot().max_text_bytes());
  EXPECT_EQ(narrowed.snapshot().max_total_bytes(),
            ceiling.snapshot().max_total_bytes());
  EXPECT_EQ(narrowed.snapshot().max_depth(), ceiling.snapshot().max_depth());
  EXPECT_EQ(narrowed.snapshot().deadline_ms(),
            ceiling.snapshot().deadline_ms());
}

TEST(ObservationLimitsTest, UnstatedRequestKeepsTheCeiling) {
  // A defaulted request is a broker that stated no preference. Reading it as
  // "zero" would produce an empty extraction that still reported success.
  const ObservationLimits& ceiling = ObservationLimits::ProcessSafeCeiling();
  const ObservationLimits narrowed =
      ceiling.NarrowedTo(RequestedSnapshotBudget());
  EXPECT_EQ(narrowed.snapshot().max_nodes(), ceiling.snapshot().max_nodes());
  EXPECT_EQ(narrowed.snapshot().deadline_ms(),
            ceiling.snapshot().deadline_ms());
}

TEST(ObservationLimitsTest, NarrowingLeavesProcessPropertiesAlone) {
  // Structured-data depth, pattern-scan length, and queue depth are
  // properties of this process, not of a request. A broker cannot ask for a
  // deeper parse of attacker-authored JSON.
  const ObservationLimits& ceiling = ObservationLimits::ProcessSafeCeiling();
  RequestedSnapshotBudget requested;
  requested.max_nodes = 1;
  const ObservationLimits narrowed = ceiling.NarrowedTo(requested);

  EXPECT_EQ(narrowed.structured_data().max_json_depth(),
            ceiling.structured_data().max_json_depth());
  EXPECT_EQ(narrowed.redaction().max_text_scan_bytes(),
            ceiling.redaction().max_text_scan_bytes());
  EXPECT_EQ(narrowed.delta().max_queued_signals(),
            ceiling.delta().max_queued_signals());
  EXPECT_EQ(narrowed.layout().max_occlusion_probes(),
            ceiling.layout().max_occlusion_probes());
}

TEST(ObservationLimitsTest, CoalescingWindowIsClampedBothWays) {
  const DeltaLimits& delta = ObservationLimits::ProcessSafeCeiling().delta();
  EXPECT_EQ(delta.ClampCoalescingWindow(base::Milliseconds(0)),
            delta.min_interval());
  EXPECT_GE(delta.ClampCoalescingWindow(base::Hours(1)), delta.min_interval());
  EXPECT_LE(delta.ClampCoalescingWindow(base::Hours(1)),
            base::Milliseconds(1000));
  const base::TimeDelta inside = delta.min_interval() * 2;
  EXPECT_EQ(delta.ClampCoalescingWindow(inside), inside);
}

}  // namespace
}  // namespace taffy
