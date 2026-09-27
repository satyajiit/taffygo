// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_PAGE_INTELLIGENCE_ENDPOINT_H_
#define TAFFY_RENDERER_PAGE_INTELLIGENCE_ENDPOINT_H_

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/delta_publisher.h"
#include "taffy/renderer/dom_mutation_signal_source.h"
#include "taffy/renderer/renderer_action_executor.h"
#include "taffy/renderer/semantic_graph_store.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// The renderer half of BIP: one per document, running inside the sandbox and
// trusted for nothing.
//
// The things this class is not allowed to do are more interesting than the
// things it does:
//
//   * It does not decide what is allowed. Every command it receives has
//     already had its capability consumed in the browser process (protocol
//     section 11.3). The command carries no capability, no approval receipt,
//     and no task identity, so there is nothing here for a compromised
//     renderer to replay or widen.
//   * It does not mint identity. TabId, FrameId, PageEpoch and GraphRevision
//     are broker-assigned. This class echoes the first three and reports the
//     fourth from its own store so the broker can detect a reply that belongs
//     to a document it has retired.
//   * It cannot report success. mojom::RendererActionOutcome has no member
//     meaning "the effect happened"; the strongest thing this class can say
//     is kDispatched (protocol section 11.6).
//   * It cannot produce user-facing text. Warnings are stable codes with
//     bounded arguments, and the strings a user sees are built from local
//     templates in the browser (protocol section 11.7).
//
// What it delegates, and why: building a snapshot, publishing deltas, and
// decoding an action command each have their own module. Each is the kind of
// code a reviewer reads line by line, and none of them was readable while it
// was interleaved with the other two.
//
// Threading: everything here runs on the main renderer thread, on the frame's
// sequence. There is no lock, and none is wanted: a second thread touching
// Blink would be a bug long before it was a race.
class PageIntelligenceEndpoint : public mojom::PageIntelligence {
 public:
  // The page epoch is NOT a constructor argument. Epochs are broker-assigned
  // (protocol section 5.2) and a renderer only ever echoes one back, so this
  // endpoint has no epoch until a request tells it one, and it observes
  // nothing before that. The browser states it on every request, first bind
  // included (README.md item 1, settled 2026-08-20); a request that names no
  // epoch is still refused here, because this endpoint cannot allocate a node
  // id namespace without one and must not invent it.
  PageIntelligenceEndpoint(blink::WebLocalFrame* frame, FrameId frame_id);
  PageIntelligenceEndpoint(const PageIntelligenceEndpoint&) = delete;
  PageIntelligenceEndpoint& operator=(const PageIntelligenceEndpoint&) = delete;
  ~PageIntelligenceEndpoint() override;

  void Bind(mojo::PendingAssociatedReceiver<mojom::PageIntelligence> receiver);

  // mojom::PageIntelligence:
  void GetProtocolInfo(GetProtocolInfoCallback callback) override;
  void GetSnapshot(mojom::SnapshotRequestPtr request,
                   GetSnapshotCallback callback) override;
  void Subscribe(mojom::PageSubscriptionOptionsPtr options,
                 mojo::PendingRemote<mojom::PageDeltaClient> client,
                 SubscribeCallback callback) override;
  void ResolveNode(mojom::ResolveNodeRequestPtr request,
                   ResolveNodeCallback callback) override;
  void InspectMediaTarget(mojom::MediaTargetRequestPtr request,
                          InspectMediaTargetCallback callback) override;
  void ExecuteRendererAction(mojom::RendererActionCommandPtr command,
                             ExecuteRendererActionCallback callback) override;
  void Cancel(const std::string& command_id) override;

  // Called by the RenderFrameObserver when Blink tells us something changed.
  // Each maps to a protocol section 5.3 change class.
  void OnDocumentMutated(SemanticGraphStore::ChangeClass change,
                         SemanticNodeId node_id);
  void OnNodesRemoved(SemanticGraphStore::IdentitySpace space,
                      int64_t source_node_id);
  void OnLifecycleChanged(mojom::DocumentLifecycleState state);

  // Blink has finished parsing this document. A snapshot that was waiting for
  // it runs now — see `DocumentIsStillArriving`.
  void OnDocumentParsed();

  // Drops every subscription and marks the store unusable. Called on
  // navigation commit, frame detach, and back/forward cache entry. Handles
  // issued before this point never resolve again, because the store dies with
  // the epoch.
  void Invalidate(mojom::InvalidationReason reason);

  SemanticGraphStore* store_for_testing() { return store_.get(); }

 private:
  // The whole of GetSnapshot, once it is known whether this request may wait
  // for the document. A request that has already waited passes false, so the
  // wait below can happen at most once per request.
  void BeginSnapshot(mojom::SnapshotRequestPtr request,
                     GetSnapshotCallback callback,
                     bool may_wait);

