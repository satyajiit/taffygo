// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_VIDEO_CAPTION_FACTS_H_
#define TAFFY_BROWSER_PAGE_VIDEO_CAPTION_FACTS_H_

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

struct RescanTally;

// Revalidates, rescans and bounds renderer-supplied loaded caption cues into
// typed generated facts. False rejects the entire media result as malformed.
bool PopulateVideoCaptionFacts(
    const mojom::MediaTargetResult& inspected,
    core_service::mojom::MediaObservationResult& media,
    RescanTally* rescan);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_VIDEO_CAPTION_FACTS_H_
