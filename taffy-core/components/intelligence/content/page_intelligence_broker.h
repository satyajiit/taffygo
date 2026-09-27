// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PAGE_INTELLIGENCE_BROKER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PAGE_INTELLIGENCE_BROKER_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/process/kill.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
// VERIFY AT SP-01: content::FrameTreeNodeId became a strong type in its own
// header. If the pinned milestone still uses a bare int, replace the
// include and the two map key types below; nothing else changes.
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace blink {
class WebInputEvent;
}  // namespace blink

namespace content {
class NavigationHandle;
class Page;
class WebContents;
}  // namespace content

// The per-WebContents half of the BIP browser broker (spec section 4).
//
// It owns identity and lifetime, and nothing else. It assigns TabId, FrameId,
// PageEpoch and GraphRevision; it assembles the frame tree from Chromium's
// own topology rather than from anything a renderer said; and it invalidates
// on every event in the spec section 13 table. The action path lives in
// ActionDispatcher and the observation path in PageIntelligenceServiceImpl,
// both of which ask this class what is true.
//
// The identity split is deliberate:
//
//   TabId       one per WebContents, for the life of the session.
//   FrameId     one per frame tree node, so it survives a same-frame
//               navigation. That is what makes "this frame navigated, the
//               parent's assumptions did not change" expressible.
//   PageEpoch   one per document, held in FrameObservationEndpoint, which is
//               DocumentUserData, so the epoch dies with the document without
//               anyone remembering to kill it.
//   GraphRevision  monotonic inside one epoch.
//
// This class never trusts a renderer-reported id, URL or origin. A renderer
// echoes identity so the broker can reject a reply that belongs to a retired
// document; the echo is checked, never adopted.
//
// UI thread only.

namespace taffy {

class FrameObservationEndpoint;

// InvalidationCode used to be declared here. It moved to
// //taffy/common/public/bip_observation.h when the delta path landed: the
// reason travels to the isolated core on every InvalidationNotice, so a consumer
// switching on it needs the enumeration rather than an integer plus a private
// table of what the integers meant.

class PageIntelligenceBroker
    : public content::WebContentsObserver,
      public content::WebContentsUserData<PageIntelligenceBroker> {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Previously issued handles for this frame are dead. Delivered for every
    // row of the spec section 13 table, before any further work for the tab
    // is processed: navigation invalidation has priority over queued
    // extraction and action work (spec section 6.3).
    virtual void OnPageInvalidated(const InvalidationNotice& notice) {}

    // Direct user input reached a tab an assistant task holds a lease on, or
    // the tab is going away. Undispatched mutation authority must be revoked
    // synchronously, before this returns.
    virtual void OnUserPreemption(TabId tab_id) {}

    // One input event a person committed in this tab: a key press, or a
    // pointer or touch commit. Movement, wheel, scroll, hover, pinch and fling
    // are deliberately not reported — see ActorLeaseRegistry's handover
    // counter, which is the only thing that consumes this, for what the
    // classification is for and why it stops where it does.
    //
    // Reported after OnUserPreemption for the same event, because revoking
    // authority is the urgent half and counting is the recording half.
    virtual void OnPersonCommittedInput(TabId tab_id) {}

    // The broker is about to be destroyed. An observer whose lifetime is not
    // nested inside the broker must synchronously stop observing from this
    // callback; WebContentsUserData entries have deliberately unordered
    // destruction, so an observer cannot wait for its own destructor.
    virtual void OnBrokerDestroyed(TabId tab_id) = 0;
  };

  PageIntelligenceBroker(const PageIntelligenceBroker&) = delete;
  PageIntelligenceBroker& operator=(const PageIntelligenceBroker&) = delete;
  ~PageIntelligenceBroker() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const TabId& tab_id() const { return tab_id_; }

  // Identity for a live frame, allocating on first use. Returns an invalid
  // FrameId for a null or already-detached host.
  FrameId GetOrAssignFrameId(content::RenderFrameHost* host);

  // The reverse direction, and the only supported one: a caller hands back a
  // FrameId and the broker resolves it against Chromium's live frame tree.
  // Returns null when the frame is gone, which is the answer that turns into
  // ActionResultCode::kFrameGone.
  content::RenderFrameHost* ResolveFrame(const FrameId& frame_id) const;

  // The current, actionable endpoint for `frame_id`, or null. Null covers
  // every reason an endpoint cannot be used: the frame is gone, the document
  // is prerendering or in the back/forward cache, the renderer is not live,
  // or the broker retired it.
  FrameObservationEndpoint* GetActionableEndpoint(const FrameId& frame_id);

