// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "url/gurl.h"

namespace taffy {

std::string TaffyPageIntelligenceHost::ObservableOrigin() const {
  const content::WebContents& web_contents = GetWebContents();
  content::RenderFrameHost* frame =
      const_cast<content::WebContents&>(web_contents).GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    return std::string();
  }
  const Origin origin =
      OriginCodec::Get().ToWireOrigin(frame->GetLastCommittedOrigin());
  // An opaque origin returns empty rather than its session-local identifier.
  // policy-engine cannot classify a nonce and a person cannot be told which
  // site it was, so there is nothing here worth carrying onward.
  return origin.is_opaque() ? std::string() : origin.serialization;
}

std::optional<DirectObservationContext>
TaffyPageIntelligenceHost::BuildDirectObservationContext() {
  content::WebContents* web_contents = observed_web_contents();
  content::RenderFrameHost* frame = web_contents->GetPrimaryMainFrame();
  auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
  if (!frame || !frame->IsRenderFrameLive() || !broker) {
    return std::nullopt;
  }
  const FrameId frame_id = broker->GetOrAssignFrameId(frame);
  FrameObservationEndpoint* endpoint =
      broker->GetOrCreateActionableEndpoint(frame_id);
  const Origin origin =
      OriginCodec::Get().ToWireOrigin(frame->GetLastCommittedOrigin());
  if (!frame_id.is_valid() || !endpoint || !origin.is_valid() ||
      origin.is_opaque()) {
    return std::nullopt;
  }
  const GURL origin_url(origin.serialization);
  if (!origin_url.is_valid() || origin_url.host().empty()) {
    return std::nullopt;
  }
  return DirectObservationContext{
      .tab_id = broker->tab_id().value,
      .frame_id = frame_id.value,
      .page_epoch = endpoint->page_epoch().value,
      .origin = origin.serialization,
      .host = std::string(origin_url.host()),
      .graph_revision = endpoint->last_reported_revision(),
  };
}

std::optional<std::string> TaffyPageIntelligenceHost::ResolveObservedLink(
    const CanonicalLinkOpenHandle& handle) {
  return ObservedLinkHandleIsCurrent(handle) ? observed_links_.Resolve(handle)
                                             : std::nullopt;
}

std::optional<std::string> TaffyPageIntelligenceHost::ResolveObservedDownload(
    const CanonicalObservedNodeHandle& handle) {
  return ObservedLinkHandleIsCurrent(handle)
             ? observed_links_.ResolveDownload(handle)
             : std::nullopt;
}

bool TaffyPageIntelligenceHost::ObservedLinkHandleIsCurrent(
    const CanonicalObservedNodeHandle& handle) {
  const std::optional<DirectObservationContext> live =
      BuildDirectObservationContext();
  if (!live) {
    LOG(WARNING) << "[taffy_observed_link_refused] at=no-live-document";
    return false;
  }
  // Which gate refused, as a compiled-in name. Nine of them answer one
  // `kNodeGone` to the model, and the recorded code alone cannot tell "the
  // page moved" from "that link is not in the table" from "the table is
  // empty" — which is what made this cost a device run to find.
  const char* at = nullptr;
  if (handle.expected_origin_is_opaque) {
    at = "opaque-origin";
  } else if (live->tab_id != handle.tab_id) {
    at = "tab";
  } else if (live->frame_id != handle.frame_id) {
    at = "frame";
  } else if (live->page_epoch != handle.page_epoch) {
    at = "page-epoch";
  } else if (live->origin != handle.expected_origin) {
    at = "origin";
  } else if (handle.graph_revision > live->graph_revision) {
    // A handle naming a revision the browser has never reported is not a
    // handle this process minted.
    at = "revision-from-the-future";
  }
  if (at) {
    LOG(WARNING) << "[taffy_observed_link_refused] at=" << at;
    return false;
  }
  return true;
}

void TaffyPageIntelligenceHost::ResolveNodeFacts(
    const NodeHandle& handle,
    base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved) {
  if (!service_) {
    std::move(on_resolved).Run(std::nullopt);
    return;
  }
  service_->ResolveNodeFacts(handle, std::move(on_resolved));
}

void TaffyPageIntelligenceHost::OnProtocolSupport(
    ProtocolSupportEnvelope result) {}

void TaffyPageIntelligenceHost::OnSubscriptionResult(
    SubscriptionEnvelope result) {}

void TaffyPageIntelligenceHost::OnActionResult(ActionResult result) {}

void TaffyPageIntelligenceHost::OnDelta(DeltaEnvelope delta) {
  // The links this delta did not retire survive it, at the revision it
  // advances to. `ApplyDelta` clears the registry itself for every advance it
  // cannot account for, which is the behaviour this used to have for all of
  // them — and which meant the only `browser.link.open` the phone ever
  // proposed was refused `NodeGone` against an empty table.
  observed_links_.ApplyDelta(delta);
}

void TaffyPageIntelligenceHost::OnPageInvalidated(InvalidationNotice notice) {
  // Only a notice about the document the links were read from takes them;
  // a child frame navigating inside the page leaves them standing.
  observed_links_.Invalidate(notice);
}

void TaffyPageIntelligenceHost::OnBackpressure(BackpressureNotice notice) {}

void TaffyPageIntelligenceHost::OnActorLeasePreempted(ActorLeaseId lease_id,
                                                      TabId tab_id) {}

void TaffyPageIntelligenceHost::RecordObservation(
    const ObservationRecord& record) {}

void TaffyPageIntelligenceHost::RecordAction(const ActionRecord& record) {}

void TaffyPageIntelligenceHost::RecordSubscription(
    const SubscriptionRecord& record) {}

}  // namespace taffy
