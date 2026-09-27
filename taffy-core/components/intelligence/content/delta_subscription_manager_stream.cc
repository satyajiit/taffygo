// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What arrives on an established subscription: deltas, invalidations, and the
// endpoint's own backpressure notices — and the pressure decision each of them
// feeds.
//
// Messages and pressure are one file because they are one loop: a delta is
// what raises pressure, and the pressure decision is what the next delta is
// judged against. Splitting them would leave two files that always change
// together.

#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/delta_backpressure_policy.h"
#include "taffy/components/intelligence/content/delta_subscription_manager.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"

namespace taffy {

// --- incoming stream messages -----------------------------------------------

void DeltaSubscriptionManager::OnDelta(mojom::PageDeltaPtr delta) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Entry* entry = CurrentSender();
  if (!entry || !entry->subscription || !delta) {
    return;
  }
  DeltaSubscription& subscription = *entry->subscription;
  const SubscriptionId subscription_id = subscription.subscription_id();
  if (!encoder_) {
    // No encoder means no way to carry the changed nodes onward. Delivering a
    // delta whose payload is empty would look like a page that changed nothing.
    EvaluatePressure(subscription_id, /*projection_already_dead=*/true,
                     /*renderer_reported_loss=*/false);
    return;
  }

  // The identity echo. The renderer was told which frame and epoch this stream
  // is bound to; a reply naming anything else belongs to a document the broker
  // did not subscribe to. The renderer's own subscription identifier is
  // compared here and used nowhere: which pipe the message arrived on is the
  // provenance this class trusts.
  if (delta->subscription_id != entry->renderer_subscription_id ||
      delta->tab_id != subscription.tab_id().value ||
      delta->frame_id != subscription.frame_id().value) {
    EvaluatePressure(subscription_id, /*projection_already_dead=*/true,
                     /*renderer_reported_loss=*/false);
    return;
  }

  DeltaEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.subscription_id = subscription_id;
  envelope.tab_id = subscription.tab_id();
  envelope.frame_id = subscription.frame_id();
  envelope.page_epoch = PageEpoch{delta->page_epoch};
  envelope.from_revision = delta->from_revision;
  envelope.to_revision = delta->to_revision;
  envelope.event_sequence = delta->event_sequence;
  envelope.coalesced_mutation_count = delta->coalesced_mutation_count;
  envelope.added_node_count = static_cast<uint32_t>(delta->added_nodes.size());
  envelope.changed_node_count =
      static_cast<uint32_t>(delta->changed_nodes.size());
  envelope.removed_node_count =
      static_cast<uint32_t>(delta->removed_node_ids.size());
  envelope.changed_edge_count =
      static_cast<uint32_t>(delta->changed_edges.size());
  for (const std::string& node_id : delta->removed_node_ids) {
    envelope.removed_node_ids.push_back(SemanticNodeId{node_id});
  }
  if (delta->truncation) {
    envelope.truncation = FromMojom(*delta->truncation);
  }
  envelope.observed_at_monotonic_ms = NowMonotonicMs();

  GraphPayloadEncoder::Encoded encoded =
      encoder_->EncodeDelta(*delta, subscription.frame_id(),
                            subscription.granted().budget.max_delta_bytes);
  if (encoded.rejected) {
    // No partial delta may advance either the broker's revision record or the
    // subscriber's cursor. Abandon the projection and ask for a snapshot: an
    // empty payload with non-empty change counts would otherwise look like a
    // successfully applied change carrying no body.
    EvaluatePressure(subscription_id, /*projection_already_dead=*/true,
                     /*renderer_reported_loss=*/false);
    return;
  }

  // The changed nodes cross as opaque bytes, exactly as a snapshot's do.
  // Browser C++ has now read the scalars above and nothing else. Only after a
  // complete encoding does the renderer's revision become one this browser
  // has actually observed. A delta does not clear a standing resnapshot
  // requirement — it describes changes to a projection, and that requirement
  // says the projection itself needs rebuilding.
  if (broker_) {
    broker_->NoteReportedRevision(subscription.frame_id(), delta->to_revision);
  }
  envelope.encoding = encoded.encoding;
  envelope.graph_payload = std::move(encoded.bytes);

  const uint32_t added = envelope.added_node_count;
  const uint32_t changed = envelope.changed_node_count;
  const uint32_t removed = envelope.removed_node_count;
  const uint32_t payload_bytes =
      static_cast<uint32_t>(envelope.graph_payload.size());
  const GraphRevision from_revision = envelope.from_revision;
  const GraphRevision to_revision = envelope.to_revision;
  const uint32_t coalesced = envelope.coalesced_mutation_count;
  const EventSequence event_sequence = envelope.event_sequence;