  // Ensures an endpoint exists for `host` and returns it, allocating a page
  // epoch if this document does not have one yet. Returns null when the
  // document is not eligible to be observed at all.
  FrameObservationEndpoint* GetOrCreateEndpoint(content::RenderFrameHost* host);

  // The same, reached through a frame identifier: resolve the frame against
  // Chromium's live tree, then ensure the current document has an endpoint.
  //
  // This is the accessor an observation uses, and the distinction from
  // GetActionableEndpoint is the whole of why both exist. DidFinishNavigation
  // deliberately does not allocate an epoch for a newly committed document —
  // "allocated on first use, so that a document nobody observes never consumes
  // an identifier" — which means *something* has to be the first use. An
  // observation is exactly that: it is the request that first asks a document
  // to describe itself. Resolving it through the non-allocating accessor made
  // every observation of a freshly committed document answer
  // kDocumentInactive, because no endpoint had been created yet and none ever
  // would be.
  //
  // It cannot revive anything. A document whose endpoint the broker already
  // retired still answers null here, because GetOrCreateEndpoint returns the
  // existing retired endpoint's is_actionable() verdict rather than replacing
  // it; a frame that is gone, prerendering, back/forward cached or whose
  // renderer is dead answers null for the same reasons it does there. The only
  // case that differs is "this document has never been observed", and that is
  // the case an observation is supposed to open.
  FrameObservationEndpoint* GetOrCreateActionableEndpoint(
      const FrameId& frame_id);

  // Allocates identity for every frame under `root_frame_id` that is eligible
  // to be described, so that BuildFrameTree can describe the whole subtree
  // rather than only the frames something happened to touch before.
  //
  // BuildFrameTree reads endpoints and never creates them - correctly, because
  // it is const and because inventing a frame is exactly what it exists to
  // prevent. The consequence was that a child frame nobody had observed
  // individually had no endpoint, so it was silently absent from the frame
  // list: an observation of a two-frame page reported one frame. That is worse
  // than reporting the child as excluded, because "not in the list" and "in the
  // list and not included" are different facts and only the second is true.
  //
  // Called when an observation may describe child frames. It allocates nothing
  // for a frame that is gone, prerendering, back/forward cached or whose
  // renderer is dead, and it never revives a retired endpoint - it is
  // GetOrCreateEndpoint applied across the subtree, with that method's rules.
  //
  // SECURITY-RELEVANT, and stated here rather than left to be rediscovered.
  // This runs before the grant is applied - ApplyFrameInclusion() decides
  // inclusion afterwards - and it deliberately allocates for frames the grant
  // will go on to exclude. The consequence is that an ungranted cross-origin
  // child now reaches the caller in `envelope.frames` carrying its origin,
  // page_epoch, frame_id, lifecycle_state and is_out_of_process, marked
  // `included = false`. Before this method existed it was absent from the
  // list entirely.
  //
  // Present-and-excluded is the contract the suite asserts:
  // FramesTest.CrossOriginChildIsExcludedUnlessGranted in
  // test/correctness/frames_and_oopif_browsertest.cc fails on either an
  // included child or an omitted one, and names why - "the first is a policy
  // failure, the second makes the page's shape unknowable". The grant is
  // still what decides whether the frame's *contents* are described; nothing
  // here widens that.
  //
  // The disclosure is nevertheless reachable now where it was not before, and
  // AGENTS.md requires a decision record or a protocol design review for a
  // change to origin scope. That record is OWED and not yet written. It has
  // to ratify one of two things: that an ungranted cross-origin child is
  // listed with its origin and marked excluded, or that the row is listed
  // with its identifying fields withheld. Until it exists, do not read the
  // present-and-excluded shape as settled policy.
  void EnsureFrameTreeIdentity(const FrameId& root_frame_id);

  // The browser-owned frame tree, assembled from Chromium's topology. Parent
  // links come from RenderFrameHost::GetParentOrOuterDocument, never from a
  // renderer's claim about its own tree (spec section 8.1).
  std::vector<FrameSummary> BuildFrameTree(const FrameId& root_frame_id) const;

  // Steps 1 to 4a of the stale-node algorithm: the tab, the frame, an active
  // document, the exact page epoch, the origin, and the revision floor.
  // Returns nullopt when the handle is live, or the terminal failure code
  // when it is not. Exposed so the dispatcher and the verifier share one
  // implementation instead of two that drift apart.
  //
  // Deliberately not a bool: every way a handle can be dead has its own
  // result code, and the isolated core's next legal move differs between them.
  std::optional<ActionResultCode> CheckHandleLiveness(
      const NodeHandle& handle,
      GraphRevision required_revision) const;

