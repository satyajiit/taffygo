// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_DELTA_PUBLISHER_H_
#define TAFFY_RENDERER_DELTA_PUBLISHER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/delta_coalescer.h"
#include "taffy/renderer/semantic_graph_store.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace taffy {

// One delta subscription: the coalescer, the client remote, and the rules
// about when a batch leaves.
//
// Protocol section 10 in one sentence: "Delta streams are an optimization.
// Correctness can always fall back to a bounded fresh snapshot." Everything
// here follows from that. When this class cannot represent what happened it
// does not guess, thin out, or reorder - it invalidates the subscriber's
// projection and asks for a resnapshot, which is strictly stronger than any
// partial delta it could have sent.
//
// The publisher is separate from the endpoint because it owns a remote and a
// timer-shaped decision, and mixing that with the interface methods made both
// harder to follow. It is also the half that has to know the change classes
// of protocol section 5.3, and keeping that switch next to the drop order it
// feeds is worth a file.
class DeltaPublisher {
 public:
  // `tab_id` and `frame_id` are the browser-stated identities from the
  // Subscribe request, not the renderer's local names. The broker's identity
  // echo drops a delta whose frame id disagrees with the subscription, and
  // the renderer's own FrameId is a process-local `rfN` that never matches.
  DeltaPublisher(SemanticGraphStore* store,
                 std::string schema_version,
                 std::string tab_id,
                 std::string frame_id);
  DeltaPublisher(const DeltaPublisher&) = delete;
  DeltaPublisher& operator=(const DeltaPublisher&) = delete;
  ~DeltaPublisher();

  // Binds the browser's delta client and starts a subscription. Returns the
  // subscription identifier.
  std::string Start(mojo::PendingRemote<mojom::PageDeltaClient> client,
                    base::TimeDelta requested_coalescing_window,
                    const std::string& subscription_id);

  bool active() const { return coalescer_ && client_; }

  // Records one change. Maps the protocol section 5.3 change class onto the
  // section 10 drop class, queues it, and flushes when the batching window
  // has closed - or immediately when an action barrier is held, because a
  // batched-away state change is exactly the precondition a dispatcher is
  // about to re-check.
  //
  // Closing the window is a timer, not a later mutation. A stream that only
  // flushed when the next change arrived would sit silent after the last
  // mutation of a burst — which is every single-mutation page, including
  // `document.body.appendChild`.
  //
  // `event_sequence` is the endpoint's, passed in rather than kept here: a
  // gap in it is what tells the broker to invalidate, so exactly one counter
  // may exist per endpoint. The delayed flush reuses the pointer the most
  // recent OnChange supplied; the endpoint outlives this publisher.
  void OnChange(SemanticGraphStore::ChangeClass change,
                SemanticNodeId node_id,
                GraphRevision revision,
                uint64_t* event_sequence);

  // Sends whatever is queued. Called on lifecycle transitions as well as from
  // OnChange: a document that stops being active must not leave a subscriber
  // applying changes to a page nobody is looking at.
  void Flush(uint64_t* event_sequence);

  // Tears the subscription down and tells the subscriber why. `retires_epoch`
  // is true when the handles the subscriber holds are dead as well as its
  // projection.
  void Stop(mojom::InvalidationReason reason,
            bool retires_epoch,
            uint64_t* event_sequence);

 private:
  void SendInvalidation(mojom::InvalidationReason reason,
                        bool retires_epoch,
                        uint64_t* event_sequence);
  void OnCoalescingWindowClosed();

  const raw_ptr<SemanticGraphStore> store_;
  const std::string schema_version_;
  const std::string tab_id_;
  const std::string frame_id_;

  std::unique_ptr<DeltaCoalescer> coalescer_;
  mojo::Remote<mojom::PageDeltaClient> client_;
  std::optional<std::string> subscription_id_;
  GraphRevision last_sent_revision_{0};
  base::TimeDelta coalescing_window_;
  raw_ptr<uint64_t> event_sequence_ = nullptr;
  // Per-stream, not per-endpoint. The endpoint's counter is also bumped by
  // snapshots, so a subscription that reused it would emit 2 as its first
  // delta; the broker's tracker starts at 0 and treats 2 as a gap, which
  // kills the projection before the core service ever sees a message.
  uint64_t stream_event_sequence_ = 0;
  // Last so a pending tick is cancelled before the members it reads die.
  base::OneShotTimer flush_timer_;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_DELTA_PUBLISHER_H_
