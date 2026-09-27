// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Establishing a subscription: the request the broker sends, the reply it
// gets, and how a subscription that never started is settled with a code.
//
// The request that arrives here has already been clamped. Nothing in this file
// narrows it further, so a reviewer looking for the clamp finds exactly one
// place with it.


#include "taffy/components/intelligence/content/delta_subscription_manager.h"

#include <utility>
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "content/public/browser/browser_thread.h"

namespace taffy {

// --- subscribe --------------------------------------------------------------

void DeltaSubscriptionManager::Subscribe(SubscriptionRequest clamped) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(clamped.request_id.is_valid());

  if (!broker_) {
    SettleWithCode(clamped.request_id, clamped,
                   ObservationResultCode::kDocumentInactive);
    return;
  }
  FrameObservationEndpoint* endpoint =
      broker_->GetActionableEndpoint(clamped.frame_id);
  if (!endpoint || !endpoint->remote().is_bound()) {
    SettleWithCode(clamped.request_id, clamped,
                   ObservationResultCode::kDocumentInactive);
    return;
  }
  // A subscription names the epoch it expects, and a mismatch is stale rather
  // than a silent rebind onto whatever document is there now. A standing
  // stream makes this stricter than a one-shot observation: a wrong bind would
  // keep being wrong for as long as the stream lived.
  if (clamped.expected_page_epoch != endpoint->page_epoch()) {
    SettleWithCode(clamped.request_id, clamped,
                   ObservationResultCode::kStalePageEpoch);
    return;
  }
  if (!encoder_) {
    // No encoder means a delta could carry no graph. Saying so is honest;
    // opening a stream that could only ever deliver empty payloads is not.
    SettleWithCode(clamped.request_id, clamped,
                   ObservationResultCode::kUnsupported);
    return;
  }

  const SubscriptionId subscription_id{base::StrCat(
      {"sub_", base::NumberToString(next_subscription_value_++)})};

  mojom::PageSubscriptionOptionsPtr options = ToMojom(clamped);

  mojo::PendingRemote<mojom::PageDeltaClient> client;
  Entry entry;
  entry.receiver_id = receivers_.Add(
      this, client.InitWithNewPipeAndPassReceiver(), subscription_id);
  // The clamped request is kept so the reply builds the subscription with the
  // budget that was actually granted rather than the one that was asked for.
  entry.pending = std::move(clamped);
  entry.awaiting_reply = true;

  // Bounded, like every call that leaves this process (protocol section 15).
  //
  // A DeltaBudget has no deadline axis — it bounds queue depth, queued bytes,
  // delta size and the coalescing window, all of which are about a stream that
  // is already running rather than about establishing one — so there is no
  // per-request deadline here to narrow. The bound is therefore the process
  // ceiling, taken through the same clamp helper the snapshot, negotiation and
  // action paths use, so that one number stands behind every bounded renderer
  // call.
  //
  // Teardown already settles a subscribe that was still waiting when the pipe
  // went away. What it cannot see is an endpoint that is still there and
  // simply never answers, which is the case this closes.
  entry.subscribe_deadline = RendererCallDeadline::Arm(
      base::Milliseconds(ClampDeadlineMs(0, GetProcessBudgetLimits())),
      base::BindOnce(&DeltaSubscriptionManager::OnSubscribeDeadline,
                     weak_factory_.GetWeakPtr(), subscription_id));
  scoped_refptr<RendererCallDeadline> guard = entry.subscribe_deadline;
  subscriptions_.emplace(subscription_id, std::move(entry));

  endpoint->remote()->Subscribe(
      std::move(options), std::move(client),
      BindReplyWithDeadline(
          std::move(guard),
          base::BindOnce(&DeltaSubscriptionManager::OnSubscribeReply,
                         weak_factory_.GetWeakPtr(), subscription_id)));
}

