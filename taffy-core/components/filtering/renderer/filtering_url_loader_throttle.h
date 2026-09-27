// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_RENDERER_FILTERING_URL_LOADER_THROTTLE_H_
#define TAFFY_COMPONENTS_FILTERING_RENDERER_FILTERING_URL_LOADER_THROTTLE_H_

#include <optional>

#include "base/memory/weak_ptr.h"
#include "base/types/optional_ref.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "taffy/components/filtering/mojom/request_filter.mojom.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "third_party/blink/public/common/tokens/tokens.h"

namespace taffy::filtering {

// The renderer half of one resource decision. It knows only how to suspend a
// loader and apply the browser's answer. Rules, posture, document attribution,
// and counters stay behind RequestFilter's browser-process interface.
class FilteringURLLoaderThrottle final : public blink::URLLoaderThrottle {
 public:
  FilteringURLLoaderThrottle(
      mojo::PendingRemote<mojom::RequestFilter> request_filter,
      base::optional_ref<const blink::LocalFrameToken> local_frame_token);
  ~FilteringURLLoaderThrottle() override;

  FilteringURLLoaderThrottle(const FilteringURLLoaderThrottle&) = delete;
  FilteringURLLoaderThrottle& operator=(const FilteringURLLoaderThrottle&) =
      delete;

  // blink::URLLoaderThrottle:
  void DetachFromCurrentSequence() override;
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      network::HttpRequestHeadersUpdateParams* headers_update_params) override;

 private:
  void Check(const GURL& url,
             network::mojom::RequestDestination destination,
             bool* defer);
  void OnChecked(bool blocked);
  void OnDisconnected();

  mojo::PendingRemote<mojom::RequestFilter> pending_request_filter_;
  mojo::Remote<mojom::RequestFilter> request_filter_;
  const std::optional<blink::LocalFrameToken> local_frame_token_;
  network::mojom::RequestDestination destination_ =
      network::mojom::RequestDestination::kEmpty;
  bool check_pending_ = false;
  bool deferred_ = false;
  bool filtering_available_ = true;
  base::WeakPtrFactory<FilteringURLLoaderThrottle> weak_factory_{this};
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_RENDERER_FILTERING_URL_LOADER_THROTTLE_H_
