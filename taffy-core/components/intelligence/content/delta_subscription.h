// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_SUBSCRIPTION_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_SUBSCRIPTION_H_

#include <stdint.h>

#include <optional>
#include <utility>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/functional/callback.h"
#include "taffy/components/intelligence/content/event_sequence_tracker.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_subscription.h"

// One delta subscription's projection cursor, queue and state machine
// (protocol section 10).
//
// What a subscription actually owns is a promise: every delta this object
// hands onward applies exactly to the projection the subscriber last
// acknowledged. Epoch, from-revision and sequence all matched, in that order,
// and nothing partial was applied. When that promise cannot be kept the
// subscription says so and asks for a fresh snapshot; it never delivers a
// delta it cannot prove applies.
//
// The queue is real, and it is what backpressure is measured against. Two
// things sit in it:
//
//   pending         received from the renderer, not yet delivered. Non-empty
//                   only while the subscription is paused.
//   unacknowledged  delivered to the subscriber, which has not yet said it
//                   applied them.
//
// A subscriber that stops acknowledging is what queue growth actually looks
// like, so acknowledgement is the signal the ladder in
// delta_backpressure_policy.h climbs. Without it, "queue depth" would be a
// number this process made up.
//
// No Chromium browser object appears in this header. The whole state machine
// is provable on a host without a browser, which is where its tests run.

namespace taffy {

class DeltaSubscription {
 public:
  // Delivered in order. The subscription owns the envelope until this returns.
  using DeliverCallback = base::RepeatingCallback<void(DeltaEnvelope)>;

  struct Counters {
    uint32_t delivered = 0;
    uint32_t discarded_duplicate = 0;
    uint32_t discarded_out_of_order = 0;
    uint32_t discarded_while_not_applying = 0;
    uint32_t rejected_epoch_mismatch = 0;
    uint32_t rejected_revision_mismatch = 0;
    uint32_t rejected_over_budget = 0;
    uint32_t gaps = 0;
    uint32_t shed_notices = 0;
  };

  DeltaSubscription(SubscriptionId subscription_id,
                    TabId tab_id,
                    FrameId frame_id,
                    PageEpoch page_epoch,
                    GraphRevision base_revision,
                    SubscriptionRequest granted,
                    DeliverCallback deliver);
  DeltaSubscription(const DeltaSubscription&) = delete;
  DeltaSubscription& operator=(const DeltaSubscription&) = delete;
  ~DeltaSubscription();

  const SubscriptionId& subscription_id() const { return subscription_id_; }
  const TabId& tab_id() const { return tab_id_; }
  const FrameId& frame_id() const { return frame_id_; }
  const PageEpoch& page_epoch() const { return page_epoch_; }
  SubscriptionState state() const { return state_; }
  const DeltaProjectionCursor& cursor() const { return cursor_; }
  const SubscriptionRequest& granted() const { return granted_; }
  const Counters& counters() const { return counters_; }
  const EventSequenceTracker::Counters& sequence_counters() const {
    return sequence_.counters();
  }
  bool carries_text_deltas() const { return granted_.include_text_deltas; }
  bool carries_layout_deltas() const { return granted_.include_layout_deltas; }
  ObservationScope scope() const { return granted_.scope; }
  bool scope_already_reduced() const { return scope_reduced_; }

  uint32_t queue_depth() const {
    return static_cast<uint32_t>(pending_.size() + unacknowledged_.size());
  }
  uint32_t queued_bytes() const { return pending_bytes_ + unacknowledged_bytes_; }

  // Offers one delta. Returns the reason it was not applied, or kNone.
  //
  // On kNone the delta is delivered immediately, or queued when the
  // subscription is paused. On any reason for which ProjectionSurvives() is
  // false the subscription moves to kAwaitingResnapshot and delivers nothing
  // further until Rebase() is called: a subscriber must never receive a delta
  // that follows one it did not get.
  DeltaRejectReason Offer(DeltaEnvelope delta);

  // The subscriber applied everything up to and including `event_sequence`.
  // Drains the queue, which is what relieves pressure.
  void Acknowledge(EventSequence event_sequence);

  // Applies a backpressure decision. Shedding narrows what the subscription
  // carries; pausing stops delivery; a resnapshot request kills the
  // projection. Returns the notice to send, or nullopt when the decision said
  // nothing needed saying.
  std::optional<BackpressureNotice> Apply(
      BackpressureAction action,
      const DeltaShedPlan& shed,
      const std::optional<ObservationScope>& reduced_scope,
      bool resnapshot_required,
      SubscriptionState next_state,
      MonotonicMillis now_ms);

  // Resumes delivery after a pause, flushing whatever was held in order.
  void Resume();

  // A fresh snapshot re-established the projection at `revision` and
  // `event_sequence`. The only way out of kAwaitingResnapshot.
  void Rebase(GraphRevision revision, EventSequence event_sequence);

  // Terminal. The identifier is never reused, and nothing is delivered after.
  void Stop();

  // True when the subscription is applying deltas right now.
  bool is_applying() const { return state_ == SubscriptionState::kActive; }

 private:
  void DeliverOrQueue(DeltaEnvelope delta);
  void DropProjection(DeltaRejectReason reason);
  static uint32_t EstimateBytes(const DeltaEnvelope& delta);

  const SubscriptionId subscription_id_;
  const TabId tab_id_;
  const FrameId frame_id_;
  const PageEpoch page_epoch_;

  SubscriptionRequest granted_;
  DeliverCallback deliver_;

  DeltaProjectionCursor cursor_;
  SubscriptionState state_ = SubscriptionState::kActive;
  bool scope_reduced_ = false;
  // Classes the broker has stopped carrying. Kept so a later notice can repeat
  // the full set rather than only the newest increment.
  std::vector<DeltaClass> shed_classes_;
  uint32_t dropped_delta_count_ = 0;
  // The subscriber has already been told its projection is dead. Every message
  // that arrives afterwards is refused, and telling it again once per refused
  // message would turn one honest notice into a storm.
  bool resnapshot_notice_sent_ = false;

  base::circular_deque<DeltaEnvelope> pending_;
  uint32_t pending_bytes_ = 0;
  // Sequence and size of each delivered-but-unacknowledged delta, oldest
  // first. The envelopes themselves are gone; only what the queue costs
  // remains, because holding a delivered payload would double its memory for
  // no benefit.
  base::circular_deque<std::pair<EventSequence, uint32_t>> unacknowledged_;
  uint32_t unacknowledged_bytes_ = 0;

  EventSequenceTracker sequence_;
  Counters counters_;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_SUBSCRIPTION_H_
