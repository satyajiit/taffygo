// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The manager's own state and the subscription lifecycle: what it holds, how a
// subscription is acknowledged, resumed, rebased, and torn down.
//
// The subscribe handshake and the incoming stream are sibling translation
// units. The split follows the three things a subscription does, not a line
// count: it is established, it carries messages, and it ends.


#include "taffy/components/intelligence/content/delta_subscription_manager.h"

#include <utility>
#include <vector>
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/delta_backpressure_policy.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "content/public/browser/browser_thread.h"

namespace taffy {

DeltaSubscriptionManager::Entry::Entry() = default;
DeltaSubscriptionManager::Entry::Entry(Entry&&) = default;
DeltaSubscriptionManager::Entry::~Entry() = default;

DeltaSubscriptionManager::DeltaSubscriptionManager(
    base::WeakPtr<PageIntelligenceBroker> broker,
    Delegate* delegate,
    ObservabilityRecorder* observability)
    : broker_(std::move(broker)),
      delegate_(delegate),
      observability_(observability) {
  CHECK(delegate_);
  CHECK(observability_);
  // A receiver that disconnects means the endpoint went away with a stream
  // open. That is an invalidation, not a quiet stop: the subscriber is holding
  // a projection of a document nobody is reporting on any more.
  receivers_.set_disconnect_handler(base::BindRepeating(
      [](base::WeakPtr<DeltaSubscriptionManager> self) {
        if (!self) {
          return;
        }
        const SubscriptionId id = self->receivers_.current_context();
        // Mojo is already unwinding this receiver, so teardown must not try to
        // remove it.
        self->Teardown(id, /*remove_receiver=*/false);
      },
      weak_factory_.GetWeakPtr()));
}

DeltaSubscriptionManager::~DeltaSubscriptionManager() = default;

void DeltaSubscriptionManager::SetGraphPayloadEncoder(
    GraphPayloadEncoder* encoder) {
  encoder_ = encoder;
}

DeltaSubscription* DeltaSubscriptionManager::GetForTesting(
    const SubscriptionId& subscription_id) {
  Entry* entry = FindByBrokerId(subscription_id);
  return entry ? entry->subscription.get() : nullptr;
}

DeltaSubscriptionManager::Entry* DeltaSubscriptionManager::FindByBrokerId(
    const SubscriptionId& subscription_id) {
  auto it = subscriptions_.find(subscription_id);
  return it == subscriptions_.end() ? nullptr : &it->second;
}

DeltaSubscriptionManager::Entry* DeltaSubscriptionManager::CurrentSender() {
  return FindByBrokerId(receivers_.current_context());
}

// --- lifecycle --------------------------------------------------------------

void DeltaSubscriptionManager::Acknowledge(const SubscriptionId& subscription_id,
                                           EventSequence event_sequence) {
  Entry* entry = FindByBrokerId(subscription_id);
  if (!entry || !entry->subscription) {
    return;
  }
  entry->subscription->Acknowledge(event_sequence);
  // Draining the queue is the only thing that relieves pressure, so it is also
  // the only thing that can end a pause.
  MaybeResume(subscription_id);
}

void DeltaSubscriptionManager::MaybeResume(
    const SubscriptionId& subscription_id) {
  Entry* entry = FindByBrokerId(subscription_id);
  if (!entry || !entry->subscription ||
      entry->subscription->state() != SubscriptionState::kPaused) {
    return;
  }
  DeltaSubscription& subscription = *entry->subscription;

  DeltaBackpressurePolicy::Pressure pressure;
  pressure.queue_depth = subscription.queue_depth();
  pressure.queued_bytes = subscription.queued_bytes();
  pressure.budget = subscription.granted().budget;
  if (DeltaBackpressurePolicy::PressurePercent(pressure) >=
      GetDeltaPressureThresholds().pause_percent) {
    return;
  }

  // Resuming is itself reportable: a subscriber that was told the stream
  // paused has to be told it started again, or it will keep resnapshotting
  // defensively.
  std::optional<BackpressureNotice> notice = subscription.Apply(
      BackpressureAction::kCoalesced, DeltaShedPlan(), std::nullopt,
      /*resnapshot_required=*/false, SubscriptionState::kActive,
      NowMonotonicMs());
  RecordSubscriptionState(subscription, SubscriptionEventKind::kBackpressure,
                          DeltaRejectReason::kNone);
  if (notice.has_value()) {
    delegate_->OnBackpressureNotice(std::move(*notice));
  }
}

void DeltaSubscriptionManager::RebaseAfterSnapshot(
    const FrameId& frame_id,
    const PageEpoch& page_epoch,
    GraphRevision revision,
    EventSequence event_sequence) {
  for (auto& [subscription_id, entry] : subscriptions_) {
    if (!entry.subscription || entry.subscription->frame_id() != frame_id ||
        entry.subscription->page_epoch() != page_epoch) {
      continue;
    }
    if (entry.subscription->state() != SubscriptionState::kAwaitingResnapshot) {
      continue;
    }
    entry.subscription->Rebase(revision, event_sequence);
    RecordSubscriptionState(*entry.subscription,
                            SubscriptionEventKind::kResnapshotRebased,
                            DeltaRejectReason::kNone);
  }
}

void DeltaSubscriptionManager::OnPageInvalidated(
    const InvalidationNotice& notice) {
  std::vector<SubscriptionId> affected;
  for (const auto& [subscription_id, entry] : subscriptions_) {
    if (!entry.subscription) {
      continue;
    }
    if (entry.subscription->frame_id() != notice.frame_id) {
      continue;
    }
    // A revision-only invalidation leaves the epoch alive, so the stream
    // survives and the next delta is judged on its own merits. Anything that
    // retires the epoch ends the stream: its handles are dead, and a new
    // binding is the only way forward.
    if (!notice.retires_page_epoch &&
        !InvalidationRetiresDocument(notice.reason)) {
      continue;
    }
    affected.push_back(subscription_id);
  }
  for (const SubscriptionId& subscription_id : affected) {
    Teardown(subscription_id, /*remove_receiver=*/true);
  }
}

size_t DeltaSubscriptionManager::StopAllForMemoryPressure() {
  const size_t stopped = subscriptions_.size();
  StopAll(InvalidationCode::kMemoryPressure);
  return stopped;
}

void DeltaSubscriptionManager::StopAll(InvalidationCode reason) {
  std::vector<SubscriptionId> ids;
  for (const auto& [subscription_id, entry] : subscriptions_) {
    ids.push_back(subscription_id);
  }
  for (const SubscriptionId& subscription_id : ids) {
    Entry* entry = FindByBrokerId(subscription_id);
    if (!entry || !entry->subscription) {
      continue;
    }
    InvalidationNotice notice;
    notice.schema_version = kBipSchemaVersion;
    notice.subscription_id = subscription_id;
    notice.tab_id = entry->subscription->tab_id();
    notice.frame_id = entry->subscription->frame_id();
    notice.page_epoch = entry->subscription->page_epoch();
    notice.event_sequence = entry->subscription->cursor().event_sequence;
    notice.reason = reason;
    notice.retires_page_epoch = InvalidationRetiresDocument(reason);
    notice.resnapshot_required = true;
    notice.observed_at_monotonic_ms = NowMonotonicMs();
    delegate_->OnStreamInvalidated(std::move(notice));
    Teardown(subscription_id, /*remove_receiver=*/true);
  }
}

void DeltaSubscriptionManager::Unsubscribe(
    const SubscriptionId& subscription_id) {
  Teardown(subscription_id, /*remove_receiver=*/true);
}

void DeltaSubscriptionManager::Teardown(const SubscriptionId& subscription_id,
                                        bool remove_receiver) {
  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    return;
  }
  Entry entry = std::move(it->second);
  subscriptions_.erase(it);

