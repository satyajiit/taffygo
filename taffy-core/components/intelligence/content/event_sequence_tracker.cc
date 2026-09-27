// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/event_sequence_tracker.h"

namespace taffy {

EventSequenceTracker::Verdict EventSequenceTracker::Classify(
    EventSequence event_sequence) {
  if (event_sequence == last_accepted_) {
    ++counters_.duplicates;
    return Verdict::kDuplicate;
  }
  if (event_sequence < last_accepted_) {
    ++counters_.out_of_order;
    return Verdict::kOutOfOrder;
  }
  if (event_sequence != last_accepted_ + 1) {
    // A skipped number. Advance past it so that one lost message does not turn
    // every subsequent message into another gap; the projection is discarded
    // by the caller either way.
    ++counters_.gaps;
    last_accepted_ = event_sequence;
    return Verdict::kGap;
  }
  ++counters_.accepted;
  last_accepted_ = event_sequence;
  return Verdict::kAccepted;
}

void EventSequenceTracker::RecordLateAfterTerminal() {
  ++counters_.late_after_terminal;
}

void EventSequenceTracker::ResetForNewEpoch() {
  last_accepted_ = 0;
}

void EventSequenceTracker::RebaseTo(EventSequence event_sequence) {
  last_accepted_ = event_sequence;
}

}  // namespace taffy
