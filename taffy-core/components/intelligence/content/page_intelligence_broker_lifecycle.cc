// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The broker's WebContentsObserver half: one function per row of the protocol
// section 13 invalidation table, and nothing else.
//
// The split from page_intelligence_broker.cc follows what the two halves are
// for, not a line count. This file is where Chromium tells the broker that
// something happened; the sibling is what the broker knows and hands out. A
// reviewer checking that every row of the table is covered reads only this
// file, and a reviewer checking that a handle cannot be revived reads only the
// other.

#include "taffy/components/intelligence/content/page_intelligence_broker.h"

#include "base/check_op.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace {

// Whether this event is one a person committed, rather than one they produced
// on the way to committing something.
//
// The allowlist is closed and the default is "no". A key press and a pointer
// or touch commit put information into the page; movement, wheel, scroll,
// hover, pinch and fling do not, and a page that scrolls itself would
// otherwise manufacture evidence that somebody was there. The one consumer is
// ActorLeaseRegistry's handover counter, whose declaration carries the whole
// of the reasoning, including why a key press and a tap are counted into one
// number that stops at three.
//
// Chromium's WebInputEvent::Type is not TaffyGo's, so this reads it and
// answers in TaffyGo's terms rather than passing it on. Nothing above this
// line sees a blink type.
bool IsCommittedPersonInput(blink::WebInputEvent::Type type) {
  switch (type) {
    case blink::WebInputEvent::Type::kRawKeyDown:
    case blink::WebInputEvent::Type::kKeyDown:
    case blink::WebInputEvent::Type::kChar:
    case blink::WebInputEvent::Type::kMouseDown:
    case blink::WebInputEvent::Type::kTouchStart:
    case blink::WebInputEvent::Type::kGestureTap:
      return true;
    default:
      // Everything else, including every type a later Chromium adds. A default
      // of "no" can only under-report that a person acted, and under-reporting
      // is the safe direction: the count is evidence, and evidence that has to
      // be earned is worth more than evidence that accrues.
      return false;
  }
}

// Which row of the protocol section 13 table a frame deletion belongs to.
//
// RenderFrameDeleted is Chromium's signal for two events the invalidation
// table keeps apart. One is an orderly teardown - a subframe detached, a
// RenderFrameHost swapped out - where the renderer process is still running
// and the endpoint simply lost its other end. The other is the renderer dying
// underneath the frame, which is its own row and its own next legal move for
// the subscriber.
//
// The process itself is what tells them apart, and it is already marked dead
// by the time this runs: RenderProcessHostImpl::ProcessDied sets its dead flag
// before it notifies observers, and it is that notification which reaches
// RenderFrameHostImpl::RenderProcessGone and, from there, RenderFrameDeleted.
// So a crash arrives here with IsInitializedAndNotDead() already false, and an
// orderly teardown arrives with a live process.
//
// Deciding it here rather than leaving it to PrimaryMainFrameRenderProcessGone
// is what makes the reason independent of dispatch order. Both callbacks fire
// on a main-frame crash, whichever runs first retires the endpoint, and the
// other then finds nothing actionable and says nothing. If only the process
// -gone callback knew the reason, the notice would name a crash or name a
// disconnect depending on the order Chromium happened to walk its observers.
InvalidationCode ReasonForFrameDeletion(content::RenderFrameHost* host) {
  content::RenderProcessHost* process = host ? host->GetProcess() : nullptr;
  if (process && !process->IsInitializedAndNotDead()) {
    return InvalidationCode::kRendererCrashed;
  }
  return InvalidationCode::kEndpointDisconnected;
}

}  // namespace

// --- protocol section 13, row by row ----------------------------------------

void PageIntelligenceBroker::RenderFrameCreated(
    content::RenderFrameHost* host) {
  // Identity is assigned eagerly so that an invalidation raised before the
  // first observation still names a frame the isolated core can recognize.
  GetOrAssignFrameId(host);
}

void PageIntelligenceBroker::RenderFrameDeleted(
    content::RenderFrameHost* host) {
  // Rows: renderer crash or process swap, and frame detach. Invalidate the
  // endpoint and every unresolved result.
  //
  // The document dies here; the frame does not. This method is called when a
  // *RenderFrameHost* goes away or its renderer process dies, which is not the
  // same event as the frame leaving the tree: a crashed main frame, a process
  // swap and a RenderDocument-style same-frame navigation all delete a
  // RenderFrameHost while the frame tree node it belonged to stays exactly
  // where it was. Frame identity is keyed by that node precisely so it
  // survives those events, so nothing is erased here — see FrameDeleted below,
  // which is Chromium's "the node is gone" signal and the only place the
  // mapping may be dropped.
  RetireDocument(host, ReasonForFrameDeletion(host),
                 /*child_frames_only=*/false);
}

