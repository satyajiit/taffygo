// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/delta_subscription.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"

namespace taffy {

namespace {

// Charged against the subscription's byte budget. The encoded graph payload
// dominates; the removed-identifier list is added because it is the one part
// of a delta the broker keeps in full, and a page that retires thousands of
// nodes would otherwise look free.
constexpr uint32_t kPerRemovedIdOverheadBytes = 16;

}  // namespace

DeltaSubscription::DeltaSubscription(SubscriptionId subscription_id,
                                     TabId tab_id,
                                     FrameId frame_id,
                                     PageEpoch page_epoch,
                                     GraphRevision base_revision,
                                     SubscriptionRequest granted,
                                     DeliverCallback deliver)
    : subscription_id_(std::move(subscription_id)),
      tab_id_(std::move(tab_id)),
      frame_id_(std::move(frame_id)),
      page_epoch_(std::move(page_epoch)),
      granted_(std::move(granted)),
      deliver_(std::move(deliver)) {
  CHECK(subscription_id_.is_valid());
  CHECK(tab_id_.is_valid());
  CHECK(frame_id_.is_valid());
  CHECK(page_epoch_.is_valid());
  CHECK(deliver_);
  cursor_.page_epoch = page_epoch_;
  cursor_.revision = base_revision;
  cursor_.event_sequence = 0;
}

DeltaSubscription::~DeltaSubscription() = default;

// static
uint32_t DeltaSubscription::EstimateBytes(const DeltaEnvelope& delta) {
  const uint64_t bytes =
      static_cast<uint64_t>(delta.graph_payload.size()) +
      static_cast<uint64_t>(delta.removed_node_ids.size()) *
          kPerRemovedIdOverheadBytes;
  return bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytes);
}

DeltaRejectReason DeltaSubscription::Offer(DeltaEnvelope delta) {
  if (state_ == SubscriptionState::kStopped ||
      state_ == SubscriptionState::kAwaitingResnapshot) {
    // Nothing is queued while the projection is dead. Holding deltas that
    // could never be applied would spend the queue budget on work that is
    // going to be thrown away by the resnapshot that follows.
    ++counters_.discarded_while_not_applying;
    return DeltaRejectReason::kSubscriptionNotApplying;
  }

  // Ordering first, and through the same tracker the endpoint uses for every
  // other message, so a duplicate delta and a duplicate snapshot reply are
  // classified by one implementation rather than two.
  const EventSequenceTracker::Verdict verdict =
      sequence_.Classify(delta.event_sequence);
  switch (verdict) {
    case EventSequenceTracker::Verdict::kDuplicate:
      ++counters_.discarded_duplicate;
      return DeltaRejectReason::kDuplicate;
    case EventSequenceTracker::Verdict::kOutOfOrder:
      ++counters_.discarded_out_of_order;
      return DeltaRejectReason::kOutOfOrder;
    case EventSequenceTracker::Verdict::kGap:
      ++counters_.gaps;
      DropProjection(DeltaRejectReason::kSequenceGap);
      return DeltaRejectReason::kSequenceGap;
    case EventSequenceTracker::Verdict::kAccepted:
      break;
  }

  // Then the protocol section 10 matching rule itself, which is written once,
  // in public/bip_delta.h, and evaluated nowhere else.
  const DeltaRejectReason reason =
      DeltaApplicability(cursor_, delta.page_epoch, delta.from_revision,
                         delta.to_revision, delta.event_sequence);
  if (reason != DeltaRejectReason::kNone) {
    switch (reason) {
      case DeltaRejectReason::kEpochMismatch:
        ++counters_.rejected_epoch_mismatch;
        break;
      case DeltaRejectReason::kRevisionMismatch:
        ++counters_.rejected_revision_mismatch;
        break;
      default:
        break;
    }
    DropProjection(reason);
    return reason;
  }

  // The subscription's own byte budget. A delta larger than one message may
  // carry is not truncated to fit: truncating would hand the subscriber a
  // partial application, which is precisely the outcome the whole section
  // forbids.
  const uint32_t bytes = EstimateBytes(delta);
  if (granted_.budget.max_delta_bytes != 0 &&
      bytes > granted_.budget.max_delta_bytes) {
    ++counters_.rejected_over_budget;
    DropProjection(DeltaRejectReason::kOverBudget);
    return DeltaRejectReason::kOverBudget;
  }

  cursor_ = delta.NextCursor();
  DeliverOrQueue(std::move(delta));
  return DeltaRejectReason::kNone;
}

void DeltaSubscription::DeliverOrQueue(DeltaEnvelope delta) {
  const uint32_t bytes = EstimateBytes(delta);
  if (state_ == SubscriptionState::kPaused) {
    pending_bytes_ += bytes;
    pending_.push_back(std::move(delta));
    return;
  }
  const EventSequence sequence = delta.event_sequence;
  unacknowledged_.emplace_back(sequence, bytes);
  unacknowledged_bytes_ += bytes;
  ++counters_.delivered;
  deliver_.Run(std::move(delta));
}

