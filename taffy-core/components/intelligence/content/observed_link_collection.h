// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVED_LINK_COLLECTION_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVED_LINK_COLLECTION_H_

#include <vector>

#include "taffy/common/public/bip_observation.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

namespace taffy {

// Extracts exact safe link destinations and their navigation flags into
// browser-only transient rows. The registry chooses which use is permitted;
// the BIP graph encoder intentionally omits these addresses.
// Duplicate node identities invalidate the whole collection: choosing one
// renderer row would turn process order into authority.
bool CollectTransientObservedLinks(
    const ObservationRequest& request,
    const mojom::PageSnapshot& snapshot,
    std::vector<TransientObservedLink>* observed_links);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVED_LINK_COLLECTION_H_
