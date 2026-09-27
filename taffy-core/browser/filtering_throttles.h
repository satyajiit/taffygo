// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FILTERING_THROTTLES_H_
#define TAFFY_BROWSER_FILTERING_THROTTLES_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace content {
class BrowserContext;
class WebContents;
}
namespace network {
struct ResourceRequest;
}

namespace taffy {

// The one entry patch 0033 calls from the upstream throttle factory. Appends
// the filtering plane's throttle for `request` when the profile's plane is
// acting, and nothing otherwise — a profile with no manager, blocking off, a
// site exception, or no compiled ruleset all cost one early return. UI thread.
void AppendFilteringThrottles(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context,
    const base::RepeatingCallback<content::WebContents*()>& wc_getter,
    std::vector<std::unique_ptr<blink::URLLoaderThrottle>>& throttles);

}  // namespace taffy

#endif  // TAFFY_BROWSER_FILTERING_THROTTLES_H_
