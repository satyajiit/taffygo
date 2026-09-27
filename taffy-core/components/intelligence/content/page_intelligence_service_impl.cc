// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "taffy/components/intelligence/content/adapter_requirement_check.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

PageIntelligenceServiceImpl::PendingObservation::PendingObservation() = default;
PageIntelligenceServiceImpl::PendingObservation::PendingObservation(
    PendingObservation&&) = default;
PageIntelligenceServiceImpl::PendingObservation::~PendingObservation() =
    default;

PageIntelligenceServiceImpl::PageIntelligenceServiceImpl(
    content::WebContents* web_contents,
    PageIntelligenceBroker* broker,
    PageIntelligenceResultSink* sink,
    TaskJournalSink* journal,
    ObservabilitySink* observability_sink,
    ActorLeaseRegistry* actor_leases,
    CapabilityLedger* capabilities)
    : web_contents_(web_contents),
      broker_(broker),
      sink_(sink),
      tab_id_(broker ? broker->tab_id() : TabId()),
      actor_leases_(actor_leases),
      capabilities_(capabilities),
      dispatcher_(web_contents,
                  broker ? broker->GetWeakPtr()
                         : base::WeakPtr<PageIntelligenceBroker>(),
                  actor_leases_,
                  capabilities_,
                  journal,
                  &observability_),
      encoder_(MakeMetadataOnlyGraphPayloadEncoder()),
      deltas_(broker ? broker->GetWeakPtr()
                     : base::WeakPtr<PageIntelligenceBroker>(),
              this,
              &observability_),
      memory_(this) {
  CHECK(broker_);
  CHECK(actor_leases_);
  CHECK(capabilities_);
  observability_.SetSink(observability_sink);
  deltas_.SetGraphPayloadEncoder(encoder_.get());
  negotiator_ = std::make_unique<ProtocolNegotiator>(base::BindRepeating(
      [](base::WeakPtr<PageIntelligenceServiceImpl> self,
         ProtocolSupportEnvelope envelope) {
        if (self && self->sink_) {
          self->sink_->OnProtocolSupport(std::move(envelope));
        }
      },
      weak_factory_.GetWeakPtr()));
  broker_observation_.Observe(broker_);
}

PageIntelligenceServiceImpl::~PageIntelligenceServiceImpl() {
  broker_observation_.Reset();
  // WebContents owns this service. Tab teardown revokes profile authority
  // before the tab can disappear; moving the same WebContents between windows
  // does not destroy the service and therefore does not revoke its lease.
  TearDownTabAuthority();
}

void PageIntelligenceServiceImpl::TearDownTabAuthority() {
  if (tab_authority_torn_down_) {
    return;
  }
  tab_authority_torn_down_ = true;
  dispatcher_.OnActorLeasesPreempted(actor_leases_->PreemptTab(tab_id_));
}

void PageIntelligenceServiceImpl::OnBrokerDestroyed(TabId tab_id) {
  CHECK(tab_id == tab_id_);
  // Reset while the source's destructor body and ObserverList are alive. The
  // service may itself be destroyed later because its WebContentsUserData has
  // an independent, deliberately unspecified destruction position.
  broker_observation_.Reset();
  broker_ = nullptr;
  TearDownTabAuthority();
}

void PageIntelligenceServiceImpl::SetGraphPayloadEncoder(
    std::unique_ptr<GraphPayloadEncoder> encoder) {
  encoder_ =
      encoder ? std::move(encoder) : MakeMetadataOnlyGraphPayloadEncoder();
  // One encoder, two paths. A snapshot and a delta encoded differently would
  // give the isolated core two dialects of the same graph.
  deltas_.SetGraphPayloadEncoder(encoder_.get());
}

void PageIntelligenceServiceImpl::SetBrowserActionDelegate(
    BrowserActionDelegate* delegate) {
  dispatcher_.SetBrowserActionDelegate(delegate);
}

void PageIntelligenceServiceImpl::SetObservedLinkResolver(
    ObservedLinkResolver* resolver) {
  dispatcher_.SetObservedLinkResolver(resolver);
}

void PageIntelligenceServiceImpl::SetBrowserEffectSource(
    BrowserEffectSource* source) {
  dispatcher_.SetBrowserEffectSource(source);
}

void PageIntelligenceServiceImpl::SetValueReferenceVault(
    ValueReferenceVault* vault) {
  dispatcher_.SetValueReferenceVault(vault);
}

void PageIntelligenceServiceImpl::ResolveNodeFacts(
    const NodeHandle& handle,
    base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved) {
  dispatcher_.ResolveNodeFacts(handle, std::move(on_resolved));
}

RequestId PageIntelligenceServiceImpl::MintRequestId(const char* prefix) {
  return RequestId{
      base::StrCat({prefix, base::NumberToString(next_request_value_++)})};
}

