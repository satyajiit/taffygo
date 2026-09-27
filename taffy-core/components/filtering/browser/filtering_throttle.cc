// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_throttle.h"

#include <utility>

#include "net/base/net_errors.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/gurl.h"

namespace taffy::filtering {

namespace proto = url_pattern_index::proto;

FilteringThrottle::FilteringThrottle(
    scoped_refptr<const SharedRuleset> ruleset,
    url::Origin document_origin,
    bool disable_generic_rules,
    BlockedCallback on_blocked)
    : ruleset_(std::move(ruleset)),
      document_origin_(std::move(document_origin)),
      disable_generic_rules_(disable_generic_rules),
      on_blocked_(std::move(on_blocked)) {}

FilteringThrottle::~FilteringThrottle() = default;

void FilteringThrottle::WillStartRequest(network::ResourceRequest* request,
                                         bool* defer) {
  destination_ = request->destination;
  if (ShouldBlock(request->url, destination_)) {
    on_blocked_.Run();
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT, "TaffyFiltering");
  }
}

void FilteringThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  // A redirect is a new URL under the same ask: a tracker that answers a
  // clean request with a hop into a blocked host is caught here, in the same
  // vocabulary the first hop was judged in.
  if (ShouldBlock(redirect_info->new_url, destination_)) {
    on_blocked_.Run();
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT, "TaffyFiltering");
  }
}

bool FilteringThrottle::ShouldBlock(
    const GURL& url,
    network::mojom::RequestDestination destination) const {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  // Top-level documents are never filtered: blocking what a person asked to
  // open is an interstitial's job, not a filter's.
  if (destination == network::mojom::RequestDestination::kDocument) {
    return false;
  }
  return ruleset_->matcher().ShouldBlockRequest(
      url, document_origin_, ElementTypeForDestination(destination),
      disable_generic_rules_);
}

proto::ElementType ElementTypeForDestination(
    network::mojom::RequestDestination destination) {
  using Destination = network::mojom::RequestDestination;
  switch (destination) {
    case Destination::kDocument:
      // Judged nowhere — the throttle answers for documents before mapping —
      // but the mapping stays total.
      return proto::ELEMENT_TYPE_OTHER;
    case Destination::kIframe:
    case Destination::kFrame:
    case Destination::kFencedframe:
      return proto::ELEMENT_TYPE_SUBDOCUMENT;
    case Destination::kScript:
    case Destination::kWorker:
    case Destination::kSharedWorker:
    case Destination::kServiceWorker:
    case Destination::kAudioWorklet:
    case Destination::kPaintWorklet:
    case Destination::kSharedStorageWorklet:
    case Destination::kXslt:
      return proto::ELEMENT_TYPE_SCRIPT;
    case Destination::kImage:
      return proto::ELEMENT_TYPE_IMAGE;
    case Destination::kStyle:
      return proto::ELEMENT_TYPE_STYLESHEET;
    case Destination::kFont:
      return proto::ELEMENT_TYPE_FONT;
    case Destination::kAudio:
    case Destination::kVideo:
    case Destination::kTrack:
      return proto::ELEMENT_TYPE_MEDIA;
    case Destination::kObject:
    case Destination::kEmbed:
      return proto::ELEMENT_TYPE_OBJECT;
    case Destination::kWebBundle:
    case Destination::kManifest:
    case Destination::kReport:
    case Destination::kSpeculationRules:
    case Destination::kCompressionDictionary:
    case Destination::kJson:
    case Destination::kWebIdentity:
    case Destination::kEmailVerification:
    case Destination::kText:
      return proto::ELEMENT_TYPE_OTHER;
    case Destination::kEmpty:
      // fetch(), XHR and beacons all arrive as an empty destination; the
      // rule vocabulary calls that family XMLHTTPREQUEST.
      return proto::ELEMENT_TYPE_XMLHTTPREQUEST;
    default:
      // The destination vocabulary grows with the platform; a member this
      // build does not name is judged as OTHER rather than let through
      // unjudged.
      return proto::ELEMENT_TYPE_OTHER;
  }
}

}  // namespace taffy::filtering
