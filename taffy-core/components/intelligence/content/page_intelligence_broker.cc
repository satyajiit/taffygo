// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/page_intelligence_broker.h"

#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace {

// Session-scoped monotonic allocation behind an opaque string.
//
// Never persisted, never restarted at zero inside one browser process, and
// never derived from a pointer: an identifier that leaked a heap address would
// be an information disclosure, and one that restarted would let a stale
// handle come back to life. The prefix exists so that a human reading a
// journal record can tell what kind of thing an identifier names without
// looking it up.
std::string NextSessionId(const char* prefix) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  static uint64_t next = 1;
  return base::StrCat({prefix, base::NumberToString(next++)});
}

DocumentLifecycleState ToContractLifecycle(
    content::RenderFrameHost::LifecycleState state) {
  switch (state) {
    case content::RenderFrameHost::LifecycleState::kPendingCommit:
      return DocumentLifecycleState::kPendingCommit;
    case content::RenderFrameHost::LifecycleState::kPrerendering:
      return DocumentLifecycleState::kPrerendering;
    case content::RenderFrameHost::LifecycleState::kActive:
      return DocumentLifecycleState::kActive;
    case content::RenderFrameHost::LifecycleState::kInBackForwardCache:
      return DocumentLifecycleState::kBackForwardCached;
    case content::RenderFrameHost::LifecycleState::kPendingDeletion:
      return DocumentLifecycleState::kDestroyed;
  }
  // VERIFY AT SP-01: content::RenderFrameHost::LifecycleState gains members
  // over time. An unmapped state is reported as destroyed, which is the
  // fail-closed answer: it makes the frame non-actionable rather than
  // defaulting it to active.
  return DocumentLifecycleState::kDestroyed;
}

}  // namespace

// static
bool PageIntelligenceBroker::IsObservableLifecycle(
    content::RenderFrameHost::LifecycleState state) {
  return state == content::RenderFrameHost::LifecycleState::kActive;
}

PageIntelligenceBroker::PageIntelligenceBroker(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<PageIntelligenceBroker>(*web_contents),
      tab_id_(TabId{NextSessionId("tab_")}) {}

PageIntelligenceBroker::~PageIntelligenceBroker() {
  // This source and some of its observers are independent WebContentsUserData
  // entries. SupportsUserData intentionally destroys those entries in an
  // unspecified order, so every surviving observer must detach while this
  // source and its ObserverList are still alive.
  for (Observer& observer : observers_) {
    observer.OnBrokerDestroyed(tab_id_);
  }
  CHECK(observers_.empty());
}

void PageIntelligenceBroker::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PageIntelligenceBroker::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

base::WeakPtr<PageIntelligenceBroker> PageIntelligenceBroker::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

FrameId PageIntelligenceBroker::GetOrAssignFrameId(
    content::RenderFrameHost* host) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!host) {
    return FrameId();
  }
  const content::FrameTreeNodeId node_id = host->GetFrameTreeNodeId();
  auto it = frame_ids_.find(node_id);
  if (it != frame_ids_.end()) {
    return it->second;
  }
  // Keyed by frame tree node, so the identifier survives a same-frame
  // navigation. That is what makes "this frame navigated, the parent's
  // assumptions did not change" expressible at all.
  const FrameId frame_id{NextSessionId("frame_")};
  frame_ids_.emplace(node_id, frame_id);
  frame_nodes_.emplace(frame_id, node_id);
  return frame_id;
}

