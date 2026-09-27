// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/filtering_throttles.h"

#include "chrome/browser/profiles/profile.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/filtering/browser/filtering_throttle_factory.h"

namespace taffy {

void AppendFilteringThrottles(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context,
    const base::RepeatingCallback<content::WebContents*()>& wc_getter,
    std::vector<std::unique_ptr<blink::URLLoaderThrottle>>& throttles) {
  Profile* profile = Profile::FromBrowserContext(browser_context);
  if (!profile) {
    return;
  }
  // `GetForProfileIfExists` on purpose: a request must never be the thing
  // that constructs a profile's core-service manager. Until something real
  // has built it, filtering has no ruleset anyway, and a request passing
  // unjudged is exactly what "no rules yet" honestly is.
  CoreServiceManager* manager =
      CoreServiceManagerFactory::GetForProfileIfExists(profile);
  if (!manager) {
    return;
  }
  filtering::FilteringRulesetService* service = manager->filtering_service();
  if (!service) {
    return;
  }
  filtering::AppendFilteringThrottles(service->GetWeakPtr(), request,
                                      wc_getter, throttles);
}

}  // namespace taffy