void DeltaSubscriptionManager::OnSubscribeDeadline(
    SubscriptionId subscription_id) {
  Entry* entry = FindByBrokerId(subscription_id);
  if (!entry || !entry->awaiting_reply) {
    return;  // Already settled. Exactly one result per request id.
  }
  const SubscriptionRequest clamped = entry->pending;
  // Cleared before Teardown so that Teardown's own settle — the one that
  // covers a pipe disconnect — does not fire as well and answer the same
  // request twice with kDocumentInactive.
  entry->awaiting_reply = false;
  Teardown(subscription_id, /*remove_receiver=*/true);

  // The receiver is gone, so the endpoint's late reply and any delta it may
  // already have sent are dropped rather than delivered against a stream the
  // subscriber was told does not exist.
  SettleWithCode(clamped.request_id, clamped,
                 ObservationResultCode::kDeadlineExceeded);
}

void DeltaSubscriptionManager::OnSubscribeReply(
    SubscriptionId subscription_id,
    mojom::SubscriptionResultPtr result) {
  Entry* entry = FindByBrokerId(subscription_id);
  if (!entry || !entry->awaiting_reply) {
    return;  // Torn down while the reply was in flight.
  }
  const SubscriptionRequest clamped = entry->pending;
  entry->awaiting_reply = false;
  // The bounded wait is over: this reply claimed the call, or the wrapper
  // would not have let it run at all. Dropping the reference here is what
  // keeps a live subscription from carrying a timer for the rest of its life.
  entry->subscribe_deadline.reset();

  const bool ok = result && result->code == mojom::ObservationResultCode::kOk &&
                  result->subscription_id.has_value() &&
                  result->base_revision.has_value();
  if (!ok) {
    // The endpoint refused, or answered without the two facts a projection
    // cannot start without. Either way there is no stream.
    const ObservationResultCode code =
        result ? static_cast<ObservationResultCode>(result->code)
               : ObservationResultCode::kInternalError;
    Teardown(subscription_id, /*remove_receiver=*/true);
    SettleWithCode(clamped.request_id, clamped, code);
    return;
  }

  entry->renderer_subscription_id = *result->subscription_id;
  entry->subscription = std::make_unique<DeltaSubscription>(
      subscription_id, clamped.tab_id, clamped.frame_id,
      clamped.expected_page_epoch, *result->base_revision, clamped,
      base::BindRepeating(
          [](base::WeakPtr<DeltaSubscriptionManager> self,
             DeltaEnvelope delta) {
            if (self) {
              self->delegate_->OnDeltaReady(std::move(delta));
            }
          },
          weak_factory_.GetWeakPtr()));

  RecordSubscriptionState(*entry->subscription,
                          SubscriptionEventKind::kOpened,
                          DeltaRejectReason::kNone);

  SubscriptionEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.request_id = clamped.request_id;
  envelope.code = ObservationResultCode::kOk;
  envelope.tab_id = clamped.tab_id;
  envelope.frame_id = clamped.frame_id;
  envelope.subscription_id = subscription_id;
  envelope.page_epoch = clamped.expected_page_epoch;
  envelope.base_revision = *result->base_revision;
  envelope.granted_budget = clamped.budget;
  envelope.granted_scope = clamped.scope;
  envelope.text_deltas_granted = clamped.include_text_deltas;
  envelope.layout_deltas_granted = clamped.include_layout_deltas;
  envelope.observed_at_monotonic_ms = NowMonotonicMs();
  delegate_->OnSubscriptionSettled(std::move(envelope));
}

void DeltaSubscriptionManager::SettleWithCode(
    const RequestId& request_id,
    const SubscriptionRequest& clamped,
    ObservationResultCode code) {
  SubscriptionEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.request_id = request_id;
  envelope.code = code;
  envelope.tab_id = clamped.tab_id;
  envelope.frame_id = clamped.frame_id;
  envelope.granted_budget = clamped.budget;
  envelope.granted_scope = clamped.scope;
  envelope.observed_at_monotonic_ms = NowMonotonicMs();
  delegate_->OnSubscriptionSettled(std::move(envelope));
}

}  // namespace taffy
