// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSET_DELIVERY_CONFIGURATION_H_
#define TAFFY_BROWSER_ASSET_DELIVERY_CONFIGURATION_H_

#include <string>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "url/gurl.h"

namespace taffy {

// Whether this build can fetch the product's own artifacts, and if not, why.
enum class AssetDeliveryConfigurationStatus {
  // An origin is configured and well-formed.
  kReady,
  // No origin is configured. Every fetch refuses; nothing is broken.
  kAbsent,
  // An origin is configured and is not one.
  kMalformedOrigin,
};

// Judges the compiled-in delivery origin.
//
// GN already refused a malformed origin before the graph existed. This is the
// second of the two checks, and it exists because the first one runs on a
// builder and this one runs on the device: a build argument reaches the binary
// as a string, and a string that was validated once by a different program is a
// string this process has not validated.
AssetDeliveryConfigurationStatus ValidateAssetDeliveryConfiguration(
    std::string_view origin);

// The absolute URL for one catalog-relative path, or an invalid `GURL`.
//
// Refused, rather than escaped: an empty segment, a `.` or `..` segment, a
// segment outside the catalog's alphabet, a leading or trailing separator, a
// scheme, a host, a query or a fragment. The catalog is generated from a source
// the same rules already checked, so a path that fails here is a path that
// reached this process from somewhere other than the catalog — which is the
// case worth refusing.
GURL AssetUrl(std::string_view origin, std::string_view catalog_path);

// The delivery origin this build was compiled with, which may be empty.
//
// Read here rather than at the call site so the build define stays inside the
// one target that carries the config that sets it. A caller that spelled the
// macro itself would compile only where that config happened to be attached.
std::string_view ConfiguredAssetOrigin();

// Which set of published artifacts this build may fetch.
//
// Derived from the build's own target rather than from anything the device
// reports, because the answer is a property of the binary: an arm64 build asks
// for arm64 artifacts wherever it is running. A target the catalog has no
// column for is not a failure — it is a device that fetches nothing, which is
// exactly what an unpublished row already means.
core_service::mojom::AssetPlatform CurrentAssetPlatform();

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSET_DELIVERY_CONFIGURATION_H_
