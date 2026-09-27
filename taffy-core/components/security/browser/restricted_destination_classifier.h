// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_SECURITY_BROWSER_RESTRICTED_DESTINATION_CLASSIFIER_H_
#define TAFFY_COMPONENTS_SECURITY_BROWSER_RESTRICTED_DESTINATION_CLASSIFIER_H_

#include "taffy/common/public/bip_identity.h"

namespace taffy {

// Result of consulting the product's compiled destination-class authority.
// kNotListed is intentionally narrow: it says only that this table has no
// matching row, never that another policy considers the destination safe.
enum class RestrictedDestinationStatus {
  kNotListed,
  kRestricted,
  kInvalidOrigin,
};

RestrictedDestinationStatus ClassifyRestrictedDestination(
    const Origin& origin);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_SECURITY_BROWSER_RESTRICTED_DESTINATION_CLASSIFIER_H_
