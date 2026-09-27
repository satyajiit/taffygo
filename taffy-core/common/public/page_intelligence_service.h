// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_PAGE_INTELLIGENCE_SERVICE_H_
#define TAFFY_PUBLIC_PAGE_INTELLIGENCE_SERVICE_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_protocol_support.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/common/public/bip_subscription.h"

// The trusted browser-process API used by the Core Service effect broker.
//
// Rust never holds a renderer Mojo remote. Chromium object and sequence
// affinity stay inside browser C++; the sandboxed core emits a typed effect,
// and the browser adapter drives this small owned-value-type surface.
//
// The contract, which is the same one decision 0004 states for every async
// call across the boundary:
//
//   * Every call that can produce work returns a RequestId synchronously.
//   * Exactly one terminal result is delivered per RequestId, ever. Not zero,
//     not two. A cancelled request still gets its one terminal result, with a
//     cancellation code.
//   * Cancel() is advisory about timing and absolute about outcome: after it,
//     the only result that can still arrive is a terminal one. A late renderer
//     reply is dropped and counted (protocol section 6.3).
//   * Every request carries a deadline. A request with no deadline is rejected
//     rather than run forever.
//   * No borrowed reference, no panic, no exception and no Chromium pointer
//     crosses this boundary in either direction.
//
// Threading: every method is called on the browser UI thread, and every sink
// method is invoked on the browser UI thread. The Core Service Mojo boundary
// and browser effect broker own the process and sequence hops.

namespace taffy {

// Implemented by the browser-side Core Service adapter. The service holds a
// raw reference to it for the service's lifetime; the owning WebContents
// integration must outlive the service.
class PageIntelligenceResultSink {
 public:
  virtual ~PageIntelligenceResultSink() = default;

  // Terminal results. Exactly one per RequestId.
  virtual void OnProtocolSupport(ProtocolSupportEnvelope result) = 0;
  virtual void OnObservationResult(ObservationEnvelope result) = 0;
  virtual void OnSubscriptionResult(SubscriptionEnvelope result) = 0;
  virtual void OnActionResult(ActionResult result) = 0;

  // A stream update. Not a reply: it carries a SubscriptionId, not a
  // RequestId, and any number of them follow one Subscribe call. The broker
  // has already proven that this delta applies to the projection it last
  // acknowledged — epoch, from-revision and sequence all matched — so the
  // core applies it without re-deciding that (protocol section 10).
  virtual void OnDelta(DeltaEnvelope delta) = 0;

  // Unsolicited. These are not replies and carry no RequestId; they tell the
  // core that handles it holds are dead, or that it must slow down.
  virtual void OnPageInvalidated(InvalidationNotice notice) = 0;
  virtual void OnBackpressure(BackpressureNotice notice) = 0;

  // The user took over, or Chromium delivered direct user input to a tab the
  // assistant holds a lease on. Undispatched mutation authority for that tab
  // is already revoked by the time this arrives (protocol section 17.4).
  virtual void OnActorLeasePreempted(ActorLeaseId lease_id, TabId tab_id) = 0;
};

// A durable record the dispatcher writes before it causes a side effect
// (protocol section 12, step 7). Content free by construction: identifiers,
// decision facts and counts only.
struct DispatchIntentRecord {
  DispatchId dispatch_id;
  TaskId task_id;
  ActionId action_id;
  // Set for a node-targeted action. Unset for a browser-owned command.
  std::optional<ActionType> action_type;
  std::optional<BrowserCommandType> command_type;
  CapabilityReference capability_reference;
  ActorLeaseId actor_lease_id;
  TabId tab_id;
  FrameId frame_id;
  PageEpoch page_epoch;
  GraphRevision graph_revision = 0;
  Origin origin;
  IdempotencyPolicy idempotency_policy = IdempotencyPolicy::kNonIdempotent;
  MonotonicMillis recorded_at_monotonic_ms = 0;
};

// A move-only completion object keeps the public service surface independent
// of Chromium's callback library while still making a physical journal append
// asynchronous. The sink owns it and must invoke Run exactly once, after the
// sequenced writer has committed or refused the record.
class TaskJournalAppendCallback {
 public:
  virtual ~TaskJournalAppendCallback() = default;
  virtual void Run(bool committed) = 0;
};

// Implemented by the profile's browser-side durable storage adapter.
class TaskJournalSink {
 public:
  virtual ~TaskJournalSink() = default;

