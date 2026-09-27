// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MANAGED_ROUTE_CONFIGURATION_H_
#define TAFFY_BROWSER_MANAGED_ROUTE_CONFIGURATION_H_

#include <string_view>

namespace taffy {

enum class ManagedRouteConfigurationStatus {
  // Empty is an intentional build-time disable: nothing mints, and a managed
  // effect is refused against an origin that does not exist.
  kDisabled,
  kReady,
  kInvalidOrigin,
};

// Validates the build-owned managed-worker origin. Runtime/UI input is never
// accepted, and neither is a served catalog's: the one origin the managed
// route speaks to is a compile-time fact of the binary (decision 0082).
ManagedRouteConfigurationStatus
ValidateManagedRouteConfiguration(std::string_view origin);

// Whether `endpoint` names exactly the configured managed origin. False for
// every endpoint when the configuration is disabled or invalid — a
// fail-closed answer, because the caller is deciding whether to attach a
// bearer token. The configured value is the compiled GN argument at every
// production call site; it arrives as a parameter so the check is a pure
// function a test can drive.
bool IsManagedOriginEndpoint(std::string_view configured_origin,
                             std::string_view endpoint);

} // namespace taffy

#endif // TAFFY_BROWSER_MANAGED_ROUTE_CONFIGURATION_H_
