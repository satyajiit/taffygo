// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/filtering_url_loader_throttle.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "net/base/net_errors.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace taffy::filtering {

FilteringURLLoaderThrottle::FilteringURLLoaderThrottle(
    mojo::PendingRemote<mojom::RequestFilter> request_filter,
    base::optional_ref<const blink::LocalFrameToken> local_frame_token)
    : pending_request_filter_(std::move(request_filter)),
      local_frame_token_(local_frame_token.CopyAsOptional()) {}

FilteringURLLoaderThrottle::~FilteringURLLoaderThrottle() = default;

void FilteringURLLoaderThrottle::DetachFromCurrentSequence() {
  // The pipe is deliberately still pending here. It binds on the loader's
  // eventual sequence in Check(), so no bound Remote crosses sequences.
}

void FilteringURLLoaderThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  destination_ = request->destination;
  Check(request->url, destination_, defer);
}

void FilteringURLLoaderThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  Check(redirect_info->new_url, destination_, defer);
}

void FilteringURLLoaderThrottle::Check(
    const GURL& url,
    network::mojom::RequestDestination destination,
    bool* defer) {
  if (!url.SchemeIsHTTPOrHTTPS() ||
      destination == network::mojom::RequestDestination::kDocument ||
      !filtering_available_) {
    return;
  }
  CHECK(!check_pending_);
  if (!request_filter_.is_bound()) {
    request_filter_.Bind(std::move(pending_request_filter_));
    request_filter_.set_disconnect_handler(base::BindOnce(
        &FilteringURLLoaderThrottle::OnDisconnected,
        weak_factory_.GetWeakPtr()));
  }
  check_pending_ = true;
  deferred_ = true;
  *defer = true;
  request_filter_->Check(
      local_frame_token_, url, destination,
      base::BindOnce(&FilteringURLLoaderThrottle::OnChecked,
                     weak_factory_.GetWeakPtr()));
}

void FilteringURLLoaderThrottle::OnChecked(bool blocked) {
  if (!check_pending_) {
    return;
  }
  check_pending_ = false;
  deferred_ = false;
  if (blocked) {
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT, "TaffyFiltering");
    return;
  }
  delegate_->Resume();
}

void FilteringURLLoaderThrottle::OnDisconnected() {
  // Browser teardown or a missing binder must not strand a loader. Filtering
  // fails open when its authority is unavailable, matching the no-ruleset
  // browser path.
  request_filter_.reset();
  filtering_available_ = false;
  if (!check_pending_) {
    return;
  }
  check_pending_ = false;
  if (deferred_) {
    deferred_ = false;
    delegate_->Resume();
  }
}

}  // namespace taffy::filtering
