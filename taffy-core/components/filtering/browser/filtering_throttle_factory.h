// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_THROTTLE_FACTORY_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_THROTTLE_FACTORY_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace content {
class WebContents;
}
namespace network {
struct ResourceRequest;
}

namespace taffy::filtering {

class FilteringRulesetService;

// Decides, on the UI thread, whether one request gets a filtering throttle,
// and builds it with everything the verdict needs copied in. No throttle is
// created when the plane is not acting — blocking off, this site excepted,
// the document allowlisted by rule, no ruleset compiled yet, or a request no
// tab owns — so the answer for those requests costs one early return.
void AppendFilteringThrottles(
    base::WeakPtr<FilteringRulesetService> service,
    const network::ResourceRequest& request,
    const base::RepeatingCallback<content::WebContents*()>& wc_getter,
    std::vector<std::unique_ptr<blink::URLLoaderThrottle>>& throttles);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_THROTTLE_FACTORY_H_
