// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_SUBSCRIPTION_MANAGER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_SUBSCRIPTION_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/components/intelligence/content/delta_subscription.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/renderer_call_deadline.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_subscription.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"

// The browser half of the delta path (protocol section 10): the receiver a
// renderer sends deltas to, the registry of live subscriptions, and the place
// backpressure is decided.
//
// Three things about it are load bearing.
//
// **A renderer-minted subscription identifier is never adopted.** The endpoint
// mints its own identifier for its own bookkeeping, and it appears on every
// delta. This class mints a separate one, keys everything by that, and binds a
// dedicated receiver per subscription so that the message's provenance comes
// from which pipe it arrived on rather than from what it claims to be. The
// renderer's identifier is compared, so a mismatch is a rejected message; it is
// never used to look anything up.
//
// **A delta is delivered only when it provably applies.** DeltaSubscription
// owns that decision and this class does not second-guess it. What this class
// adds is everything the decision needs that lives outside one subscription:
// the endpoint, the epoch the broker currently holds, and the encoder.
//
// **Losing the stream is always safe.** Every failure path here ends in either
// a backpressure notice that says a fresh snapshot is required or an
// invalidation that says the handles are dead. Nothing degrades quietly,
// because a subscriber that believed a thinned stream was complete is exactly
// the failure protocol section 10 is written to prevent.
//
// UI thread only.

namespace taffy {

class PageIntelligenceBroker;

class DeltaSubscriptionManager : public mojom::PageDeltaClient {
 public:
  // What the manager hands back to the service implementation, which owns the
  // one-terminal-result contract with the isolated core.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The terminal result of one Subscribe call.
    virtual void OnSubscriptionSettled(SubscriptionEnvelope envelope) = 0;

    // A delta that provably applies to the projection the subscriber last
    // acknowledged.
    virtual void OnDeltaReady(DeltaEnvelope delta) = 0;

    // Load shedding, or a loss the renderer already took.
    virtual void OnBackpressureNotice(BackpressureNotice notice) = 0;

    // The stream's own reason for saying the projection or the handles are
    // dead. Distinct from the broker's navigation invalidation, which reaches
    // the service directly.
    virtual void OnStreamInvalidated(InvalidationNotice notice) = 0;
  };

  DeltaSubscriptionManager(base::WeakPtr<PageIntelligenceBroker> broker,
                           Delegate* delegate,
                           ObservabilityRecorder* observability);
  DeltaSubscriptionManager(const DeltaSubscriptionManager&) = delete;
  DeltaSubscriptionManager& operator=(const DeltaSubscriptionManager&) = delete;
  ~DeltaSubscriptionManager() override;

  // The encoder that turns a renderer delta into the opaque payload the isolated core
  // decodes. Borrowed, not owned: the service implementation owns exactly one
  // encoder and both paths use it, so a snapshot and a delta can never be
  // encoded differently.
  void SetGraphPayloadEncoder(GraphPayloadEncoder* encoder);

  // Opens a stream. `clamped` must already have been through
  // ClampSubscriptionRequest; this class does no narrowing and asserts that
  // fact rather than repeating the clamp.
  //
  // Exactly one OnSubscriptionSettled follows, always.
  void Subscribe(SubscriptionRequest clamped);

  // Ends a stream. Idempotent.
  void Unsubscribe(const SubscriptionId& subscription_id);

  // The subscriber applied everything up to `event_sequence`. This is what
  // relieves queue pressure, and therefore what lets a paused subscription
  // resume.
  void Acknowledge(const SubscriptionId& subscription_id,
                   EventSequence event_sequence);

  // A fresh snapshot re-established a projection. The only way out of
  // kAwaitingResnapshot, and the reason a resnapshot is worth asking for.
  void RebaseAfterSnapshot(const FrameId& frame_id,
                           const PageEpoch& page_epoch,
                           GraphRevision revision,
                           EventSequence event_sequence);