  // Whether there is not yet a document here to describe.
  //
  // True while the parser is still running and has produced no body. A read
  // that lands in that window is a read of a page that has not arrived, not
  // of a page this build cannot describe, and answering it immediately says
  // the second: the DOM adapter is REQUIRED for a whole-document read and it
  // reports unsupported when `document.Body()` is null, so the whole
  // observation comes back kUnsupported, whose recovery is do-not-retry.
  bool DocumentIsStillArriving() const;

  // Runs the held request, whether the parser finished or the wait ran out.
  void RunDeferredSnapshot();

  // Returns the store for `epoch`, creating it on first bind. Returns null
  // when this endpoint is already bound to a different epoch, which is a
  // refusal: a document does not get to change identity underneath a caller
  // that is still holding handles from the old one.
  SemanticGraphStore* StoreFor(PageEpoch epoch);

  // Everything the browser stated about this observation, collected in one
  // place so the adapters can be handed browser fact rather than renderer
  // guess (protocol sections 7.2 and 7.6).
  BrowserSuppliedFacts FactsFrom(const mojom::SnapshotRequest& request) const;
  BrowserSuppliedFacts CurrentFacts() const;

  // The lifecycle as an adapter-visible fact: the browser's statement when
  // there is one, and nothing when there is not. Written once so the two
  // fact builders cannot disagree about what "not told yet" looks like.
  std::optional<DocumentLifecycle> LifecycleFact() const;

  // The small, precondition-relevant projection of a node: the only part of a
  // renderer reply that browser-process C++ inspects field by field.
  mojom::ResolvedNodePtr BuildResolvedNode(
      const SemanticGraphStore::LiveNode& node,
      GraphRevision revision,
      bool refresh_live_checked_state) const;

  const raw_ptr<blink::WebLocalFrame> frame_;
  const FrameId frame_id_;
  std::unique_ptr<SemanticGraphStore> store_;
  // Created with the store and dies with it. Not with a subscription: the
  // three RenderFrameObserver signals this sits beside are always on, the
  // graph revision they advance is what an action precondition is checked
  // against, and a revision that only moved while somebody happened to be
  // subscribed would be a different number depending on who was watching.
  std::unique_ptr<DomMutationSignalSource> dom_mutations_;
  std::unique_ptr<DeltaPublisher> delta_publisher_;
  RendererActionExecutor action_executor_;

  mojo::AssociatedReceiver<mojom::PageIntelligence> receiver_{this};
  // Echoed into every message this endpoint sends. The renderer states no
  // protocol version of its own except in ProtocolInfo: everywhere else it
  // repeats what it was told, which is both simpler and the right trust
  // posture.
  std::string negotiated_schema_version_;
  // Echoed so the broker can reject a reply that belongs to a tab it has
  // retired. Never trusted, never minted here.
  std::string tab_id_;
  // The browser's identity for this document, as stated in the last snapshot
  // request or subscription. Empty until the browser has said one. Distinct
  // from `frame_id_` below, which is this process's own and never leaves it.
  std::string browser_frame_id_;
  // Whether the browser addressed this endpoint as something other than the
  // root of the observation. Established when a request or a subscription
  // arrives and carried onto the delta path, so that both describe the same
  // document at the same sensitivity floor.
  bool cross_origin_frame_ = false;
  std::string sensitivity_policy_id_;
  std::string task_purpose_;
  std::vector<std::string> allowed_origin_serializations_;

  // Strictly increasing per epoch (protocol section 6.3). Never reset while
  // the epoch lives; a gap is what tells the broker to invalidate. Exactly
  // one of these exists, which is why DeltaPublisher is handed a pointer to
  // it rather than keeping its own.
  uint64_t event_sequence_ = 0;

  // Separate from the event sequence: a snapshot id and a subscription id are
  // names, not positions in a stream, and sharing a counter would make a gap
  // in one look like a gap in the other.
  uint64_t next_local_id_ = 0;

  // Empty until the browser states a lifecycle for this document. Comparing
  // an empty optional against kActive is false, which is the behaviour every
  // guard below needs: an endpoint that has not been told is an endpoint that
  // refuses, and it never has to name a state it was not given.
  std::optional<mojom::DocumentLifecycleState> lifecycle_state_;
  bool invalidated_ = false;

  // One snapshot may wait for the document, and only one. A second request
  // arriving during that wait is answered from the page as it stands, because
  // a queue here would be a second place a reply can be delayed and the
  // caller has its own deadline for exactly one.
  mojom::SnapshotRequestPtr deferred_snapshot_;
  GetSnapshotCallback deferred_snapshot_callback_;
  base::OneShotTimer deferred_snapshot_timer_;

  // Commands cancelled by the browser. A late reply for one of these is
  // dropped and counted rather than delivered (protocol section 6.3).
  // Cancellation order matters only for bounded eviction. Removing the
  // oldest entry must not shift every newer command during a cancellation
  // burst.
  std::deque<std::string> cancelled_commands_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<PageIntelligenceEndpoint> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_PAGE_INTELLIGENCE_ENDPOINT_H_
