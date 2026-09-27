// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_EVENT_SEQUENCE_TRACKER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_EVENT_SEQUENCE_TRACKER_H_

#include <stdint.h>

#include "taffy/common/public/bip_identity.h"

// Per-epoch message ordering (protocol section 6.3).
//
// "Each frame endpoint emits a strictly increasing event_sequence per epoch.
// The broker discards duplicates and invalid order. A detected gap invalidates
// delta state and requires a fresh snapshot."
//
// Three sentences, three distinct outcomes, and conflating any two of them is
// a real defect rather than a tidiness question:
//
//   accepted   the message is the next one. Apply it.
//   discarded  a duplicate or a reordered arrival. The projection is still
//              correct, because whatever this message carried was either
//              already applied or is superseded. Drop it and count it.
//   gap        a sequence number was skipped. Something happened that nobody
//              can name, so the projection is dead: invalidate delta state
//              and force a fresh snapshot.
//
// The counters are not diagnostics-as-an-afterthought. Protocol section 6.3
// requires that late renderer responses after cancellation are "ignored and
// counted", and sustained reordering or duplication is the signature of a
// transport problem that would otherwise look like an occasionally stale page.
//
// This is a pure value type with no Chromium dependency, so the ordering rules
// are provable on a host without a browser.

namespace taffy {

class EventSequenceTracker {
 public:
  enum class Verdict : uint8_t {
    kAccepted = 0,
    kDuplicate = 1,
    kOutOfOrder = 2,
    kGap = 3,
  };

  struct Counters {
    uint32_t accepted = 0;
    uint32_t duplicates = 0;
    uint32_t out_of_order = 0;
    uint32_t gaps = 0;
    // Replies that arrived after their request had already produced a terminal
    // result. Counted here rather than at each call site so there is one
    // number for "the renderer answered too late", whatever the reason.
    uint32_t late_after_terminal = 0;
  };

  EventSequenceTracker() = default;

  // Classifies one incoming sequence number and advances the cursor when the
  // verdict allows it.
  //
  // A gap advances the cursor to the observed value. That is deliberate: the
  // projection is being thrown away anyway, and leaving the cursor behind
  // would turn one gap into a permanent stream of gap verdicts for every
  // message that followed.
  Verdict Classify(EventSequence event_sequence);

  // Records that a reply arrived after its request was already terminal.
  void RecordLateAfterTerminal();

  // Resets the cursor for a new epoch. Counters survive, because they describe
  // the endpoint's behaviour rather than one document's.
  void ResetForNewEpoch();

  // Moves the cursor to a sequence number established out of band — the one a
  // fresh snapshot reported. Counters are untouched on purpose: a resnapshot
  // is not evidence that the messages it skipped past were ever accepted, and
  // counting them as accepted would hide exactly the loss that caused it.
  void RebaseTo(EventSequence event_sequence);

  EventSequence last_accepted() const { return last_accepted_; }
  const Counters& counters() const { return counters_; }

 private:
  EventSequence last_accepted_ = 0;
  Counters counters_;
};

// True when the verdict leaves the subscriber's projection usable. Only a gap
// does not.
constexpr bool VerdictKeepsProjection(EventSequenceTracker::Verdict verdict) {
  return verdict != EventSequenceTracker::Verdict::kGap;
}

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_EVENT_SEQUENCE_TRACKER_H_