content::RenderFrameHost* PageIntelligenceBroker::ResolveFrame(
    const FrameId& frame_id) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = frame_nodes_.find(frame_id);
  if (it == frame_nodes_.end() || !web_contents()) {
    return nullptr;
  }

  // Walk Chromium's own frame tree. Resolving through the browser's topology
  // rather than through a cached pointer is what makes "the frame is gone"
  // answerable instead of a use-after-free.
  //
  // VERIFY AT SP-01: the ForEachRenderFrameHost family has changed shape
  // across milestones (base::FunctionRef versus a repeating callback, and a
  // WithAction variant for early exit). Confirm the exact spelling at the
  // pinned milestone; the traversal is what matters.
  content::RenderFrameHost* found = nullptr;
  const content::FrameTreeNodeId wanted = it->second;
  web_contents()->ForEachRenderFrameHost(
      [&found, wanted](content::RenderFrameHost* host) {
        if (!found && host->GetFrameTreeNodeId() == wanted) {
          found = host;
        }
      });
  return found;
}

FrameObservationEndpoint* PageIntelligenceBroker::GetOrCreateEndpoint(
    content::RenderFrameHost* host) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!host || !host->IsRenderFrameLive()) {
    return nullptr;
  }
  if (!IsObservableLifecycle(host->GetLifecycleState())) {
    return nullptr;
  }

  if (auto* existing = FrameObservationEndpoint::GetForCurrentDocument(host)) {
    return existing->is_actionable() ? existing : nullptr;
  }

  const FrameId frame_id = GetOrAssignFrameId(host);
  if (!frame_id.is_valid()) {
    return nullptr;
  }

  // A page epoch is allocated exactly once per document, here, and dies with
  // the document because the endpoint is DocumentUserData.
  //
  // "First use" is no longer only "somebody observed this document". Since
  // EnsureFrameTreeIdentity() below, an observation that may describe child
  // frames allocates for every eligible frame in the subtree, so a child
  // frame nobody asked about individually now consumes an identifier. That is
  // a deliberate widening of when an identifier is minted, and the reason is
  // in EnsureFrameTreeIdentity's own comment: without it a child frame was
  // absent from the frame list rather than listed and excluded, and absent is
  // the answer that makes the page's shape unknowable.
  //
  // The comment in page_intelligence_broker_lifecycle.cc that "a document
  // nobody observes never consumes an identifier" is still true of the
  // navigation path it is written on - DidFinishNavigation still allocates
  // nothing - and is no longer true of the observation path.
  FrameObservationEndpoint::CreateForCurrentDocument(
      host, frame_id, PageEpoch{NextSessionId("epoch_")},
      OriginCodec::Get().ToWireOrigin(host->GetLastCommittedOrigin()));
  return FrameObservationEndpoint::GetForCurrentDocument(host);
}

FrameObservationEndpoint*
PageIntelligenceBroker::GetOrCreateActionableEndpoint(const FrameId& frame_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return GetOrCreateEndpoint(ResolveFrame(frame_id));
}

FrameObservationEndpoint* PageIntelligenceBroker::GetActionableEndpoint(
    const FrameId& frame_id) {
  content::RenderFrameHost* host = ResolveFrame(frame_id);
  if (!host) {
    return nullptr;
  }
  auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host);
  if (!endpoint || !endpoint->is_actionable()) {
    return nullptr;
  }
  if (!IsObservableLifecycle(host->GetLifecycleState()) ||
      !host->IsRenderFrameLive()) {
    return nullptr;
  }
  return endpoint;
}

void PageIntelligenceBroker::EnsureFrameTreeIdentity(
    const FrameId& root_frame_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  content::RenderFrameHost* root = ResolveFrame(root_frame_id);
  if (!root) {
    return;
  }
  root->ForEachRenderFrameHost(
      [this](content::RenderFrameHost* host) { GetOrCreateEndpoint(host); });
}