void PageIntelligenceBroker::FrameDeleted(content::FrameTreeNodeId node_id) {
  // The frame itself left the tree: a subframe was detached, or a frame tree
  // was destroyed. Only now is the FrameId unresolvable, and only now may it
  // be forgotten.
  //
  // Erasing on RenderFrameDeleted instead was a defect with a visible symptom.
  // A renderer crash deletes the main frame's RenderFrameHost and keeps the
  // node, so the pre-crash FrameId stopped resolving; after the reload,
  // RenderFrameCreated minted a second identifier for the same node and the
  // first named nothing at all. An observation carrying a handle from before
  // the crash was then refused kDocumentInactive — a true statement about a
  // frame the broker could no longer find, and the wrong one about a reloaded
  // document that is live, primary and observable. What is stale there is the
  // epoch, and kStalePageEpoch is the refusal that tells a caller its next
  // legal move is to bind a new epoch rather than to wait for the document to
  // become active. Two distinct facts had collapsed into one code.
  //
  // Frame identity outliving the document is not a way for a handle to come
  // back to life: NodeHandle carries the page epoch, the epoch is
  // DocumentUserData and dies with the document, and CheckHandleLiveness
  // compares it exactly. A resolvable frame with a fresh epoch is the shape
  // the stale-node algorithm is written for.
  auto node_it = frame_ids_.find(node_id);
  if (node_it == frame_ids_.end()) {
    return;
  }
  frame_nodes_.erase(node_it->second);
  frame_ids_.erase(node_it);
}

void PageIntelligenceBroker::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  // Row: renderer replacement that cannot preserve observation identity.
  RetireDocument(old_host, InvalidationCode::kCrossDocumentCommit,
                 /*child_frames_only=*/false);
  GetOrAssignFrameId(new_host);
}

void PageIntelligenceBroker::RenderFrameHostStateChanged(
    content::RenderFrameHost* host,
    content::RenderFrameHost::LifecycleState old_state,
    content::RenderFrameHost::LifecycleState new_state) {
  // Row: back/forward cache entry. Mark inactive, stop deltas, cancel queued
  // actions. Retiring the endpoint does all three: a retired endpoint drops its
  // remote, which terminates in-flight calls, and observers cancel on the
  // invalidation.
  if (new_state ==
      content::RenderFrameHost::LifecycleState::kInBackForwardCache) {
    RetireDocument(host, InvalidationCode::kBfcacheEntered,
                   /*child_frames_only=*/false);
    return;
  }

  // Row: prerender activation. Nothing was actionable while prerendering, so
  // activation binds a fresh endpoint rather than promoting an old one. The new
  // epoch is allocated lazily by GetOrCreateEndpoint.
  if (old_state == content::RenderFrameHost::LifecycleState::kPrerendering &&
      new_state == content::RenderFrameHost::LifecycleState::kActive) {
    RetireDocument(host, InvalidationCode::kPrerenderActivation,
                   /*child_frames_only=*/false);
  }
}

void PageIntelligenceBroker::DidRedirectNavigation(
    content::NavigationHandle* handle) {
  // Row: redirect. The browser records each transition; the final origin
  // controls the new epoch. Nothing is invalidated here because nothing has
  // committed yet — the record exists so that a capability carrying an allowed
  // redirect policy can be evaluated against what actually happened rather
  // than against the destination the isolated core expected.
  if (!handle) {
    return;
  }
  for (const GURL& url : handle->GetRedirectChain()) {
    OriginCodec::Get().ToWireOrigin(url::Origin::Create(url));
  }
}