  // A delta may carry invalidations of its own. They are applied before the
  // delta is offered, because an invalidation that arrived with the batch
  // describes state the batch is already too late for (protocol section 6.3).
  bool carried_invalidation = false;
  for (const mojom::PageInvalidationPtr& carried : delta->invalidations) {
    if (!carried) {
      continue;
    }
    carried_invalidation = true;
    InvalidationNotice notice;
    notice.schema_version = kBipSchemaVersion;
    notice.subscription_id = subscription_id;
    notice.tab_id = subscription.tab_id();
    notice.frame_id = subscription.frame_id();
    notice.page_epoch = subscription.page_epoch();
    notice.event_sequence = carried->event_sequence;
    // An unmapped reason is a newer endpoint than this build knows. It is
    // reported as a broker invalidation rather than coerced to a neighbour,
    // and the projection dies either way.
    notice.reason = FromMojom(carried->reason)
                        .value_or(InvalidationCode::kBrokerInvalidation);
    notice.retires_page_epoch = carried->retires_page_epoch;
    notice.invalidates_child_frames_only =
        carried->invalidates_child_frames_only;
    notice.resnapshot_required = true;
    notice.observed_at_monotonic_ms = NowMonotonicMs();
    delegate_->OnStreamInvalidated(std::move(notice));
  }

  const DeltaRejectReason reason = subscription.Offer(std::move(envelope));

  SubscriptionRecord record;
  record.subscription_id = ToRecordIdentifier(subscription_id.value);
  record.tab_id = ToRecordIdentifier(subscription.tab_id().value);
  record.frame_id = ToRecordIdentifier(subscription.frame_id().value);
  record.page_epoch = ToRecordIdentifier(subscription.page_epoch().value);
  record.from_revision = from_revision;
  record.to_revision = to_revision;
  record.event_sequence = subscription.cursor().event_sequence;
  record.event = reason == DeltaRejectReason::kNone
                     ? SubscriptionEventKind::kDeltaDelivered
                 : ProjectionSurvives(reason)
                     ? SubscriptionEventKind::kDeltaDiscarded
                     : SubscriptionEventKind::kProjectionInvalidated;
  record.state = subscription.state();
  record.reject_reason = reason;
  record.queue_depth = subscription.queue_depth();
  record.queued_bytes = subscription.queued_bytes();
  record.byte_count = payload_bytes;
  record.added_node_count = added;
  record.changed_node_count = changed;
  record.removed_node_count = removed;
  record.coalesced_mutation_count = coalesced;
  record.delivered_count = subscription.counters().delivered;
  record.gap_count = subscription.counters().gaps;
  record.duplicate_count = subscription.counters().discarded_duplicate;
  record.out_of_order_count = subscription.counters().discarded_out_of_order;
  record.late_after_terminal_count =
      subscription.sequence_counters().late_after_terminal;
  observability_->Record(record);

  // A projection the browser just abandoned is named, not merely mourned. The
  // pressure decision below asks for a resnapshot, which says that something
  // has to be re-read but never says what went wrong; a subscriber that cannot
  // tell a lost change from a retired document cannot report the difference
  // either (protocol section 6.3). The reason travels as an invalidation,
  // ahead of the pressure notice, because an invalidation takes priority over
  // everything the subscriber still holds.
  if (const std::optional<InvalidationCode> invalidated =
          InvalidationCodeForDeltaReject(reason)) {
    InvalidationNotice notice;
    notice.schema_version = kBipSchemaVersion;
    notice.subscription_id = subscription_id;
    notice.tab_id = subscription.tab_id();
    notice.frame_id = subscription.frame_id();
    // Browser-owned identity, exactly as OnInvalidated below. The renderer's
    // echo was checked at the top of this function and is never adopted.
    notice.page_epoch = subscription.page_epoch();
    // The message the loss was detected on, which is the one a subscriber
    // needs to place the gap. The cursor still holds the last delta that
    // applied, so it would name the wrong one.
    notice.event_sequence = event_sequence;
    notice.reason = *invalidated;
    notice.retires_page_epoch = InvalidationRetiresDocument(*invalidated);
    notice.invalidates_child_frames_only = false;
    notice.resnapshot_required = true;
    notice.observed_at_monotonic_ms = NowMonotonicMs();
    delegate_->OnStreamInvalidated(std::move(notice));
  }

  EvaluatePressure(subscription_id,
                   /*projection_already_dead=*/!ProjectionSurvives(reason) ||
                       carried_invalidation,
                   /*renderer_reported_loss=*/false);
}

void DeltaSubscriptionManager::OnInvalidated(
    mojom::PageInvalidationPtr invalidation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Entry* entry = CurrentSender();
  if (!entry || !entry->subscription || !invalidation) {
    return;
  }
  DeltaSubscription& subscription = *entry->subscription;

  InvalidationNotice notice;
  notice.schema_version = kBipSchemaVersion;
  notice.subscription_id = subscription.subscription_id();
  notice.tab_id = subscription.tab_id();
  notice.frame_id = subscription.frame_id();
  // Browser-owned identity, not the renderer's echo. A renderer that named a
  // different epoch would otherwise be telling the isolated core which handles
  // to throw away.
  notice.page_epoch = subscription.page_epoch();
  notice.event_sequence = invalidation->event_sequence;
  notice.reason = FromMojom(invalidation->reason)
                      .value_or(InvalidationCode::kBrokerInvalidation);
  notice.retires_page_epoch = invalidation->retires_page_epoch;
  notice.invalidates_child_frames_only =
      invalidation->invalidates_child_frames_only;
  notice.resnapshot_required = true;
  notice.observed_at_monotonic_ms = NowMonotonicMs();

  const SubscriptionId subscription_id = subscription.subscription_id();
  delegate_->OnStreamInvalidated(std::move(notice));
  EvaluatePressure(subscription_id, /*projection_already_dead=*/true,
                   /*renderer_reported_loss=*/false);
}

