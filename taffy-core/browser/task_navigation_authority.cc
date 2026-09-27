// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_navigation_authority.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_ui_data.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "taffy/browser/task_navigation_authority_platform.h"
#include "taffy/components/security/browser/restricted_destination_classifier.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace taffy {
namespace {

bool IsNormalizedHttpAddress(const GURL& address) {
  return address.is_valid() && address.SchemeIsHTTPOrHTTPS() &&
         !address.has_username() && !address.has_password() &&
         address.spec() == address.possibly_invalid_spec();
}

const TaskNavigationAuthorityPlatform*& PlatformSlot() {
  static const TaskNavigationAuthorityPlatform* platform = nullptr;
  return platform;
}

}  // namespace

TaskNavigationAuthority::~TaskNavigationAuthority() = default;

TaskNavigationAuthority::TaskNavigationAuthority(GURL exact_destination)
    : exact_destination_(std::move(exact_destination)) {}

// static
std::unique_ptr<content::NavigationUIData>
TaskNavigationAuthority::CreateNavigationData(const GURL& exact_destination) {
  if (!IsNormalizedHttpAddress(exact_destination)) {
    return nullptr;
  }
  const TaskNavigationAuthorityPlatform* platform =
      GetTaskNavigationAuthorityPlatform();
  return platform ? platform->CreateNavigationData(exact_destination.spec())
                  : nullptr;
}

// static
std::optional<TaskNavigationAuthority> TaskNavigationAuthority::FromNavigation(
    content::NavigationHandle& navigation_handle) {
  content::NavigationUIData* untyped = navigation_handle.GetNavigationUIData();
  if (!untyped) {
    return std::nullopt;
  }
  const TaskNavigationAuthorityPlatform* platform =
      GetTaskNavigationAuthorityPlatform();
  if (!platform) {
    return std::nullopt;
  }
  const std::optional<std::string> encoded =
      platform->ReadExactDestination(*untyped);
  if (!encoded) {
    return std::nullopt;
  }
  const GURL destination(*encoded);
  if (!IsNormalizedHttpAddress(destination) || destination.spec() != *encoded) {
    return std::nullopt;
  }
  return TaskNavigationAuthority(destination);
}

void InstallTaskNavigationAuthorityPlatform(
    const TaskNavigationAuthorityPlatform* platform) {
  CHECK(platform);
  CHECK(!PlatformSlot() || PlatformSlot() == platform);
  PlatformSlot() = platform;
}

const TaskNavigationAuthorityPlatform* GetTaskNavigationAuthorityPlatform() {
  return PlatformSlot();
}

ScopedTaskNavigationAuthorityPlatformForTesting::
    ScopedTaskNavigationAuthorityPlatformForTesting(
        const TaskNavigationAuthorityPlatform* platform)
    : platform_(platform), previous_(PlatformSlot()) {
  CHECK(platform_);
  PlatformSlot() = platform_;
}

ScopedTaskNavigationAuthorityPlatformForTesting::
    ~ScopedTaskNavigationAuthorityPlatformForTesting() {
  CHECK_EQ(PlatformSlot(), platform_);
  PlatformSlot() = previous_;
}

bool TaskNavigationAuthority::AllowsRequest(const GURL& address,
                                            bool is_redirect) const {
  return IsAllowed(VerdictForRequest(address, is_redirect));
}

// static
bool TaskNavigationAuthority::IsAllowed(RequestVerdict verdict) {
  switch (verdict) {
    case RequestVerdict::kAllowedExact:
    case RequestVerdict::kAllowedSameSiteHop:
    case RequestVerdict::kAllowedCrossSiteHop:
      return true;
    case RequestVerdict::kRefusedNotNormalized:
    case RequestVerdict::kRefusedFirstRequestNotExact:
    case RequestVerdict::kRefusedInsecureHop:
    case RequestVerdict::kRefusedOpaqueLanding:
    case RequestVerdict::kRefusedRestrictedClass:
    case RequestVerdict::kRefusedUnclassifiableLanding:
      return false;
  }
  return false;
}

