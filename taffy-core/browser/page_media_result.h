// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_MEDIA_RESULT_H_
#define TAFFY_BROWSER_PAGE_MEDIA_RESULT_H_

#include <stdint.h>

#include <vector>

#include "taffy/browser/page_media_observation.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace taffy {

core_service::mojom::MediaObservationKind RenderedMediaKind(
    PageMediaObservationKind kind);

std::vector<gfx::Rect> MediaTargetRedactionBounds(
    const mojom::MediaTargetResult& result);

bool MediaTargetRedactionsAreValid(const mojom::MediaTargetResult& result,
                                   bool allow_redactions);

core_service::mojom::MediaCaptureProvenancePtr MakeCaptureProvenance(
    const gfx::Rect& capture_bounds,
    const gfx::Size& viewport_size,
    uint32_t output_scale_ppm,
    uint64_t captured_at_monotonic_ms,
    uint32_t redacted_region_count);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_MEDIA_RESULT_H_