  virtual void RecordDispatching(
      DispatchIntentRecord record,
      std::unique_ptr<TaskJournalAppendCallback> callback) = 0;
  virtual void RecordTerminalResult(
      ActionResult result,
      std::unique_ptr<TaskJournalAppendCallback> callback) = 0;
};

// A request to hold mutation authority over one tab
// (domain model section 12.3).
struct ActorLeaseRequest {
  TaskId task_id;
  TabId tab_id;
  // Clamped to the process ceiling.
  uint32_t requested_duration_ms = 0;
  // True when the task intends to mutate. At most one mutating lease exists
  // per tab, and direct user input preempts it.
  bool mutating = false;
};

enum class ActorLeaseResultCode : uint8_t {
  kIssued = 0,
  kTabGone = 1,
  kAlreadyHeld = 2,
  kDeniedByPolicy = 3,
  kInternalError = 4,
};

struct ActorLeaseResult {
  ActorLeaseResultCode code = ActorLeaseResultCode::kInternalError;
  ActorLeaseId lease_id;
  MonotonicMillis expires_at_monotonic_ms = 0;
};

class PageIntelligenceService {
 public:
  virtual ~PageIntelligenceService() = default;

  // Protocol negotiation for one tab. Reports the endpoint's supported
  // adapters, scopes and action types so the core runtime can plan against real
  // support rather than discovering UNSUPPORTED after the fact.
  virtual RequestId QueryProtocolSupport(TabId tab_id) = 0;

  // Bounded observation. The request's budget is clamped to the process
  // ceiling and then to the task's grant before anything leaves the browser
  // process; the returned envelope reports what actually applied.
  virtual RequestId SubmitObservation(ObservationRequest request,
                                      ObservationPolicyGrant grant) = 0;

  // The authorized action path. Runs the stale-node algorithm of protocol
  // section 12 in order, consumes the capability in this process before any
  // renderer command exists, journals the intent before the side effect, and
  // observes the declared postconditions before reporting anything as
  // verified.
  virtual RequestId SubmitAction(AuthorizedActionEnvelope envelope) = 0;

  // The browser-owned command path: navigate, open a task tab, search. Same
  // authority, same journal, same verification; no renderer command.
  virtual RequestId SubmitBrowserCommand(AuthorizedBrowserCommand command) = 0;

  // Opens a delta stream on one frame. The subscription's budgets are clamped
  // exactly like an observation's, and the terminal SubscriptionEnvelope
  // reports the base revision the projection starts from.
  //
  // A subscription is an optimization and never a correctness dependency: a
  // caller that loses one falls back to SubmitObservation, which is why
  // nothing on this interface requires a subscription to exist first.
  virtual RequestId Subscribe(SubscriptionRequest request,
                              ObservationPolicyGrant grant) = 0;

  // Ends a subscription. Idempotent, and safe to call for an identifier the
  // broker has already stopped. No further delta or backpressure notice for
  // that identifier is delivered after this returns.
  virtual void Unsubscribe(SubscriptionId subscription_id) = 0;

  // The subscriber applied every delta up to and including `event_sequence`.
  //
  // This is not bookkeeping. It is the only signal the broker has that the
  // subscriber is keeping up, so it is what queue depth is measured from and
  // therefore what the backpressure ladder climbs. A subscriber that stops
  // acknowledging is a subscriber that will be shed, then paused, then asked
  // to resnapshot — in that order, and each step is reported.
  virtual void AcknowledgeDelta(SubscriptionId subscription_id,
                                EventSequence event_sequence) = 0;

  // Terminal result still arrives, with a cancellation code.
  virtual void Cancel(RequestId request_id) = 0;

  virtual ActorLeaseResult IssueActorLease(ActorLeaseRequest request) = 0;
  virtual void ReleaseActorLease(ActorLeaseId lease_id) = 0;
};

}  // namespace taffy

#endif  // TAFFY_PUBLIC_PAGE_INTELLIGENCE_SERVICE_H_
