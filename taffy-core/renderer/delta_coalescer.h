// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_DELTA_COALESCER_H_
#define TAFFY_RENDERER_DELTA_COALESCER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/types/pass_key.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/semantic_graph_store.h"

namespace taffy {

// Mutation batching and backpressure for a delta subscription, per spec
// section 10.
//
// The whole class rests on one sentence from the spec: "Delta streams are an
// optimization. Correctness can always fall back to a bounded fresh
// snapshot." That is what makes the drop policy simple. When this class
// cannot keep up it does not guess, thin out, or reorder: it says the
// projection is invalid and the subscriber resnapshots. A dropped optional
// text change costs a subscriber nothing; a dropped node removal would leave
// a subscriber holding a handle to something that no longer exists, and would
// turn a correctness property into a race.
//
// Hence the ordering, which is the one thing in here that is not negotiable:
//
//     text/layout  ->  state changes  ->  additions  ->  |  removals
//                                                        |  lifecycle
//                        dropped first ------------->    |  never dropped
//
// Removals and lifecycle signals are never dropped. When they cannot fit, the
// coalescer emits an invalidation instead, which is strictly stronger: it
// tells the subscriber that everything it holds is stale.
class DeltaCoalescer {
 public:
  // Signal classes in drop order. The enum's order is the drop order and the
  // static_assert in the .cc pins that to the policy above so a reordering
  // cannot happen by accident.
  enum class SignalClass {
    kTextChanged,
    kLayoutChanged,
    kStateChanged,
    kNodeAdded,
    kNodeRemoved,
    kLifecycle,
  };

  struct Signal {
    SignalClass signal_class = SignalClass::kTextChanged;
    SemanticNodeId node_id;
    GraphRevision revision{0};
    // Approximate serialized cost, charged against the queue byte budget.
    size_t estimated_bytes = 0;

    // Renderer-local arrival order. Queue() overwrites this on ingress; it is
    // not serialized. Keeping it on the queued value lets a repeated signal
    // be replaced in place while Flush() and overflow shedding retain the
    // same newest-occurrence ordering as erase-and-append did.
    uint64_t coalescing_order = 0;
  };

  // Every bound this class enforces comes from the one limits policy. There
  // is no default here to fall back to, and no constructor that takes a
  // number: a queue depth that could be written at a call site is a queue
  // depth that will be written at two.
  struct Limits {
    // The only production constructor. It takes the limits policy object,
    // which nothing in this component can build a value for, so there is no
    // way to write a queue depth at a call site - and a bound that can be
    // written at one call site will be written at two.
    Limits(const DeltaLimits& delta, base::TimeDelta window);

    // Tests need a queue small enough to overflow in four statements. The
    // name is the guard: production code that reached for this would be
    // choosing its own bound, which is exactly what review is looking for.
    static Limits ForTesting(size_t max_queued_signals,
                             size_t max_queued_bytes,
                             base::TimeDelta window);

    size_t max_queued_signals;
    size_t max_queued_bytes;
    base::TimeDelta coalescing_window;

   private:
    Limits(size_t max_queued_signals,
           size_t max_queued_bytes,
           base::TimeDelta window,
           base::PassKey<Limits> pass_key);
  };

  struct Batch {
    Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    Batch(Batch&&);
    Batch& operator=(Batch&&);
    ~Batch();

    GraphRevision from_revision{0};
    GraphRevision to_revision{0};
    uint64_t event_sequence = 0;

    std::vector<Signal> signals;

    // Set when the subscriber must discard its projection and resnapshot:
    // the queue overflowed, a required signal could not be represented, or
    // an adapter restarted. Spec section 10 makes this the only correct
    // response to a gap.
    bool requires_resnapshot = false;

    // Counts for the content-free local diagnostics of protocol section 16.
    uint32_t dropped_optional_signals = 0;
    uint32_t dropped_state_signals = 0;
    uint32_t dropped_addition_signals = 0;
  };

  explicit DeltaCoalescer(const Limits& limits);
  DeltaCoalescer(const DeltaCoalescer&) = delete;
  DeltaCoalescer& operator=(const DeltaCoalescer&) = delete;
  ~DeltaCoalescer();

  // Queues a signal. Returns false when the signal forced the projection to
  // be invalidated, which the caller reports rather than retries.
  bool Queue(const Signal& signal);

  // True when the batching window should stay open. Always false while an
  // action barrier is held: protocol section 5.3 forbids coalescing across an
  // action preflight/dispatch boundary, because a batched-away state change
  // is exactly the precondition the dispatcher is about to re-check.
  bool ShouldKeepBatching(base::TimeTicks now,
                          bool action_barrier_active) const;

  // Produces the batch and resets. `to_revision` is the store's revision at
  // the moment of flushing.
  Batch Flush(GraphRevision from_revision, GraphRevision to_revision);

  // The browser broker asking the renderer to back off (protocol section 10:
  // "the broker may reduce observation scope, pause a subscription, or
  // request a resnapshot before queue growth affects renderer or UI health").
  // Paused means optional signals are dropped on arrival; required ones are
  // still accounted so the resnapshot demand survives.
  void SetPaused(bool paused);
  bool paused() const { return paused_; }

  size_t queued_signal_count() const { return queue_.size(); }
  size_t queued_bytes() const { return queued_bytes_; }
  bool projection_invalid() const { return projection_invalid_; }

 private:
  // Drops the lowest class present until the queue is inside its budget.
  // Returns false when nothing further may be dropped, which means the only
  // remaining signals are removals and lifecycle - and then the projection is
  // invalidated instead.
  bool EnforceLimits();
  void RecordDropped(SignalClass signal_class);
  void RebuildCoalescingIndex();

  using CoalescingKey = std::pair<SemanticNodeId, SignalClass>;

  const Limits limits_;
  std::vector<Signal> queue_;
  // One lookup per queued mutation replaces a linear scan and vector erase.
  // The queue is bounded, but a busy page can replace entries many thousands
  // of times inside one window; without this index that work is quadratic in
  // the queue depth and shifts owning node-id strings on every hit.
  std::map<CoalescingKey, size_t> coalescing_index_;
  uint64_t next_coalescing_order_ = 1;
  size_t queued_bytes_ = 0;
  bool paused_ = false;
  bool projection_invalid_ = false;
  uint64_t event_sequence_ = 0;
  std::optional<base::TimeTicks> window_opened_at_;

  uint32_t dropped_optional_ = 0;
  uint32_t dropped_state_ = 0;
  uint32_t dropped_additions_ = 0;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_DELTA_COALESCER_H_
