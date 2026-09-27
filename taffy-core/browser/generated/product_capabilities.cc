// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from //taffy/build/product-capabilities.json. Do not edit.

#include "taffy/browser/generated/product_capabilities.h"

namespace taffy::product_capabilities {

const Profile& Active() {
#if defined(TAFFY_CAPABILITY_PROFILE_CANDIDATE) && \
    defined(TAFFY_CAPABILITY_PROFILE_DEVELOPMENT)
#error "exactly one TaffyGo capability profile must be selected"
#elif defined(TAFFY_CAPABILITY_PROFILE_CANDIDATE)
  return kCandidate;
#elif defined(TAFFY_CAPABILITY_PROFILE_DEVELOPMENT)
  return kDevelopment;
#else
#error "a TaffyGo capability profile must be selected by the GN projection"
#endif
}

}  // namespace taffy::product_capabilities
