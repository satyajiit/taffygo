// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_auth_redirect_throttle.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "url/gurl.h"

namespace taffy {

// static
void ProviderAuthRedirectThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry &registry) {
  content::NavigationHandle &handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame()) {
    return;
  }
  content::WebContents *contents = handle.GetWebContents();
  Profile *profile =
      contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
               : nullptr;
  CoreServiceManager *manager =
      profile ? CoreServiceManagerFactory::GetForProfileIfExists(profile)
              : nullptr;
  if (!manager || !manager->HasInterceptableProviderRedirect()) {
    return;
  }
  registry.AddThrottle(
      std::make_unique<ProviderAuthRedirectThrottle>(registry));
}

ProviderAuthRedirectThrottle::ProviderAuthRedirectThrottle(
    content::NavigationThrottleRegistry &registry)
    : content::NavigationThrottle(registry) {}

ProviderAuthRedirectThrottle::~ProviderAuthRedirectThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
ProviderAuthRedirectThrottle::WillStartRequest() {
  return ClaimOrProceed();
}

content::NavigationThrottle::ThrottleCheckResult
ProviderAuthRedirectThrottle::WillRedirectRequest() {
  return ClaimOrProceed();
}

const char *ProviderAuthRedirectThrottle::GetNameForLogging() {
  return "ProviderAuthRedirectThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
ProviderAuthRedirectThrottle::ClaimOrProceed() {
  content::NavigationHandle *handle = navigation_handle();
  // Only a top-level document navigation can be a vendor's redirect. A
  // subframe or a fenced frame asking for the same address is a page trying
  // to make the product hand it a sign-in, and it is left to load and fail
  // like any other request the vendor will refuse.
  if (!handle || !handle->IsInPrimaryMainFrame()) {
    return PROCEED;
  }
  content::WebContents *contents = handle->GetWebContents();
  Profile *profile =
      contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
               : nullptr;
  if (!profile) {
    return PROCEED;
  }
  // `GetForProfileIfExists` for the reason the filtering seam gives: a
  // navigation must never be the thing that constructs a profile's
  // core-service manager. A profile with no manager has no running sign-in
  // either, so there is nothing this could have claimed.
  CoreServiceManager *manager =
      CoreServiceManagerFactory::GetForProfileIfExists(profile);
  if (!manager || !manager->ClaimProviderRedirectNavigation(handle->GetURL())) {
    return PROCEED;
  }
  // The code has been taken. Cancelling and ignoring leaves the tab exactly
  // where it was rather than showing an error for something that worked.
  return CANCEL_AND_IGNORE;
}

}  // namespace taffy
