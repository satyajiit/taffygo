// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Which document is in the tab the isolated core named.
//
// The core may name a task and a target tab; it cannot assert which renderer
// frame, document epoch or committed origin currently occupies that tab, so
// every one of these answers is read off the browser here and nowhere else.
// There are three documents a task can stand on and they are deliberately
// three functions rather than one with a flag: an ordinary page with a site of
// its own, the opaque blank a zero-source errand is given to find its first
// source in, and a document with no site of its own that a task may only leave
// (decision 0176). A fourth answers the tab's own session history.

#include <optional>
#include <string>

#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace taffy {

std::optional<TaskPolicyDocumentContext> ResolveTaskPolicyDocument(
    content::BrowserContext* browser_context,
    const std::string& tab_id) {
  TaffyPageIntelligenceHost* host =
      FindExactlyOneTabHost(browser_context, tab_id);
  if (!host) {
    return std::nullopt;
  }
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  if (!live || live->tab_id != tab_id) {
    return std::nullopt;
  }
  return TaskPolicyDocumentContext{
      .tab_id = live->tab_id,
      .frame_id = live->frame_id,
      .page_epoch = live->page_epoch,
      .origin = live->origin,
      .graph_revision = live->graph_revision,
  };
}

std::optional<TaskDiscoveryDocumentContext> ResolveTaskDiscoveryDocument(
    content::BrowserContext* browser_context,
    const std::string& tab_id) {
  TaffyPageIntelligenceHost* host =
      FindExactlyOneTabHost(browser_context, tab_id);
  content::WebContents* web_contents =
      host ? host->observed_web_contents() : nullptr;
  auto* broker = web_contents
                     ? PageIntelligenceBroker::FromWebContents(web_contents)
                     : nullptr;
  if (!broker) {
    return std::nullopt;
  }
  content::RenderFrameHost* frame = web_contents->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive() ||
      web_contents->GetLastCommittedURL() != GURL("about:blank") ||
      !frame->GetLastCommittedOrigin().opaque()) {
    return std::nullopt;
  }
  const FrameId frame_id = broker->GetOrAssignFrameId(frame);
  FrameObservationEndpoint* endpoint =
      broker->GetOrCreateActionableEndpoint(frame_id);
  if (!frame_id.is_valid() || !endpoint || !endpoint->page_epoch().is_valid()) {
    return std::nullopt;
  }
  return TaskDiscoveryDocumentContext{
      .tab_id = broker->tab_id().value,
      .frame_id = frame_id.value,
      .page_epoch = endpoint->page_epoch().value,
      // A page epoch is already a browser-minted, document-scoped opaque
      // identifier. Reusing that exact identity avoids inventing a second
      // nonce that could accidentally outlive the blank document.
      .opaque_origin_id = endpoint->page_epoch().value,
  };
}

std::optional<TaskDepartureDocumentContext> ResolveTaskDepartureDocument(
    content::BrowserContext* browser_context,
    const std::string& tab_id) {
  TaffyPageIntelligenceHost* host =
      FindExactlyOneTabHost(browser_context, tab_id);
  content::WebContents* web_contents =
      host ? host->observed_web_contents() : nullptr;
  auto* broker = web_contents
                     ? PageIntelligenceBroker::FromWebContents(web_contents)
                     : nullptr;
  if (!broker) {
    return std::nullopt;
  }
  content::RenderFrameHost* frame = web_contents->GetPrimaryMainFrame();
  // The one document this answers for: live, top level, and carrying no site
  // of its own. A document that does have a site is resolved by
  // `ResolveTaskPolicyDocument`, which is the context that authorizes reading
  // and acting; answering here for one of those would be a second route to a
  // page that has an origin, which is not what this is.
  if (!frame || !frame->IsRenderFrameLive() ||
      !frame->GetLastCommittedOrigin().opaque()) {
    return std::nullopt;
  }
  const FrameId frame_id = broker->GetOrAssignFrameId(frame);
  FrameObservationEndpoint* endpoint =
      broker->GetOrCreateActionableEndpoint(frame_id);
  if (!frame_id.is_valid() || !endpoint || !endpoint->page_epoch().is_valid()) {
    return std::nullopt;
  }
  return TaskDepartureDocumentContext{
      .tab_id = broker->tab_id().value,
      .frame_id = frame_id.value,
      .page_epoch = endpoint->page_epoch().value,
      // The same identity a discovery blank carries, for the same reason: a
      // page epoch is already a browser-minted, document-scoped opaque
      // identifier, so nothing here invents a second nonce.
      .opaque_origin_id = endpoint->page_epoch().value,
  };
}

std::optional<std::string> ResolveTaskHistoryDestination(
    content::BrowserContext* browser_context,
    const std::string& tab_id,
    core_service::mojom::TaskActionOperationKind operation) {
  TaffyPageIntelligenceHost* host =
      FindExactlyOneTabHost(browser_context, tab_id);
  content::WebContents* web_contents =
      host ? host->observed_web_contents() : nullptr;
  if (!web_contents) {
    return std::nullopt;
  }
  int offset = 0;
  switch (operation) {
    case core_service::mojom::TaskActionOperationKind::kHistoryBack:
      offset = -1;
      break;
    case core_service::mojom::TaskActionOperationKind::kHistoryForward:
      offset = 1;
      break;
    default:
      return std::nullopt;
  }
  content::NavigationEntry* entry =
      web_contents->GetController().GetEntryAtOffset(offset);
  if (!entry) {
    return std::nullopt;
  }
  const GURL& target = entry->GetURL();
  if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS() ||
      target.has_username() || target.has_password() || target.spec().empty() ||
      target.spec().size() > core_service::mojom::kMaxDestinationAddressBytes) {
    return std::nullopt;
  }
  return target.spec();
}

}  // namespace taffy