void DeltaSubscription::Acknowledge(EventSequence event_sequence) {
  while (!unacknowledged_.empty() &&
         unacknowledged_.front().first <= event_sequence) {
    unacknowledged_bytes_ -= unacknowledged_.front().second;
    unacknowledged_.pop_front();
  }
}

void DeltaSubscription::DropProjection(DeltaRejectReason reason) {
  CHECK(!ProjectionSurvives(reason));
  state_ = SubscriptionState::kAwaitingResnapshot;
  // Everything held becomes unapplicable at the same moment. Releasing it here
  // rather than at the resnapshot means the memory is gone before the
  // subscriber has finished reacting.
  pending_.clear();
  pending_bytes_ = 0;
  unacknowledged_.clear();
  unacknowledged_bytes_ = 0;
}

std::optional<BackpressureNotice> DeltaSubscription::Apply(
    BackpressureAction action,
    const DeltaShedPlan& shed,
    const std::optional<ObservationScope>& reduced_scope,
    bool resnapshot_required,
    SubscriptionState next_state,
    MonotonicMillis now_ms) {
  if (state_ == SubscriptionState::kStopped) {
    return std::nullopt;
  }
  if (resnapshot_required && resnapshot_notice_sent_) {
    // Already said. Repeating it once per refused message would drown the
    // notice that mattered.
    return std::nullopt;
  }

  for (const SheddableDeltaClass& sheddable : shed.classes) {
    if (!std::ranges::contains(shed_classes_, sheddable.value())) {
      shed_classes_.push_back(sheddable.value());
    }
    // Shedding a class the subscription carries is what actually stops it
    // being produced; the grant is narrowed so that a later pressure sample
    // does not offer the same relief twice.
    if (sheddable.value() == DeltaClass::kText) {
      granted_.include_text_deltas = false;
    } else if (sheddable.value() == DeltaClass::kLayout) {
      granted_.include_layout_deltas = false;
    }
  }

  if (reduced_scope.has_value()) {
    granted_.scope = *reduced_scope;
    scope_reduced_ = true;
  }

  const uint32_t dropped_now =
      static_cast<uint32_t>(pending_.size() + unacknowledged_.size());

  if (resnapshot_required) {
    dropped_delta_count_ += dropped_now;
    resnapshot_notice_sent_ = true;
    // kOverBudget is the reason that reaches here: every backpressure path that
    // demands a resnapshot does so because more arrived than the subscription
    // was granted room for. The stream's own reasons — a gap, an epoch
    // mismatch — took DropProjection directly from Offer with their own.
    DropProjection(DeltaRejectReason::kOverBudget);
  } else {
    state_ = next_state;
    if (state_ == SubscriptionState::kActive) {
      Resume();
    }
  }

  BackpressureNotice notice;
  notice.schema_version = kBipSchemaVersion;
  notice.subscription_id = subscription_id_;
  notice.tab_id = tab_id_;
  notice.frame_id = frame_id_;
  notice.page_epoch = page_epoch_;
  notice.event_sequence = cursor_.event_sequence;
  notice.action = action;
  notice.dropped_delta_count = dropped_delta_count_;
  notice.dropped_classes = shed_classes_;
  notice.queue_depth_hint = queue_depth();
  notice.reduced_scope = reduced_scope;
  notice.resnapshot_required = resnapshot_required;
  notice.observed_at_monotonic_ms = now_ms;

  // The contract's conditional requirements, checked before the notice leaves
  // rather than trusted. A malformed notice would let a subscriber keep a dead
  // projection, which is the one outcome backpressure exists to prevent.
  CHECK(BackpressureNoticeIsWellFormed(notice));
  ++counters_.shed_notices;
  return notice;
}

void DeltaSubscription::Resume() {
  if (state_ != SubscriptionState::kActive) {
    return;
  }
  base::circular_deque<DeltaEnvelope> flushing;
  flushing.swap(pending_);
  pending_bytes_ = 0;
  while (!flushing.empty()) {
    DeltaEnvelope delta = std::move(flushing.front());
    flushing.pop_front();
    const uint32_t bytes = EstimateBytes(delta);
    unacknowledged_.emplace_back(delta.event_sequence, bytes);
    unacknowledged_bytes_ += bytes;
    ++counters_.delivered;
    deliver_.Run(std::move(delta));
  }
}

void DeltaSubscription::Rebase(GraphRevision revision,
                               EventSequence event_sequence) {
  if (state_ == SubscriptionState::kStopped) {
    return;
  }
  cursor_.page_epoch = page_epoch_;
  cursor_.revision = revision;
  cursor_.event_sequence = event_sequence;
  resnapshot_notice_sent_ = false;
  // The snapshot established where the stream actually is. Rebasing rather
  // than resetting means the next delta is judged against that point, and the
  // gap that forced the resnapshot stays counted.
  sequence_.RebaseTo(event_sequence);
  pending_.clear();
  pending_bytes_ = 0;
  unacknowledged_.clear();
  unacknowledged_bytes_ = 0;
  state_ = SubscriptionState::kActive;
}

void DeltaSubscription::Stop() {
  state_ = SubscriptionState::kStopped;
  pending_.clear();
  pending_bytes_ = 0;
  unacknowledged_.clear();
  unacknowledged_bytes_ = 0;
}

}  // namespace taffy
