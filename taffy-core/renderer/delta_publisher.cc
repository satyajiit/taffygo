// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/delta_publisher.h"

#include <utility>

#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/wire_conversions.h"

namespace taffy {

namespace {

// Protocol section 5.3 change classes onto the section 10 drop classes.
//
// The switch has no default case on purpose: a change class added to the
// store forces a decision here about how droppable it is, and "droppable"
// versus "never dropped" is the difference between a subscriber that missed a
// cosmetic update and one holding a handle to something that no longer
// exists.
DeltaCoalescer::SignalClass SignalClassFor(
    SemanticGraphStore::ChangeClass change) {
  switch (change) {
    case SemanticGraphStore::ChangeClass::kNodeRemoved:
      return DeltaCoalescer::SignalClass::kNodeRemoved;

    case SemanticGraphStore::ChangeClass::kRouteTransition:
    case SemanticGraphStore::ChangeClass::kAdapterInvalidated:
    case SemanticGraphStore::ChangeClass::kChildFrameChanged:
      return DeltaCoalescer::SignalClass::kLifecycle;

    case SemanticGraphStore::ChangeClass::kNodeAdded:
    case SemanticGraphStore::ChangeClass::kNodeReparented:
    case SemanticGraphStore::ChangeClass::kNodeReplaced:
    case SemanticGraphStore::ChangeClass::kVirtualizedRecycled:
    case SemanticGraphStore::ChangeClass::kShadowTreeChanged:
      return DeltaCoalescer::SignalClass::kNodeAdded;

    case SemanticGraphStore::ChangeClass::kRoleChanged:
    case SemanticGraphStore::ChangeClass::kAccessibleStateChanged:
    case SemanticGraphStore::ChangeClass::kFormStateChanged:
    case SemanticGraphStore::ChangeClass::kVisibilityChanged:
    case SemanticGraphStore::ChangeClass::kEnabledStateChanged:
    case SemanticGraphStore::ChangeClass::kDestinationChanged:
      return DeltaCoalescer::SignalClass::kStateChanged;

    case SemanticGraphStore::ChangeClass::kBoundsChanged:
      return DeltaCoalescer::SignalClass::kLayoutChanged;
  }
}

uint64_t NowMs() {
  return static_cast<uint64_t>(
      (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds());
}

}  // namespace

DeltaPublisher::DeltaPublisher(SemanticGraphStore* store,
                               std::string schema_version,
                               std::string tab_id,
                               std::string frame_id)
    : store_(store),
      schema_version_(std::move(schema_version)),
      tab_id_(std::move(tab_id)),
      frame_id_(std::move(frame_id)) {}

DeltaPublisher::~DeltaPublisher() = default;

std::string DeltaPublisher::Start(
    mojo::PendingRemote<mojom::PageDeltaClient> client,
    base::TimeDelta requested_coalescing_window,
    const std::string& subscription_id) {
  const ObservationLimits& limits = ObservationLimits::ProcessSafeCeiling();
  coalescing_window_ =
      limits.delta().ClampCoalescingWindow(requested_coalescing_window);
  coalescer_ = std::make_unique<DeltaCoalescer>(
      DeltaCoalescer::Limits(limits.delta(), requested_coalescing_window));
  flush_timer_.Stop();
  event_sequence_ = nullptr;
  client_.reset();
  client_.Bind(std::move(client));
  subscription_id_ = subscription_id;
  last_sent_revision_ = store_->current_revision();
  stream_event_sequence_ = 0;
  return subscription_id;
}

void DeltaPublisher::OnChange(SemanticGraphStore::ChangeClass change,
                              SemanticNodeId node_id,
                              GraphRevision revision,
                              uint64_t* event_sequence) {
  if (!active()) {
    return;
  }
  const ObservationLimits& limits = ObservationLimits::ProcessSafeCeiling();

  DeltaCoalescer::Signal signal;
  signal.signal_class = SignalClassFor(change);
  signal.node_id = std::move(node_id);
  signal.revision = revision;
  signal.estimated_bytes = limits.delta().estimated_signal_bytes();

  event_sequence_ = event_sequence;
  coalescer_->Queue(signal);
  if (!coalescer_->ShouldKeepBatching(base::TimeTicks::Now(),
                                      store_->action_barrier_active())) {
    flush_timer_.Stop();
    Flush(event_sequence);
    return;
  }
  // Tumbling window: the first queued signal opens it, and later signals in
  // the same window ride along. Restarting on every mutation would postpone
  // a busy page forever.
  if (!flush_timer_.IsRunning() && coalescing_window_.is_positive()) {
    flush_timer_.Start(FROM_HERE, coalescing_window_, this,
                       &DeltaPublisher::OnCoalescingWindowClosed);
  }
}

void DeltaPublisher::OnCoalescingWindowClosed() {
  if (event_sequence_) {
    Flush(event_sequence_);
  }
}

void DeltaPublisher::Flush(uint64_t* event_sequence) {
  flush_timer_.Stop();
  if (!active()) {
    return;
  }

  DeltaCoalescer::Batch batch =
      coalescer_->Flush(last_sent_revision_, store_->current_revision());
  last_sent_revision_ = store_->current_revision();

  if (batch.requires_resnapshot) {
    // The stream could not represent what happened. An invalidation is
    // strictly stronger than any partial delta: it tells the subscriber that
    // everything it holds is stale, and correctness always has the
    // bounded-fresh-snapshot fallback.
    SendInvalidation(mojom::InvalidationReason::kDeltaOverflow,
                     /*retires_epoch=*/false, event_sequence);

    auto notice = mojom::BackpressureNotice::New();
    notice->schema_version = schema_version_;
    notice->subscription_id = subscription_id_.value();
    notice->tab_id = tab_id_;
    notice->frame_id = frame_id_;
    notice->page_epoch = store_->page_epoch().value();
    notice->event_sequence = ++stream_event_sequence_;
    notice->action = mojom::BackpressureAction::kResnapshotRequested;
    notice->dropped_delta_count = batch.dropped_optional_signals +
                                  batch.dropped_state_signals +
                                  batch.dropped_addition_signals;
    // Named so the browser can see WHAT was shed, not only how much. Optional
    // categories only: a removal or lifecycle signal is never in this list,
    // because neither is ever dropped (protocol section 10).
    notice->dropped_categories = {mojom::DeltaCategory::kText,
                                  mojom::DeltaCategory::kLayout};
    notice->resnapshot_required = true;
    notice->observed_at_monotonic_ms = NowMs();
    client_->OnBackpressure(std::move(notice));
    return;
  }

  if (batch.signals.empty()) {
    return;
  }

  auto delta = mojom::PageDelta::New();
  delta->schema_version = schema_version_;
  delta->subscription_id = subscription_id_.value();
  delta->tab_id = tab_id_;
  delta->frame_id = frame_id_;
  delta->page_epoch = store_->page_epoch().value();
  delta->from_revision = batch.from_revision.value();
  delta->to_revision = batch.to_revision.value();
  delta->event_sequence = ++stream_event_sequence_;
  delta->coalesced_mutation_count =
      static_cast<uint32_t>(batch.signals.size());
  delta->truncation = mojom::Truncation::New();
  delta->observed_at_monotonic_ms = NowMs();

  for (const DeltaCoalescer::Signal& signal : batch.signals) {
    if (signal.signal_class == DeltaCoalescer::SignalClass::kNodeRemoved) {
      delta->removed_node_ids.push_back(signal.node_id.value());
      continue;
    }
    const SemanticGraphStore::ResolveResult resolved =
        store_->Resolve(signal.node_id, store_->page_epoch(), std::nullopt);
    if (resolved.status != SemanticGraphStore::ResolveStatus::kOk) {
      continue;
    }

    // The identity and the precondition-relevant facts, and nothing else. A
    // subscriber that needs the body re-reads it, which keeps one redaction
    // gate on the snapshot path rather than two - and one gate is easier to
    // prove correct than two that have to agree.
    auto changed = mojom::ChangedNode::New();
    changed->node = mojom::SemanticNode::New();
    changed->node->node_id = signal.node_id.value();
    changed->node->frame_id = store_->frame_id().value();
    changed->node->role = wire::ToMojom(resolved.node->role);
    changed->node->sensitivity = wire::ToMojom(resolved.node->sensitivity);
    for (NodeState state : resolved.node->states) {
      changed->node->states.push_back(wire::ToMojom(state));
    }
    for (ActionKind action : resolved.node->actions) {
      changed->node->actions.push_back(wire::ToMojom(action));
    }
    changed->changed_fields = {mojom::SemanticField::kStates,
                               mojom::SemanticField::kActions};
    if (signal.signal_class == DeltaCoalescer::SignalClass::kLayoutChanged &&
        resolved.node->bounds.has_value()) {
      auto bounds = mojom::Bounds::New();
      bounds->x = resolved.node->bounds->x;
      bounds->y = resolved.node->bounds->y;
      bounds->width = resolved.node->bounds->width;
      bounds->height = resolved.node->bounds->height;
      changed->node->bounds = std::move(bounds);
      changed->changed_fields.push_back(mojom::SemanticField::kBounds);
    }
    delta->changed_nodes.push_back(std::move(changed));
  }

  client_->OnDelta(std::move(delta));
}

void DeltaPublisher::Stop(mojom::InvalidationReason reason,
                          bool retires_epoch,
                          uint64_t* event_sequence) {
  flush_timer_.Stop();
  if (active()) {
    SendInvalidation(reason, retires_epoch, event_sequence);
  }
  coalescer_.reset();
  client_.reset();
  subscription_id_.reset();
  event_sequence_ = nullptr;
}

void DeltaPublisher::SendInvalidation(mojom::InvalidationReason reason,
                                      bool retires_epoch,
                                      uint64_t* event_sequence) {
  auto invalidation = mojom::PageInvalidation::New();
  invalidation->schema_version = schema_version_;
  invalidation->subscription_id = subscription_id_.value();
  invalidation->tab_id = tab_id_;
  invalidation->frame_id = frame_id_;
  invalidation->page_epoch = store_->page_epoch().value();
  invalidation->event_sequence = ++stream_event_sequence_;
  invalidation->reason = reason;
  invalidation->retires_page_epoch = retires_epoch;
  invalidation->invalidates_child_frames_only = false;
  invalidation->resnapshot_required = true;
  invalidation->observed_at_monotonic_ms = NowMs();
  client_->OnInvalidated(std::move(invalidation));
}

}  // namespace taffy