std::vector<FrameSummary> PageIntelligenceBroker::BuildFrameTree(
    const FrameId& root_frame_id) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<FrameSummary> tree;

  content::RenderFrameHost* root = ResolveFrame(root_frame_id);
  if (!root) {
    return tree;
  }

  // Topology comes from Chromium. A renderer describes only what it can see,
  // and a compromised one would happily describe a tree it is not in
  // (protocol section 8.1).
  root->ForEachRenderFrameHost([&](content::RenderFrameHost* host) {
    auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host);
    if (!endpoint) {
      return;
    }
    FrameSummary summary;
    summary.frame_id = endpoint->frame_id();
    summary.page_epoch = endpoint->page_epoch();
    summary.graph_revision = endpoint->last_reported_revision();
    summary.origin = endpoint->origin();
    summary.lifecycle_state = ToContractLifecycle(host->GetLifecycleState());
    summary.is_main_frame = host->GetParentOrOuterDocument() == nullptr;
    summary.is_out_of_process =
        !summary.is_main_frame &&
        host->GetProcess() != root->GetProcess();
    summary.included = IsObservableLifecycle(host->GetLifecycleState()) &&
                       host->IsRenderFrameLive() && endpoint->is_actionable();

    if (content::RenderFrameHost* parent = host->GetParentOrOuterDocument()) {
      if (auto* parent_endpoint =
              FrameObservationEndpoint::GetForCurrentDocument(parent)) {
        summary.parent_frame_id = parent_endpoint->frame_id();
      }
      summary.is_cross_origin_to_parent =
          !host->GetLastCommittedOrigin().IsSameOriginWith(
              parent->GetLastCommittedOrigin());
    }
    tree.push_back(std::move(summary));
  });
  return tree;
}

std::optional<ActionResultCode> PageIntelligenceBroker::CheckHandleLiveness(
    const NodeHandle& handle,
    GraphRevision required_revision) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // Step 1: the tab, in the expected profile. The broker is per-WebContents, so
  // "the expected profile" is structural: a handle for another tab cannot
  // reach this instance.
  if (handle.tab_id != tab_id_) {
    return ActionResultCode::kTabGone;
  }

  // Step 2: the frame still exists.
  content::RenderFrameHost* host = ResolveFrame(handle.frame_id);
  if (!host) {
    return ActionResultCode::kFrameGone;
  }

  auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host);
  if (!endpoint || !endpoint->is_actionable()) {
    return ActionResultCode::kStalePageEpoch;
  }

  // Step 2 continued: an active document, and exactly the expected epoch. A
  // mismatch is stale, never "close enough to rebind".
  if (endpoint->page_epoch() != handle.page_epoch) {
    return ActionResultCode::kStalePageEpoch;
  }
  if (!IsObservableLifecycle(host->GetLifecycleState()) ||
      !host->IsRenderFrameLive()) {
    return ActionResultCode::kDocumentInactive;
  }

  // Step 3: the origin the handle was issued against, compared through the
  // codec so two distinct opaque origins stay distinct.
  if (!OriginCodec::Get().Matches(handle.expected_origin,
                                  host->GetLastCommittedOrigin())) {
    return ActionResultCode::kOriginChanged;
  }

  // The freshness requirement, and it is two questions rather than one.
  //
  // The first is whether the document moved after the handle was issued. The
  // browser can know that without knowing what the graph now looks like — a
  // same-document commit is the standing example — and it is a refusal on its
  // own, because a handle can be perfectly current by revision and still
  // describe a document that changed a millisecond ago.
  if (endpoint->resnapshot_required()) {
    return ActionResultCode::kGraphMovedDuringPreflight;
  }

  // The second is ordering, and it is meaningful here and only here: both
  // revisions belong to the same epoch, which was just proven equal, and both
  // are now the renderer's own numbering rather than one of each.
  if (endpoint->last_reported_revision() < required_revision) {
    return ActionResultCode::kStaleGraph;
  }

  return std::nullopt;
}

void PageIntelligenceBroker::NoteReportedRevision(const FrameId& frame_id,
                                                 GraphRevision revision) {
  content::RenderFrameHost* host = ResolveFrame(frame_id);
  if (!host) {
    return;
  }
  if (auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host)) {
    endpoint->NoteReportedRevision(revision);
  }
}