void PageIntelligenceBroker::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle || !handle->HasCommitted()) {
    return;
  }
  content::RenderFrameHost* host = handle->GetRenderFrameHost();
  if (!host) {
    return;
  }

  // Row: same-document mutation and History API route change. The document
  // survives, so the epoch survives; previously observed nodes must be
  // revalidated before use. Handing back the same epoch and requiring a fresh
  // observation is the reason revisions exist at all.
  //
  // The browser records that the document moved rather than inventing a
  // revision to express it. It has not seen the new graph and cannot number
  // it, and a number it made up would be compared against the renderer's — the
  // exact confusion this stopped being.
  if (handle->IsSameDocument()) {
    auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host);
    if (!endpoint || !endpoint->is_actionable()) {
      return;
    }
    endpoint->RequireResnapshot();

    // A same-document navigation can still change the security origin, and an
    // origin transition retires the epoch outright (protocol section 5.2).
    if (!OriginCodec::Get().Matches(endpoint->origin(),
                                    host->GetLastCommittedOrigin())) {
      RetireDocument(host, InvalidationCode::kOriginChanged,
                     /*child_frames_only=*/false);
      return;
    }

    NotifyInvalidated(endpoint->frame_id(), PageEpoch(), endpoint->page_epoch(),
                      InvalidationCode::kHistoryRouteChange,
                      /*child_frames_only=*/false);
    return;
  }

  // Row: back/forward cache restore. A new epoch is allocated, which is the
  // conservative reading of [Open (OD-029)]: safety and implementation
  // simplicity take precedence over reusing node handles. If the validation
  // spike proves that restoring the same document plus a mandatory new graph
  // revision is equally safe, this is the single place that changes.
  //
  // It has its own method because the restored document is the same document:
  // the generic retire below would find an endpoint that is already retired,
  // do nothing, and leave that retired endpoint attached to the document for
  // the rest of its life. See RestoreDocumentFromBackForwardCache.
  if (handle->IsServedFromBackForwardCache()) {
    RestoreDocumentFromBackForwardCache(host);
    return;
  }

  InvalidationCode reason = InvalidationCode::kCrossDocumentCommit;
  bool child_frames_only = false;
  if (handle->IsPrerenderedPageActivation()) {
    reason = InvalidationCode::kPrerenderActivation;
  } else if (!handle->IsInPrimaryMainFrame()) {
    // Row: child-frame navigation. Only that frame's epoch is invalidated. A
    // parent handle survives, because the parent's own assumptions have not
    // changed; whether the task's assumptions survive is the isolated core's call, and
    // it is told through the notice.
    reason = InvalidationCode::kChildFrameNavigation;
    child_frames_only = true;
  }

  RetireDocument(host, reason, child_frames_only);
  // The new document's epoch is allocated on first use rather than here, so
  // that a document nobody observes never consumes an identifier. "First use"
  // includes an observation of an ancestor that may describe child frames -
  // see PageIntelligenceBroker::EnsureFrameTreeIdentity - so a child frame no
  // caller named individually does consume one. Nothing is allocated on this
  // path either way.
}

void PageIntelligenceBroker::PrimaryPageChanged(content::Page& page) {
  // Defence in depth for the cross-document row: if a commit path ever fails to
  // reach DidFinishNavigation for the outgoing document, this still retires
  // anything left actionable outside the new primary page.
  if (!web_contents()) {
    return;
  }
  content::RenderFrameHost* new_main = &page.GetMainDocument();
  web_contents()->ForEachRenderFrameHost(
      [this, new_main](content::RenderFrameHost* host) {
        if (host == new_main) {
          return;
        }
        if (!IsObservableLifecycle(host->GetLifecycleState())) {
          RetireDocument(host, InvalidationCode::kCrossDocumentCommit,
                         /*child_frames_only=*/false);
        }
      });
}

void PageIntelligenceBroker::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  // Row: renderer crash. Every handle in the tab dies, and unresolved results
  // become terminal. A consequential action in flight is never replayed
  // automatically: the dispatcher reports kRendererCrashed or kOutcomeUnknown,
  // and reconciliation is a decision above this layer.
  InvalidateAll(InvalidationCode::kRendererCrashed);
}

void PageIntelligenceBroker::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  // The user acted in this tab. Taking over preempts the assistant's mutation
  // authority synchronously (protocol section 2), so this is a direct call into
  // the observers rather than a posted task.
  for (Observer& observer : observers_) {
    observer.OnUserPreemption(tab_id_);
  }
  if (!IsCommittedPersonInput(event.GetType())) {
    return;
  }
  for (Observer& observer : observers_) {
    observer.OnPersonCommittedInput(tab_id_);
  }
}

void PageIntelligenceBroker::WebContentsDestroyed() {
  // Row: tab close and profile teardown. Invalidate everything and dispose
  // ephemeral observations.
  InvalidateAll(InvalidationCode::kTabClosed);
  Observe(nullptr);
}

}  // namespace taffy
