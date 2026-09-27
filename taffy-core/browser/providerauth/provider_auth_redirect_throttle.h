// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_REDIRECT_THROTTLE_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_REDIRECT_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

namespace taffy {

// Takes a vendor's authorization code out of the navigation that carries it,
// and cancels that navigation (decision 0095 section 2).
//
// Every flow of this kind was designed for a command-line tool: it opens a
// browser and the browser redirects to a loopback listener the tool is
// running. TaffyGo *is* the browser, on a phone, where a loopback listener is
// both awkward and a local attack surface. So the redirect is matched here,
// before the request starts, and the navigation never happens: nothing binds
// a port, nothing connects, and the code never reaches a page — not the
// vendor's, and certainly not one that happens to be listening on the same
// address.
//
// This class holds no policy of its own. Whether a navigation belongs to a
// sign-in is the profile broker's answer, made against the address the
// running flow actually presented and the state that flow minted, and this
// throttle only carries the question there and acts on the answer.
class ProviderAuthRedirectThrottle final : public content::NavigationThrottle {
 public:
  // Adds a throttle only to a primary navigation that starts while the profile
  // has an interceptable sign-in flow. The exact address and live state are
  // deliberately checked again at request time; this first check only keeps
  // ordinary browsing off the allocation path.
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry &registry);

  explicit ProviderAuthRedirectThrottle(
      content::NavigationThrottleRegistry &registry);
  ProviderAuthRedirectThrottle(const ProviderAuthRedirectThrottle &) = delete;
  ProviderAuthRedirectThrottle &operator=(
      const ProviderAuthRedirectThrottle &) = delete;
  ~ProviderAuthRedirectThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char *GetNameForLogging() override;

 private:
  // Both checks ask the same question, because a vendor may reach its
  // registered redirect through a chain of its own and the last hop is the
  // one that carries the code.
  ThrottleCheckResult ClaimOrProceed();
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_REDIRECT_THROTTLE_H_
