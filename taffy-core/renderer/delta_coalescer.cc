// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/delta_coalescer.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/check_op.h"
#include "base/notreached.h"

namespace taffy {

namespace {

// The drop order is the enum order. If someone reorders the enum, this stops
// compiling rather than quietly making node removals droppable.
static_assert(
    static_cast<int>(DeltaCoalescer::SignalClass::kTextChanged) <
            static_cast<int>(DeltaCoalescer::SignalClass::kStateChanged) &&
        static_cast<int>(DeltaCoalescer::SignalClass::kStateChanged) <
            static_cast<int>(DeltaCoalescer::SignalClass::kNodeAdded) &&
        static_cast<int>(DeltaCoalescer::SignalClass::kNodeAdded) <
            static_cast<int>(DeltaCoalescer::SignalClass::kNodeRemoved) &&
        static_cast<int>(DeltaCoalescer::SignalClass::kNodeRemoved) <
            static_cast<int>(DeltaCoalescer::SignalClass::kLifecycle),
    "Signal drop order is the enum order: optional signals first, node "
    "removals and lifecycle last. See protocol section 10.");

bool IsOptional(DeltaCoalescer::SignalClass signal_class) {
  return signal_class == DeltaCoalescer::SignalClass::kTextChanged ||
         signal_class == DeltaCoalescer::SignalClass::kLayoutChanged;
}

}  // namespace

DeltaCoalescer::Batch::Batch() = default;
DeltaCoalescer::Batch::Batch(Batch&&) = default;
DeltaCoalescer::Batch& DeltaCoalescer::Batch::operator=(Batch&&) = default;
DeltaCoalescer::Batch::~Batch() = default;

DeltaCoalescer::Limits::Limits(const DeltaLimits& delta, base::TimeDelta window)
    : max_queued_signals(delta.max_queued_signals()),
      max_queued_bytes(delta.max_queued_bytes()),
      coalescing_window(delta.ClampCoalescingWindow(window)) {}

DeltaCoalescer::Limits::Limits(size_t max_queued_signals,
                               size_t max_queued_bytes,
                               base::TimeDelta window,
                               base::PassKey<Limits>)
    : max_queued_signals(max_queued_signals),
      max_queued_bytes(max_queued_bytes),
      coalescing_window(window) {}

// static
DeltaCoalescer::Limits DeltaCoalescer::Limits::ForTesting(
    size_t max_queued_signals,
    size_t max_queued_bytes,
    base::TimeDelta window) {
  return Limits(max_queued_signals, max_queued_bytes, window,
                base::PassKey<Limits>());
}

DeltaCoalescer::DeltaCoalescer(const Limits& limits) : limits_(limits) {}
DeltaCoalescer::~DeltaCoalescer() = default;

bool DeltaCoalescer::Queue(const Signal& signal) {
  if (projection_invalid_) {
    // Already invalid: nothing queued after this point can matter, because
    // the subscriber has to resnapshot anyway. Keep counting nothing, keep
    // allocating nothing.
    return false;
  }

  if (paused_ && IsOptional(signal.signal_class)) {
    ++dropped_optional_;
    return true;
  }

  // Coalescing: a later signal about the same node in the same class replaces
  // the earlier one rather than adding to it. This is the "coalesce rapid
  // mutations" of protocol section 5.3, and it is safe precisely because the
  // signal carries no payload - only which node and which revision.
  //
  // The index is the performance boundary here. A linear find followed by a
  // vector erase made a burst of repeated mutations quadratic in the bounded
  // queue depth, and every hit shifted owning node-id strings. Replace the
  // value in place; coalescing_order preserves the previous erase-and-append
  // ordering at overflow and flush.
  Signal queued = signal;
  queued.coalescing_order = next_coalescing_order_++;
  const CoalescingKey key(queued.node_id, queued.signal_class);
  const auto existing = coalescing_index_.find(key);
  if (existing == coalescing_index_.end()) {
    coalescing_index_.emplace(key, queue_.size());
    queue_.push_back(std::move(queued));
  } else {
    queued_bytes_ -= queue_[existing->second].estimated_bytes;
    queue_[existing->second] = std::move(queued);
  }
  const size_t ceiling = std::numeric_limits<size_t>::max();
  queued_bytes_ = signal.estimated_bytes > ceiling - queued_bytes_
                      ? ceiling
                      : queued_bytes_ + signal.estimated_bytes;
  if (!EnforceLimits()) {
    projection_invalid_ = true;
    return false;
  }

  if (!queue_.empty() && !window_opened_at_.has_value()) {
    window_opened_at_ = base::TimeTicks::Now();
  }
  return true;
}

void DeltaCoalescer::RecordDropped(SignalClass signal_class) {
  switch (signal_class) {
    case SignalClass::kTextChanged:
    case SignalClass::kLayoutChanged:
      ++dropped_optional_;
      break;
    case SignalClass::kStateChanged:
      ++dropped_state_;
      break;
    case SignalClass::kNodeAdded:
      ++dropped_additions_;
      break;
    case SignalClass::kNodeRemoved:
    case SignalClass::kLifecycle:
      NOTREACHED();
  }
}

bool DeltaCoalescer::EnforceLimits() {
  size_t remaining_count = queue_.size();
  const auto over_limit = [this, &remaining_count]() {
    return remaining_count > limits_.max_queued_signals ||
           queued_bytes_ > limits_.max_queued_bytes;
  };
  if (!over_limit()) {
    return true;
  }

  // In-place replacement leaves entries in their first-seen vector slots.
  // Restore latest-arrival order only on the exceptional overflow path before
  // applying the established "oldest low-priority signal first" policy.
  std::ranges::sort(queue_, [](const Signal& a, const Signal& b) {
    return a.coalescing_order < b.coalescing_order;
  });

  std::vector<bool> dropped(queue_.size(), false);
  for (SignalClass candidate :
       {SignalClass::kTextChanged, SignalClass::kLayoutChanged,
        SignalClass::kStateChanged, SignalClass::kNodeAdded}) {
    for (size_t i = 0; i < queue_.size() && over_limit(); ++i) {
      if (queue_[i].signal_class != candidate) {
        continue;
      }
      dropped[i] = true;
      queued_bytes_ -= queue_[i].estimated_bytes;
      --remaining_count;
      RecordDropped(candidate);
    }
    if (!over_limit()) {
      break;
    }
  }

  size_t write = 0;
  for (size_t read = 0; read < queue_.size(); ++read) {
    if (dropped[read]) {
      continue;
    }
    if (write != read) {
      queue_[write] = std::move(queue_[read]);
    }
    ++write;
  }
  queue_.resize(write);
  RebuildCoalescingIndex();
  return !over_limit();
}

void DeltaCoalescer::RebuildCoalescingIndex() {
  coalescing_index_.clear();
  for (size_t index = 0; index < queue_.size(); ++index) {
    coalescing_index_.emplace(
        CoalescingKey(queue_[index].node_id, queue_[index].signal_class),
        index);
  }
}

bool DeltaCoalescer::ShouldKeepBatching(base::TimeTicks now,
                                        bool action_barrier_active) const {
  if (action_barrier_active) {
    // Spec section 5.3. While an action is being checked and dispatched,
    // every change goes out immediately: batching here would hide the exact
    // precondition change the dispatcher needs to see.
    return false;
  }
  if (!window_opened_at_.has_value()) {
    return false;
  }
  return now - window_opened_at_.value() < limits_.coalescing_window;
}

DeltaCoalescer::Batch DeltaCoalescer::Flush(GraphRevision from_revision,
                                            GraphRevision to_revision) {
  Batch batch;
  batch.from_revision = from_revision;
  batch.to_revision = to_revision;
  batch.event_sequence = ++event_sequence_;
  batch.requires_resnapshot = projection_invalid_;
  batch.dropped_optional_signals = dropped_optional_;
  batch.dropped_state_signals = dropped_state_;
  batch.dropped_addition_signals = dropped_additions_;

  if (!projection_invalid_) {
    // Emitted in drop order, which is also a sensible apply order: removals
    // and lifecycle last means a subscriber that stops applying part-way
    // through has still applied the cheap changes and not a partial removal.
    std::ranges::sort(queue_, [](const Signal& a, const Signal& b) {
      if (a.signal_class != b.signal_class) {
        return a.signal_class < b.signal_class;
      }
      return a.coalescing_order < b.coalescing_order;
    });
    batch.signals = std::move(queue_);
  }

  queue_.clear();
  coalescing_index_.clear();
  next_coalescing_order_ = 1;
  queued_bytes_ = 0;
  window_opened_at_.reset();
  dropped_optional_ = 0;
  dropped_state_ = 0;
  dropped_additions_ = 0;
  projection_invalid_ = false;
  return batch;
}

void DeltaCoalescer::SetPaused(bool paused) {
  paused_ = paused;
}

}  // namespace taffy
