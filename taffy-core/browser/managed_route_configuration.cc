// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/managed_route_configuration.h"

#include <string_view>

#include "url/gurl.h"

namespace taffy {

ManagedRouteConfigurationStatus
ValidateManagedRouteConfiguration(std::string_view origin) {
  if (origin.empty()) {
    return ManagedRouteConfigurationStatus::kDisabled;
  }
  const GURL parsed(origin);
  // The same shape the account plane's origin is held to: a bare https
  // origin, one spelling, nothing that could smuggle a path, an identity or
  // a query into every managed request.
  if (!parsed.is_valid() || !parsed.SchemeIs("https") ||
      parsed.has_username() || parsed.has_password() || parsed.has_query() ||
      parsed.has_ref() ||
      (!parsed.path().empty() && parsed.path() != "/")) {
    return ManagedRouteConfigurationStatus::kInvalidOrigin;
  }
  return ManagedRouteConfigurationStatus::kReady;
}

bool IsManagedOriginEndpoint(std::string_view configured_origin,
                             std::string_view endpoint) {
  if (ValidateManagedRouteConfiguration(configured_origin) !=
      ManagedRouteConfigurationStatus::kReady) {
    return false;
  }
  const GURL configured(configured_origin);
  const GURL candidate(endpoint);
  return candidate.is_valid() &&
         candidate.DeprecatedGetOriginAsURL() ==
             configured.DeprecatedGetOriginAsURL();
}

} // namespace taffy