void PageIntelligenceBroker::RetireDocument(content::RenderFrameHost* host,
                                            InvalidationCode reason,
                                            bool child_frames_only) {
  if (!host) {
    return;
  }
  auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host);
  if (!endpoint || !endpoint->is_actionable()) {
    return;
  }
  const FrameId frame_id = endpoint->frame_id();
  const PageEpoch retired = endpoint->page_epoch();
  endpoint->Retire();
  NotifyInvalidated(frame_id, retired, PageEpoch(), reason, child_frames_only);
}

void PageIntelligenceBroker::RestoreDocumentFromBackForwardCache(
    content::RenderFrameHost* host) {
  if (!host) {
    return;
  }
  FrameObservationEndpoint* endpoint =
      FrameObservationEndpoint::GetForCurrentDocument(host);
  if (!endpoint) {
    // Nothing was ever observed in this document, so there is no epoch to
    // retire and the first use after the restore allocates the only one there
    // will have been.
    return;
  }
  const FrameId frame_id = endpoint->frame_id();
  const PageEpoch retired = endpoint->page_epoch();

  // Retire before deleting so the mojo remote is dropped through the same path
  // every other retirement uses, and so an in-flight call cannot land on an
  // endpoint that is being destroyed.
  endpoint->Retire();
  // The endpoint is DocumentUserData, so without this the retired instance
  // outlives the round trip and GetOrCreateEndpoint keeps answering nullptr
  // for a document that is live, primary and perfectly observable. Deleting it
  // is what lets the next use allocate the new epoch this row calls for; the
  // allocation itself stays lazy, so a restored document nobody observes still
  // consumes no identifier.
  FrameObservationEndpoint::DeleteForCurrentDocument(host);

  // The notice names the retired epoch and no successor, because there is no
  // successor until somebody asks for one. A handle from before the round trip
  // is dead either way: the epoch it names will never be issued again.
  NotifyInvalidated(frame_id, retired, PageEpoch(),
                    InvalidationCode::kBfcacheRestored,
                    /*child_frames_only=*/false);
}

void PageIntelligenceBroker::NotifyInvalidated(const FrameId& frame_id,
                                               const PageEpoch& retired_epoch,
                                               const PageEpoch& new_epoch,
                                               InvalidationCode reason,
                                               bool child_frames_only) {
  InvalidationNotice notice;
  notice.tab_id = tab_id_;
  notice.frame_id = frame_id;
  notice.page_epoch = retired_epoch;
  notice.reason = reason;
  notice.retires_page_epoch = retired_epoch.is_valid();
  notice.invalidates_child_frames_only = child_frames_only;
  notice.resnapshot_required = true;
  if (new_epoch.is_valid()) {
    notice.new_page_epoch = new_epoch;
  }
  notice.observed_at_monotonic_ms = NowMonotonicMs();

  // Synchronous, and before anything else this tab has queued: navigation
  // invalidation has priority over extraction and action work
  // (protocol section 6.3). An observer that cancels an in-flight action does
  // so inside this loop, which is what makes the cancellation race free.
  for (Observer& observer : observers_) {
    observer.OnPageInvalidated(notice);
  }
}

bool PageIntelligenceBroker::Invalidate(const FrameId& frame_id,
                                        InvalidationCode reason) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  content::RenderFrameHost* host = ResolveFrame(frame_id);
  if (!host) {
    return false;
  }
  auto* endpoint = FrameObservationEndpoint::GetForCurrentDocument(host);
  if (!endpoint || !endpoint->is_actionable()) {
    return false;
  }
  RetireDocument(host, reason, /*child_frames_only=*/false);
  return true;
}

void PageIntelligenceBroker::InvalidateAll(InvalidationCode reason) {
  if (!web_contents()) {
    return;
  }
  web_contents()->ForEachRenderFrameHost(
      [this, reason](content::RenderFrameHost* host) {
        RetireDocument(host, reason, /*child_frames_only=*/false);
      });
  for (Observer& observer : observers_) {
    observer.OnUserPreemption(tab_id_);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PageIntelligenceBroker);

}  // namespace taffy
