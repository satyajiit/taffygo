// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PAGE_INTELLIGENCE_SERVICE_IMPL_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PAGE_INTELLIGENCE_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "taffy/common/public/page_intelligence_service.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "taffy/components/intelligence/content/delta_subscription_manager.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/components/intelligence/content/memory_pressure_governor.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/observation_result_builder.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/components/intelligence/content/protocol_negotiator.h"
#include "taffy/components/intelligence/content/renderer_call_deadline.h"
#include "taffy/components/security/browser/action_authority.h"

namespace content {
class WebContents;
}  // namespace content

// The per-tab implementation of the trusted API used by the browser-side
// Core Service observation broker.
//
// One instance per WebContents, created alongside the broker. It is a router,
// and it is worth naming what it is not: it does not decide policy, it does not
// parse a semantic graph, and it does not authorize anything. It clamps, it
// bounds, it validates a renderer reply against browser-owned facts, and it
// guarantees the one-terminal-result contract.
//
// Four collaborators do the work it routes to:
//
//   ActionDispatcher            the stale-node algorithm and the verifier.
//   DeltaSubscriptionManager    the delta path and backpressure.
//   MemoryPressureGovernor      what gets given up first when memory is short.
//   ObservationResultBuilder    turning one reply into one envelope.
//
// Two guarantees are this class's own, because nothing else is positioned to
// make them. Every request that leaves the browser process carries a deadline,
// so a renderer that never answers still produces a terminal result. And every
// request produces exactly one terminal result, whether it was answered,
// timed out, cancelled, invalidated by navigation, or refused before it left.
//
// UI thread only.