  // Records the newest graph revision the renderer has stated for a frame.
  // Both report paths run through here — a snapshot reply and a delta — so
  // there is one place that learns what the renderer's numbering has reached
  // and no way for the two to disagree about it.
  //
  // The browser does not advance a revision of its own. When it learns of a
  // mutation the renderer has not reported yet, it calls the endpoint's
  // RequireResnapshot() instead: that is a different fact and it is recorded
  // as one.
  void NoteReportedRevision(const FrameId& frame_id, GraphRevision revision);

  // Retires one frame's endpoint on the broker's own initiative — the last
  // entry in the protocol section 5.2 list, "an explicit broker invalidation".
  //
  // It exists because not every reason to stop trusting a handle is a
  // Chromium lifecycle event. A policy grant that was withdrawn, a task that
  // ended, an adapter that restarted, a stream that lost its place: each is a
  // reason the broker knows and Chromium does not, and each has to be able to
  // retire an epoch without waiting for a navigation. Returns false when there
  // was nothing actionable to retire.
  bool Invalidate(const FrameId& frame_id, InvalidationCode reason);

  // Retires every endpoint in the tab and notifies observers. Called on tab
  // close, renderer loss and profile teardown.
  void InvalidateAll(InvalidationCode reason);

  base::WeakPtr<PageIntelligenceBroker> GetWeakPtr();

  // content::WebContentsObserver:
  void RenderFrameCreated(content::RenderFrameHost* host) override;
  void RenderFrameDeleted(content::RenderFrameHost* host) override;
  void FrameDeleted(content::FrameTreeNodeId node_id) override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void RenderFrameHostStateChanged(
      content::RenderFrameHost* host,
      content::RenderFrameHost::LifecycleState old_state,
      content::RenderFrameHost::LifecycleState new_state) override;
  void DidRedirectNavigation(content::NavigationHandle* handle) override;
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void PrimaryPageChanged(content::Page& page) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<PageIntelligenceBroker>;

  explicit PageIntelligenceBroker(content::WebContents* web_contents);

  // Only an active document is observable. Prerendering documents may be
  // prepared internally but are never exposed as actionable, and a document in
  // the back/forward cache is inactive (protocol section 13). It is a member
  // rather than a file-local helper because both translation units of this
  // class ask the question, and two copies of a fail-closed predicate is one
  // copy too many.
  static bool IsObservableLifecycle(
      content::RenderFrameHost::LifecycleState state);

  // Retires the endpoint bound to `host`'s current document, if any, and
  // notifies observers with `reason` from the protocol section 13 table.
  void RetireDocument(content::RenderFrameHost* host,
                      InvalidationCode reason,
                      bool child_frames_only);

  // The back/forward cache restore half of the protocol section 13 row.
  //
  // It cannot go through RetireDocument, and the difference is the whole
  // defect that made this a separate method: the document that comes back is
  // the *same* document, its endpoint was already retired on the way into the
  // cache, and RetireDocument refuses to act on an endpoint that is not
  // actionable. So the restore emitted no notice at all, and — because the
  // endpoint is DocumentUserData and therefore survived the round trip —
  // GetOrCreateEndpoint answered nullptr for the rest of that document's
  // life. A page restored from the cache was permanently unobservable.
  void RestoreDocumentFromBackForwardCache(content::RenderFrameHost* host);

  void NotifyInvalidated(const FrameId& frame_id,
                         const PageEpoch& retired_epoch,
                         const PageEpoch& new_epoch,
                         InvalidationCode reason,
                         bool child_frames_only);

  const TabId tab_id_;

  // Frame identity keyed by frame tree node, so it survives a same-frame
  // navigation — and a renderer crash, a process swap and every other event
  // that replaces a RenderFrameHost without removing the frame. Entries are
  // erased in FrameDeleted() and nowhere else, because that is Chromium's
  // signal that the node itself is gone.
  std::map<content::FrameTreeNodeId, FrameId> frame_ids_;
  std::map<FrameId, content::FrameTreeNodeId> frame_nodes_;

  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<PageIntelligenceBroker> weak_factory_{this};

  // RESOLVED AT SP-01: the milestone still declares both macros, so the pair is
  // required. WebContentsUserData::UserDataKey() returns `&T::kUserDataKey`
  // (content/public/browser/web_contents_user_data.h:88), the _DECL macro is
  // what declares that member, and the _IMPL macro in the .cc defines it.
  // Without this line the .cc fails with "no member named 'kUserDataKey'".
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PAGE_INTELLIGENCE_BROKER_H_