base::TimeDelta PageIntelligenceServiceImpl::RendererDeadlineFor(
    const ObservationBudget& budget) const {
  // Every call that leaves this process is bounded, and the bound is the
  // clamped deadline rather than the requested one, so a client cannot ask for
  // an unbounded wait by asking for a very long one.
  return base::Milliseconds(
      ClampDeadlineMs(budget.deadline_ms, GetProcessBudgetLimits()));
}

// --- actions ----------------------------------------------------------------

RequestId PageIntelligenceServiceImpl::SubmitAction(
    AuthorizedActionEnvelope envelope) {
  const RequestId request_id = MintRequestId("req_");
  dispatcher_.Dispatch(request_id, std::move(envelope),
                       base::BindOnce(
                           [](base::WeakPtr<PageIntelligenceServiceImpl> self,
                              ActionResult result) {
                             if (self && self->sink_) {
                               self->sink_->OnActionResult(std::move(result));
                             }
                           },
                           weak_factory_.GetWeakPtr()));
  return request_id;
}

RequestId PageIntelligenceServiceImpl::SubmitTaskAction(
    AuthorizedActionEnvelope envelope,
    ActionDispatcher::CompletionCallback on_complete) {
  const RequestId request_id = MintRequestId("req_");
  dispatcher_.Dispatch(request_id, std::move(envelope), std::move(on_complete),
                       /*admit_from_task_grant=*/true);
  return request_id;
}

RequestId PageIntelligenceServiceImpl::SubmitTaskBrowserCommand(
    AuthorizedBrowserCommand command,
    ActionDispatcher::CompletionCallback on_complete) {
  const RequestId request_id = MintRequestId("req_");
  dispatcher_.DispatchBrowserCommand(request_id, std::move(command),
                                     std::move(on_complete),
                                     /*admit_from_task_grant=*/true);
  return request_id;
}

RequestId PageIntelligenceServiceImpl::SubmitBrowserCommand(
    AuthorizedBrowserCommand command) {
  const RequestId request_id = MintRequestId("req_");
  dispatcher_.DispatchBrowserCommand(
      request_id, std::move(command),
      base::BindOnce(
          [](base::WeakPtr<PageIntelligenceServiceImpl> self,
             ActionResult result) {
            if (self && self->sink_) {
              self->sink_->OnActionResult(std::move(result));
            }
          },
          weak_factory_.GetWeakPtr()));
  return request_id;
}

void PageIntelligenceServiceImpl::Cancel(RequestId request_id) {
  // Observations settle with a cancellation code; the renderer's late reply is
  // dropped when it arrives because the pending entry is gone.
  if (pending_observations_.contains(request_id)) {
    FinishObservationWithCode(request_id, ObservationResultCode::kCancelled);
  }
  negotiator_->CancelWithCode(request_id, ObservationResultCode::kCancelled);
  dispatcher_.Cancel(request_id);
}

bool PageIntelligenceServiceImpl::GrantNamesTheCommittedDocument(
    const ObservationPolicyGrant& grant,
    const FrameId& root_frame_id) const {
  // Fail closed on a grant that names nothing. The alternative reading — that
  // an unset field means "any document" — is the one that turns a mint site
  // somebody forgot to update into a silent hole, and it would be indefensible
  // in exactly the case the field exists for.
  if (!grant.document_origin.is_valid()) {
    return false;
  }
  content::RenderFrameHost* host = broker_->ResolveFrame(root_frame_id);
  if (!host) {
    return false;
  }
  // The committed origin, not the requested one and not the one the renderer
  // reports. This process is the only one entitled to answer "which site is
  // this", which is the same reason CollectBrowserOwnedFacts reads it here.
  //
  // OriginCodec::Matches rather than operator==, because the two are not the
  // same question for an opaque origin: a wire Origin carries a session-local
  // identifier that only the codec can resolve, and comparing the records
  // directly would ask whether two labels are spelled alike. Matches answers
  // no for an opaque origin it has evicted or never issued, which is the
  // direction to be wrong in. The production host refuses an opaque root
  // outright a step earlier, so this is the belt to that brace.
  return OriginCodec::Get().Matches(grant.document_origin,
                                    host->GetLastCommittedOrigin());
}

// --- negotiation ------------------------------------------------------------

RequestId PageIntelligenceServiceImpl::QueryProtocolSupport(TabId tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const RequestId request_id = MintRequestId("req_");

  if (tab_id != broker_->tab_id() || !web_contents_) {
    RefuseProtocolQuery(request_id, ObservationResultCode::kUnsupported);
    return request_id;
  }
  FrameObservationEndpoint* endpoint =
      broker_->GetOrCreateEndpoint(web_contents_->GetPrimaryMainFrame());
  if (!endpoint || !endpoint->remote().is_bound()) {
    RefuseProtocolQuery(request_id, ObservationResultCode::kUnsupported);
    return request_id;
  }
  negotiator_->Query(request_id, broker_->tab_id(), endpoint->frame_id(),
                     endpoint->remote(),
                     RendererDeadlineFor(ObservationBudget()));
  return request_id;
}