  if (entry.subscription) {
    entry.subscription->Stop();
    RecordSubscriptionState(*entry.subscription, SubscriptionEventKind::kClosed,
                            DeltaRejectReason::kNone);
  }
  // Dropping the receiver is what actually stops the renderer's messages
  // reaching this process: everything above is bookkeeping, and this is the
  // line that makes "after Unsubscribe returns, nothing further is delivered"
  // true rather than intended.
  if (remove_receiver) {
    receivers_.Remove(entry.receiver_id);
  }

  // Whatever ends the subscription also ends its bounded wait. Claiming the
  // guard rather than relying on the entry's destructor to stop the timer says
  // so in one line, and keeps the deadline from producing a second terminal
  // result for a request this call is about to settle.
  if (entry.subscribe_deadline) {
    entry.subscribe_deadline->Claim();
  }

  // A subscribe that was still waiting for the endpoint's reply has to settle,
  // and this is the only place left that can do it.
  //
  // Mojo destroys a pending reply callback on disconnect rather than running
  // it, so OnSubscribeReply is not called when the endpoint goes away, the
  // renderer dies, or the interface was never bound in that process. Without
  // this line the request had no terminal result at all — not a refusal, not
  // an error, nothing — and the contract is exactly one per request
  // (protocol section 6.3). A subscriber left waiting forever is the failure
  // mode that looks identical to a renderer that is merely slow.
  //
  // Settling here cannot produce a second result: OnSubscribeReply looks the
  // entry up first and returns when it is gone, which this call has already
  // made true.
  if (entry.awaiting_reply) {
    SettleWithCode(entry.pending.request_id, entry.pending,
                   ObservationResultCode::kDocumentInactive);
  }
}

}  // namespace taffy
