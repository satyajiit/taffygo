// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_THROTTLE_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_THROTTLE_H_

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "components/url_pattern_index/proto/rules.pb.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/origin.h"

#include "taffy/components/filtering/browser/filtering_ruleset_service.h"

namespace network {
struct ResourceRequest;
}

namespace taffy::filtering {

// One request's brush with the filtering plane. Created only when the plane
// is acting — matcher present, posture active for the document — so the
// request path of a person who turned blocking off, or excepted this site,
// carries no throttle at all.
//
// The throttle holds its own reference to the ruleset and its own copy of the
// document facts: a swap or a posture change mid-request cannot change this
// request's verdict, and the throttle runs on whatever sequence the loader
// drives without reaching back to the UI thread to decide.
class FilteringThrottle : public blink::URLLoaderThrottle {
 public:
  // Runs on the sequence the throttle decides on; the creator binds it back
  // to the UI thread where the counters live.
  using BlockedCallback = base::RepeatingClosure;

  FilteringThrottle(scoped_refptr<const SharedRuleset> ruleset,
                    url::Origin document_origin,
                    bool disable_generic_rules,
                    BlockedCallback on_blocked);
  ~FilteringThrottle() override;

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      network::HttpRequestHeadersUpdateParams* headers_update_params) override;

 private:
  bool ShouldBlock(const GURL& url,
                   network::mojom::RequestDestination destination) const;

  const scoped_refptr<const SharedRuleset> ruleset_;
  const url::Origin document_origin_;
  const bool disable_generic_rules_;
  const BlockedCallback on_blocked_;
  // Remembered from the start so a redirect is judged as what the document
  // asked for, not as what a server rewrote it into.
  network::mojom::RequestDestination destination_ =
      network::mojom::RequestDestination::kEmpty;
};

// The element class a fetch destination is, in the rule vocabulary. Exposed
// for the tests that pin the mapping down.
url_pattern_index::proto::ElementType ElementTypeForDestination(
    network::mojom::RequestDestination destination);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_THROTTLE_H_
