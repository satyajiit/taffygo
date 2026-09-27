// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/security/browser/restricted_destination_classifier.h"

#include <algorithm>

#include "taffy/components/security/browser/restricted_destination_table_generated.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

RestrictedDestinationStatus ClassifyRestrictedDestination(
    const Origin& origin) {
  if (origin.kind != OriginKind::kTuple || origin.serialization.empty() ||
      !origin.opaque_id.empty()) {
    return RestrictedDestinationStatus::kInvalidOrigin;
  }
  const GURL address(origin.serialization);
  const url::Origin normalized = url::Origin::Create(address);
  if (!address.is_valid() || !address.SchemeIsHTTPOrHTTPS() ||
      normalized.opaque() || normalized.Serialize() != origin.serialization) {
    return RestrictedDestinationStatus::kInvalidOrigin;
  }
  return std::ranges::binary_search(destination_class::kRestrictedHosts,
                                    address.host())
             ? RestrictedDestinationStatus::kRestricted
             : RestrictedDestinationStatus::kNotListed;
}

}  // namespace taffy