TaskNavigationAuthority::RequestVerdict
TaskNavigationAuthority::VerdictForRequest(const GURL& address,
                                           bool is_redirect) const {
  if (!IsNormalizedHttpAddress(address)) {
    return RequestVerdict::kRefusedNotNormalized;
  }
  if (address == exact_destination_) {
    return RequestVerdict::kAllowedExact;
  }
  // A site answers a request at a host of its own choosing inside its own
  // registrable domain: an apex that sends you to `www`, a regional host, a
  // path the address omitted. Decision 0155 already calls that the site
  // answering, and `CheckCommittedNavigation` is satisfied by it - but this
  // throttle cancelled the request before the postcondition could ever see it,
  // so a followed link to a site that redirects at all never committed and the
  // task waited out its ten seconds and was told the step did not happen. Two
  // copies of one rule that disagreed; they agree now (decision 0177). A
  // cross-site hop and an http hop are still cancelled here, which is the
  // class boundary and not a detail.
  if (!is_redirect) {
    return RequestVerdict::kRefusedFirstRequestNotExact;
  }
  if (!address.SchemeIs(url::kHttpsScheme)) {
    return RequestVerdict::kRefusedInsecureHop;
  }
  if (exact_destination_.SchemeIs(url::kHttpsScheme) &&
      address.EffectiveIntPort() == exact_destination_.EffectiveIntPort() &&
      net::registry_controlled_domains::SameDomainOrHost(
          address, exact_destination_,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return RequestVerdict::kAllowedSameSiteHop;
  }
  // And a search engine answers a result through a redirector of its own, so
  // a hop to another site is the ordinary way a followed link reaches the
  // site it names. Cancelling it meant a task could never follow a search
  // result at all - the one move decision 0144 exists for - and the errand
  // waited out its deadline and was told the step did not happen.
  //
  // Where such a hop may land is still decided, by the same three things that
  // decide it anywhere else: the compiled destination-class table refuses the
  // classes this product never acts on for a person; the navigate's
  // postcondition contradicts a landing its proposal did not name, so nothing
  // is reported verified; and the tab is offered as a fresh source under the
  // sites budget rather than inheriting the one it left. This throttle binds
  // the request to an address the task may reach, not to the one address it
  // asked for (decision 0177).
  const url::Origin landing = url::Origin::Create(address);
  if (landing.opaque()) {
    return RequestVerdict::kRefusedOpaqueLanding;
  }
  switch (ClassifyRestrictedDestination(Origin{
      .kind = OriginKind::kTuple,
      .serialization = landing.Serialize(),
  })) {
    case RestrictedDestinationStatus::kNotListed:
      return RequestVerdict::kAllowedCrossSiteHop;
    case RestrictedDestinationStatus::kRestricted:
      return RequestVerdict::kRefusedRestrictedClass;
    case RestrictedDestinationStatus::kInvalidOrigin:
      return RequestVerdict::kRefusedUnclassifiableLanding;
  }
  return RequestVerdict::kRefusedUnclassifiableLanding;
}

// static
const char* TaskNavigationAuthority::NameOfVerdict(RequestVerdict verdict) {
  switch (verdict) {
    case RequestVerdict::kAllowedExact:
      return "exact";
    case RequestVerdict::kAllowedSameSiteHop:
      return "same-site-hop";
    case RequestVerdict::kAllowedCrossSiteHop:
      return "cross-site-hop";
    case RequestVerdict::kRefusedNotNormalized:
      return "address-not-normalized";
    case RequestVerdict::kRefusedFirstRequestNotExact:
      return "first-request-not-exact";
    case RequestVerdict::kRefusedInsecureHop:
      return "hop-left-https";
    case RequestVerdict::kRefusedOpaqueLanding:
      return "landing-origin-opaque";
    case RequestVerdict::kRefusedRestrictedClass:
      return "landing-in-restricted-class";
    case RequestVerdict::kRefusedUnclassifiableLanding:
      return "landing-not-classifiable";
  }
  return "unknown";
}

}  // namespace taffy