namespace taffy {

class PageIntelligenceServiceImpl : public PageIntelligenceService,
                                    public PageIntelligenceBroker::Observer,
                                    public DeltaSubscriptionManager::Delegate,
                                    public MemoryPressureGovernor::Delegate {
 public:
  PageIntelligenceServiceImpl(content::WebContents* web_contents,
                              PageIntelligenceBroker* broker,
                              PageIntelligenceResultSink* sink,
                              TaskJournalSink* journal,
                              ObservabilitySink* observability_sink,
                              ActorLeaseRegistry* actor_leases,
                              CapabilityLedger* capabilities);
  PageIntelligenceServiceImpl(const PageIntelligenceServiceImpl&) = delete;
  PageIntelligenceServiceImpl& operator=(const PageIntelligenceServiceImpl&) =
      delete;
  ~PageIntelligenceServiceImpl() override;

  // Installs the encoder generated from taffy-core/contracts/bip. Until it is
  // installed, observations report GraphPayloadEncoding::kNone and carry
  // metadata only, and Subscribe is refused as unsupported rather than opening
  // a stream that could only deliver empty payloads.
  void SetGraphPayloadEncoder(std::unique_ptr<GraphPayloadEncoder> encoder);
  void SetBrowserActionDelegate(BrowserActionDelegate* delegate);
  void SetObservedLinkResolver(ObservedLinkResolver* resolver);
  void SetBrowserEffectSource(BrowserEffectSource* source);
  // The profile's holder of values a person entered into Taffy's own controls
  // (value_reference_vault.h). Optional, and a dispatcher without one refuses
  // every envelope that names a value.
  void SetValueReferenceVault(ValueReferenceVault* vault);

  // The browser's own re-read of one node, forwarded from the dispatcher.
  //
  // It is exposed because the dispatcher is private to this class and there
  // is exactly one other browser-process caller that has to ask a node what
  // it is without dispatching anything at it: the field-value coordinator,
  // which decides from a field's *re-read* classification whether the browser
  // may hold bytes for it (decision 0088). Routing that through the same
  // ResolveNode call the dispatch path uses is the point — a second way to
  // learn a field's class would be a second answer.
  void ResolveNodeFacts(
      const NodeHandle& handle,
      base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved);

  DegradationStage degradation_stage() const { return memory_.stage(); }

  // PageIntelligenceService:
  RequestId QueryProtocolSupport(TabId tab_id) override;
  RequestId SubmitObservation(ObservationRequest request,
                              ObservationPolicyGrant grant) override;
  RequestId SubmitAction(AuthorizedActionEnvelope envelope) override;
  RequestId SubmitBrowserCommand(AuthorizedBrowserCommand command) override;
  // The task-path sibling of `SubmitAction`. Policy signed a reducer proposal
  // digest; `admit_from_task_grant` tells the dispatcher to match that grant
  // rather than a BIP envelope digest computed after the document was bound.
  RequestId SubmitTaskAction(AuthorizedActionEnvelope envelope,
                             ActionDispatcher::CompletionCallback on_complete);
  RequestId SubmitTaskBrowserCommand(
      AuthorizedBrowserCommand command,
      ActionDispatcher::CompletionCallback on_complete);
  RequestId Subscribe(SubscriptionRequest request,
                      ObservationPolicyGrant grant) override;
  void Unsubscribe(SubscriptionId subscription_id) override;
  void AcknowledgeDelta(SubscriptionId subscription_id,
                        EventSequence event_sequence) override;
  void Cancel(RequestId request_id) override;
  ActorLeaseResult IssueActorLease(ActorLeaseRequest request) override;
  void ReleaseActorLease(ActorLeaseId lease_id) override;

  // PageIntelligenceBroker::Observer:
  void OnPageInvalidated(const InvalidationNotice& notice) override;
  void OnUserPreemption(TabId tab_id) override;
  void OnPersonCommittedInput(TabId tab_id) override;
  void OnBrokerDestroyed(TabId tab_id) override;

  // DeltaSubscriptionManager::Delegate:
  void OnSubscriptionSettled(SubscriptionEnvelope envelope) override;
  void OnDeltaReady(DeltaEnvelope delta) override;
  void OnBackpressureNotice(BackpressureNotice notice) override;
  void OnStreamInvalidated(InvalidationNotice notice) override;

  // MemoryPressureGovernor::Delegate:
  void StopDeltaStreamsForMemoryPressure() override;
  void OnDegradationStageChanged(DegradationStage stage) override;

 private:
  struct PendingObservation {
    PendingObservation();
    PendingObservation(PendingObservation&&);
    ~PendingObservation();

    RequestId request_id;
    ObservationAuthoritySubject authority_subject;
    ObservationRequest clamped;
    ObservationPolicyGrant grant;
    PageEpoch expected_epoch;
    base::TimeTicks started_at;
    // Held here, and not only inside the reply callback, because a pipe
    // disconnect destroys the callback: if the guard died with it, the timer
    // would go too and the request would never settle.
    scoped_refptr<RendererCallDeadline> deadline;
  };

  RequestId MintRequestId(const char* prefix);
  base::TimeDelta RendererDeadlineFor(const ObservationBudget& budget) const;

  void OnSnapshotResult(RequestId request_id,
                        FrameId frame_id,
                        mojom::SnapshotResultPtr result);

  // Gathers what Chromium owns about the observed document, so the builder can
  // overwrite whatever the renderer echoed.
  BrowserOwnedObservationFacts CollectBrowserOwnedFacts(
      const ObservationRequest& clamped) const;
  FrameInclusionInputs BuildFrameInclusionInputs(
      const ObservationRequest& clamped,
      const ObservationPolicyGrant& grant,
      const Origin& root_origin) const;

  // Whether the grant was decided against the document that is committed in
  // `root_frame_id` right now. False when the frame is gone, when the grant
  // names no document, and when it names a different one.
  //
  // Read `grant.document_origin`'s own comment for why this is a separate
  // field from the allowed-origin list rather than a lookup in it.
  bool GrantNamesTheCommittedDocument(const ObservationPolicyGrant& grant,
                                      const FrameId& root_frame_id) const;

  ObservationEnvelope BuildBareEnvelope(const RequestId& request_id,
                                        ObservationResultCode code) const;

  // A refusal decided before the request left this process. It never had a
  // pending entry, so it cannot collide with a reply or a deadline.
  //
  // `at` is a compiled-in name for the branch that refused, logged beside the
  // code. Seven branches here answer three codes between them, so the code
  // alone does not say which rule was applied — and a read refused
  // `kUnsupported` reads as "this build cannot do that" whichever of them it
  // came from (decision 0136 section 1).
  void RefuseObservation(const RequestId& request_id,
                         ObservationResultCode code,
                         const char* at);
  void RefuseObservationForMemoryPressure(const RequestId& request_id,
                                          const ObservationRequest& request);
  void RefuseProtocolQuery(const RequestId& request_id,
                           ObservationResultCode code);

  // Idempotent per request id: settles a request that is still in flight.
  void FinishObservation(RequestId request_id, ObservationEnvelope envelope);
  void FinishObservationWithCode(RequestId request_id,
                                 ObservationResultCode code);

  void RecordObservationOutcome(const PendingObservation& pending,
                                const ObservationEnvelope& envelope);
  void TearDownTabAuthority();

  const raw_ptr<content::WebContents> web_contents_;
  raw_ptr<PageIntelligenceBroker> broker_;
  const raw_ptr<PageIntelligenceResultSink> sink_;

  // Copied from the broker while both objects are alive. The service and
  // broker are separate WebContentsUserData ownership trees, so destruction
  // must never need to dereference the broker to recover this identity.
  const TabId tab_id_;

  // Profile-owned authority. These outlive every WebContents in the profile;
  // a movable tab therefore keeps the same lease and spend ledger when only
  // its window changes. No per-WebContents registry exists in production.
  const raw_ptr<ActorLeaseRegistry> actor_leases_;
  const raw_ptr<CapabilityLedger> capabilities_;
  ObservabilityRecorder observability_;
  ActionDispatcher dispatcher_;
  std::unique_ptr<GraphPayloadEncoder> encoder_;
  DeltaSubscriptionManager deltas_;
  MemoryPressureGovernor memory_;
  // Constructed in the constructor body rather than in the initializer list:
  // it needs a weak pointer, and weak_factory_ is deliberately the last member,
  // so it does not exist yet while the list is running.
  std::unique_ptr<ProtocolNegotiator> negotiator_;

  std::map<RequestId, PendingObservation> pending_observations_;
  uint64_t next_request_value_ = 1;
  bool tab_authority_torn_down_ = false;

  base::ScopedObservation<PageIntelligenceBroker,
                          PageIntelligenceBroker::Observer>
      broker_observation_{this};

  base::WeakPtrFactory<PageIntelligenceServiceImpl> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PAGE_INTELLIGENCE_SERVICE_IMPL_H_