  // Navigation invalidation, forwarded from the broker. Runs synchronously
  // inside the broker's notification so no delta can be delivered between the
  // invalidation and the teardown (protocol section 6.3).
  void OnPageInvalidated(const InvalidationNotice& notice);

  // Memory pressure stops deltas before anything else (protocol section 15).
  // Returns how many subscriptions were stopped.
  size_t StopAllForMemoryPressure();

  // Stops every stream. Tab teardown and profile teardown.
  void StopAll(InvalidationCode reason);

  size_t subscription_count() const { return subscriptions_.size(); }
  DeltaSubscription* GetForTesting(const SubscriptionId& subscription_id);

  // mojom::PageDeltaClient:
  void OnDelta(mojom::PageDeltaPtr delta) override;
  void OnInvalidated(mojom::PageInvalidationPtr invalidation) override;
  void OnBackpressure(mojom::BackpressureNoticePtr notice) override;

 private:
  struct Entry {
    Entry();
    Entry(Entry&&);
    ~Entry();

    std::unique_ptr<DeltaSubscription> subscription;
    // What the endpoint called it. Compared, never used as a key.
    std::string renderer_subscription_id;
    // The pipe this stream arrives on. Held so teardown can drop it, which is
    // the line that makes "nothing further is delivered" true rather than
    // intended.
    mojo::ReceiverId receiver_id = 0;
    // The request this stream is still waiting on, and the budget it was
    // granted. Cleared once the endpoint's Subscribe reply arrives.
    SubscriptionRequest pending;
    bool awaiting_reply = true;
    // The bounded wait on the endpoint's Subscribe reply. Held on the entry,
    // and not only inside the reply wrapper, because mojo destroys a pending
    // reply callback on a pipe disconnect rather than running it: a guard whose
    // only reference lived there would take its timer with it. Released once
    // the subscription is settled, so a live stream carries no timer.
    scoped_refptr<RendererCallDeadline> subscribe_deadline;
  };

  void OnSubscribeReply(SubscriptionId subscription_id,
                        mojom::SubscriptionResultPtr result);
  // The endpoint never answered Subscribe.
  void OnSubscribeDeadline(SubscriptionId subscription_id);
  void SettleWithCode(const RequestId& request_id,
                      const SubscriptionRequest& clamped,
                      ObservationResultCode code);

  // Runs the ladder and applies whatever it decided. Called after every delta
  // and every acknowledgement, because both change the pressure.
  void EvaluatePressure(const SubscriptionId& subscription_id,
                        bool projection_already_dead,
                        bool renderer_reported_loss);

  void RecordSubscriptionState(const DeltaSubscription& subscription,
                               SubscriptionEventKind event,
                               DeltaRejectReason reason);

  Entry* FindByBrokerId(const SubscriptionId& subscription_id);
  // The subscription that owns the receiver the current message arrived on.
  // Null when the receiver has already been torn down, which is a message that
  // lost a race with teardown rather than an error.
  Entry* CurrentSender();

  // `remove_receiver` is false only when the receiver is already being torn
  // down by mojo, which is the case inside a disconnect handler: removing it
  // there would be removing the object mojo is currently unwinding.
  void Teardown(const SubscriptionId& subscription_id, bool remove_receiver);

  // Takes a paused subscription back to active when its queue has drained
  // below the pause threshold.
  void MaybeResume(const SubscriptionId& subscription_id);

  base::WeakPtr<PageIntelligenceBroker> broker_;
  const raw_ptr<Delegate> delegate_;
  const raw_ptr<ObservabilityRecorder> observability_;
  raw_ptr<GraphPayloadEncoder> encoder_ = nullptr;

  std::map<SubscriptionId, Entry> subscriptions_;
  // Context is the broker-minted identifier. Which pipe a message arrived on is
  // the only provenance this class trusts.
  mojo::ReceiverSet<mojom::PageDeltaClient, SubscriptionId> receivers_;
  uint64_t next_subscription_value_ = 1;

  base::WeakPtrFactory<DeltaSubscriptionManager> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_DELTA_SUBSCRIPTION_MANAGER_H_