void PageIntelligenceServiceImpl::RefuseProtocolQuery(
    const RequestId& request_id,
    ObservationResultCode code) {
  if (sink_) {
    sink_->OnProtocolSupport(
        ProtocolNegotiator::BareEnvelope(request_id, broker_->tab_id(), code));
  }
}

// --- subscriptions ----------------------------------------------------------

RequestId PageIntelligenceServiceImpl::Subscribe(SubscriptionRequest request,
                                                 ObservationPolicyGrant grant) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const RequestId request_id = MintRequestId("req_");
  request.request_id = request_id;

  auto settle = [this, &request](ObservationResultCode code) {
    SubscriptionEnvelope envelope;
    envelope.schema_version = kBipSchemaVersion;
    envelope.request_id = request.request_id;
    envelope.code = code;
    envelope.tab_id = request.tab_id;
    envelope.frame_id = request.frame_id;
    envelope.observed_at_monotonic_ms = NowMonotonicMs();
    OnSubscriptionSettled(std::move(envelope));
  };

  if (!DeltasAllowedAt(memory_.stage())) {
    // Deltas are the first thing memory pressure takes away, so a subscription
    // request during pressure is refused rather than opened and immediately
    // stopped (protocol section 15).
    settle(ObservationResultCode::kResourcePressure);
    return request_id;
  }
  // The same document binding SubmitObservation applies, for the same reason
  // and with more at stake: a subscription outlives the call that opened it,
  // so a grant that was not checked against the committed document here would
  // go on delivering deltas from whatever document replaced it.
  //
  // `kStalePageEpoch` for the same reason the observation path answers it:
  // the document moved on between the grant and the request, which is a fresh
  // look rather than a thing this build cannot do (decision 0164).
  if (!GrantNamesTheCommittedDocument(grant, request.frame_id)) {
    LOG(WARNING) << "[taffy_subscription_refused] at=grant_document_moved";
    settle(ObservationResultCode::kStalePageEpoch);
    return request_id;
  }

  ProcessBudgetLimits endpoint_limits;
  if (const ProtocolSupportEnvelope* support =
          negotiator_->GetSupport(request.frame_id)) {
    endpoint_limits = support->effective_limits;
  }

  SubscriptionRequest clamped = ClampSubscriptionRequest(
      request, grant, GetProcessBudgetLimits(), endpoint_limits);
  clamped.request_id = request_id;
  clamped.task_id = request.task_id;
  clamped.tab_id = request.tab_id;
  deltas_.Subscribe(std::move(clamped));
  return request_id;
}

void PageIntelligenceServiceImpl::Unsubscribe(SubscriptionId subscription_id) {
  deltas_.Unsubscribe(subscription_id);
}

void PageIntelligenceServiceImpl::AcknowledgeDelta(
    SubscriptionId subscription_id,
    EventSequence event_sequence) {
  deltas_.Acknowledge(subscription_id, event_sequence);
}

void PageIntelligenceServiceImpl::OnSubscriptionSettled(
    SubscriptionEnvelope envelope) {
  if (sink_) {
    sink_->OnSubscriptionResult(std::move(envelope));
  }
}

void PageIntelligenceServiceImpl::OnDeltaReady(DeltaEnvelope delta) {
  // A verifier waiting on an observation-based postcondition can read the new
  // state now rather than at its next poll: protocol section 11.6 accepts a
  // delta observation as evidence, and this is where that becomes real.
  dispatcher_.OnDeltaObserved(delta.frame_id, delta.to_revision);
  if (sink_) {
    sink_->OnDelta(std::move(delta));
  }
}

void PageIntelligenceServiceImpl::OnBackpressureNotice(
    BackpressureNotice notice) {
  if (sink_) {
    sink_->OnBackpressure(std::move(notice));
  }
}

void PageIntelligenceServiceImpl::OnStreamInvalidated(
    InvalidationNotice notice) {
  if (sink_) {
    sink_->OnPageInvalidated(std::move(notice));
  }
}

// --- memory pressure --------------------------------------------------------

void PageIntelligenceServiceImpl::StopDeltaStreamsForMemoryPressure() {
  deltas_.StopAllForMemoryPressure();
}