void DeltaSubscriptionManager::OnBackpressure(
    mojom::BackpressureNoticePtr notice) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Entry* entry = CurrentSender();
  if (!entry || !entry->subscription || !notice) {
    return;
  }
  // The renderer says it could not keep up. Whether that killed the projection
  // is decided here, from the classes it says it dropped, rather than from the
  // flag it set: a renderer that dropped a removal and claimed the projection
  // survived would be asking the subscriber to act on a node that is gone.
  bool lost_protected_class = false;
  for (mojom::DeltaCategory category : notice->dropped_categories) {
    const std::optional<DeltaClass> mapped = FromMojom(category);
    if (!mapped.has_value() || !IsSheddableDeltaClass(*mapped)) {
      lost_protected_class = true;
    }
  }
  const bool dead = lost_protected_class || notice->resnapshot_required;
  EvaluatePressure(entry->subscription->subscription_id(),
                   /*projection_already_dead=*/dead,
                   /*renderer_reported_loss=*/true);
}

// --- pressure ---------------------------------------------------------------

void DeltaSubscriptionManager::EvaluatePressure(
    const SubscriptionId& subscription_id,
    bool projection_already_dead,
    bool renderer_reported_loss) {
  Entry* entry = FindByBrokerId(subscription_id);
  if (!entry || !entry->subscription) {
    return;
  }
  DeltaSubscription& subscription = *entry->subscription;
  if (subscription.state() == SubscriptionState::kStopped) {
    return;
  }

  DeltaBackpressurePolicy::Pressure pressure;
  pressure.queue_depth = subscription.queue_depth();
  pressure.queued_bytes = subscription.queued_bytes();
  pressure.budget = subscription.granted().budget;
  pressure.carries_text_deltas = subscription.carries_text_deltas();
  pressure.carries_layout_deltas = subscription.carries_layout_deltas();
  pressure.scope = subscription.scope();
  pressure.scope_already_reduced = subscription.scope_already_reduced();
  pressure.renderer_reported_loss = renderer_reported_loss;
  pressure.projection_already_dead =
      projection_already_dead ||
      subscription.state() == SubscriptionState::kAwaitingResnapshot;

  const DeltaBackpressurePolicy::Decision decision =
      DeltaBackpressurePolicy::Evaluate(pressure, GetDeltaPressureThresholds());
  if (!decision.notify) {
    return;
  }

  std::optional<BackpressureNotice> notice = subscription.Apply(
      decision.action, decision.shed, decision.reduced_scope,
      decision.resnapshot_required, decision.next_state, NowMonotonicMs());
  if (!notice.has_value()) {
    return;
  }

  RecordSubscriptionState(subscription, SubscriptionEventKind::kBackpressure,
                          DeltaRejectReason::kNone);
  delegate_->OnBackpressureNotice(std::move(*notice));
}

void DeltaSubscriptionManager::RecordSubscriptionState(
    const DeltaSubscription& subscription,
    SubscriptionEventKind event,
    DeltaRejectReason reason) {
  SubscriptionRecord record;
  record.subscription_id =
      ToRecordIdentifier(subscription.subscription_id().value);
  record.task_id = ToRecordIdentifier(subscription.granted().task_id.value);
  record.tab_id = ToRecordIdentifier(subscription.tab_id().value);
  record.frame_id = ToRecordIdentifier(subscription.frame_id().value);
  record.page_epoch = ToRecordIdentifier(subscription.page_epoch().value);
  record.to_revision = subscription.cursor().revision;
  record.event_sequence = subscription.cursor().event_sequence;
  record.event = event;
  record.state = subscription.state();
  record.reject_reason = reason;
  record.queue_depth = subscription.queue_depth();
  record.queued_bytes = subscription.queued_bytes();
  record.delivered_count = subscription.counters().delivered;
  record.dropped_delta_count = subscription.counters().shed_notices;
  record.gap_count = subscription.counters().gaps;
  record.duplicate_count = subscription.counters().discarded_duplicate;
  record.out_of_order_count = subscription.counters().discarded_out_of_order;
  record.late_after_terminal_count =
      subscription.sequence_counters().late_after_terminal;
  record.resnapshot_required =
      subscription.state() == SubscriptionState::kAwaitingResnapshot;
  observability_->Record(record);
}

}  // namespace taffy