void PageIntelligenceServiceImpl::RefuseObservationForMemoryPressure(
    const RequestId& request_id,
    const ObservationRequest& request) {
  if (!sink_) {
    return;
  }
  ObservationEnvelope envelope =
      BuildBareEnvelope(request_id, ObservationResultCode::kResourcePressure);
  // Pressure prevents extraction, not correlation. Read only an existing live
  // endpoint: a missing or retired document must not be rebound, and the
  // caller's expected epoch and revision floor are never observed facts.
  FrameObservationEndpoint* endpoint =
      broker_->GetActionableEndpoint(request.root_frame_id);
  if (endpoint && (!request.expected_page_epoch ||
                   *request.expected_page_epoch == endpoint->page_epoch())) {
    envelope.root_frame_id = endpoint->frame_id();
    envelope.page_epoch = endpoint->page_epoch();
    // This is the last revision the renderer reported, including zero before
    // its first report. No new snapshot, graph, or page text is produced.
    envelope.graph_revision = endpoint->last_reported_revision();
    envelope.scope = request.scope;
    envelope.form_root = request.form_root;
    envelope.media_root = request.media_root;
  }
  sink_->OnObservationResult(std::move(envelope));
}

void PageIntelligenceServiceImpl::OnDegradationStageChanged(
    DegradationStage stage) {
  // Nothing is cancelled here beyond the streams the governor already stopped.
  // An action in flight keeps its verifier: abandoning it would leave a side
  // effect nobody could reconcile, which costs more than the memory it saves.
  // Recorded so that a degraded result later in the session is explicable
  // rather than mysterious. Content free, like every record: the stage, and
  // which tab it applies to.
  ObservationRecord record;
  record.tab_id = ToRecordIdentifier(broker_->tab_id().value);
  record.code = ObservationsAllowedAt(stage)
                    ? ObservationResultCode::kOk
                    : ObservationResultCode::kResourcePressure;
  record.truncated = !OptionalAdaptersAllowedAt(stage);
  observability_.Record(record);
}

// --- leases and grants ------------------------------------------------------

ActorLeaseResult PageIntelligenceServiceImpl::IssueActorLease(
    ActorLeaseRequest request) {
  if (request.tab_id != broker_->tab_id()) {
    ActorLeaseResult result;
    result.code = ActorLeaseResultCode::kTabGone;
    return result;
  }
  return actor_leases_->Issue(request, base::TimeTicks::Now());
}

void PageIntelligenceServiceImpl::ReleaseActorLease(ActorLeaseId lease_id) {
  actor_leases_->Release(lease_id);
}

// --- broker notifications ---------------------------------------------------

void PageIntelligenceServiceImpl::OnPageInvalidated(
    const InvalidationNotice& notice) {
  // Navigation invalidation has priority over queued extraction and action
  // work (protocol section 6.3), so actions are cancelled first, then streams
  // are torn down, and only then is the isolated core told.
  const std::vector<ActorLeaseId> revoked =
      notice.retires_page_epoch
          ? actor_leases_->PreemptTab(broker_->tab_id())
          : actor_leases_->PreemptDirectObservations(broker_->tab_id());
  // Keep expected navigation verification separate from an explicit takeover.
  // Queued work loses its lease before either the dispatcher or core is told.
  dispatcher_.OnPageInvalidated(notice, revoked);
  deltas_.OnPageInvalidated(notice);

  std::vector<RequestId> stale;
  for (const auto& [request_id, pending] : pending_observations_) {
    if (pending.clamped.root_frame_id == notice.frame_id) {
      stale.push_back(request_id);
    }
  }
  for (const RequestId& request_id : stale) {
    FinishObservationWithCode(request_id,
                              ObservationResultCode::kStalePageEpoch);
  }

  if (notice.retires_page_epoch) {
    // Negotiation belongs to a document. Keeping a retired document's answer
    // would let a request name a required adapter the new document's endpoint
    // never claimed to have.
    negotiator_->ForgetFrame(notice.frame_id);
  }

  if (sink_) {
    sink_->OnPageInvalidated(notice);
  }
}

void PageIntelligenceServiceImpl::OnPersonCommittedInput(TabId tab_id) {
  // The only consumer of the classification. The registry ignores it unless a
  // handover window is open on this tab, so nothing accrues outside one.
  actor_leases_->NoteHandoverInput(tab_id);
}

void PageIntelligenceServiceImpl::OnUserPreemption(TabId tab_id) {
  // Revoke first, tell afterwards. Take over has to remove undispatched
  // mutation authority before anything else can run (protocol section 17.4).
  const std::vector<ActorLeaseId> revoked = actor_leases_->PreemptTab(tab_id);
  if (revoked.empty()) {
    return;
  }
  dispatcher_.OnActorLeasesPreempted(revoked);
  if (sink_) {
    for (const ActorLeaseId& lease_id : revoked) {
      sink_->OnActorLeasePreempted(lease_id, tab_id);
    }
  }
}

}  // namespace taffy
