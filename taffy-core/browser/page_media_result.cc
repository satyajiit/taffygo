// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_media_result.h"

#include "base/notreached.h"

namespace taffy {

namespace core_mojom = core_service::mojom;
constexpr size_t kMaximumVisualRedactionBounds = 128u;

core_mojom::MediaObservationKind RenderedMediaKind(
    PageMediaObservationKind kind) {
  switch (kind) {
    case PageMediaObservationKind::kImage:
      return core_mojom::MediaObservationKind::kImage;
    case PageMediaObservationKind::kVideo:
      return core_mojom::MediaObservationKind::kVideo;
    case PageMediaObservationKind::kPageScreenshot:
      return core_mojom::MediaObservationKind::kPageScreenshot;
    case PageMediaObservationKind::kPdf:
      NOTREACHED();
  }
}

std::vector<gfx::Rect> MediaTargetRedactionBounds(
    const mojom::MediaTargetResult& result) {
  std::vector<gfx::Rect> bounds;
  bounds.reserve(result.redaction_bounds.size());
  for (const mojom::BoundsPtr& value : result.redaction_bounds) {
    if (value) {
      bounds.emplace_back(value->x, value->y, value->width, value->height);
    }
  }
  return bounds;
}

bool MediaTargetRedactionsAreValid(const mojom::MediaTargetResult& result,
                                   bool allow_redactions) {
  if (!result.bounds ||
      (!allow_redactions && !result.redaction_bounds.empty()) ||
      result.redaction_bounds.size() > kMaximumVisualRedactionBounds) {
    return false;
  }
  for (const mojom::BoundsPtr& value : result.redaction_bounds) {
    if (!value || value->x < 0 || value->y < 0 || value->width <= 0 ||
        value->height <= 0 ||
        static_cast<int64_t>(value->x) + value->width >
            result.bounds->width ||
        static_cast<int64_t>(value->y) + value->height >
            result.bounds->height) {
      return false;
    }
  }
  return true;
}

core_mojom::MediaCaptureProvenancePtr MakeCaptureProvenance(
    const gfx::Rect& capture_bounds,
    const gfx::Size& viewport_size,
    uint32_t output_scale_ppm,
    uint64_t captured_at_monotonic_ms,
    uint32_t redacted_region_count) {
  auto provenance = core_mojom::MediaCaptureProvenance::New();
  provenance->capture_x_dip = static_cast<uint32_t>(capture_bounds.x());
  provenance->capture_y_dip = static_cast<uint32_t>(capture_bounds.y());
  provenance->capture_width_dip =
      static_cast<uint32_t>(capture_bounds.width());
  provenance->capture_height_dip =
      static_cast<uint32_t>(capture_bounds.height());
  provenance->viewport_width_dip = static_cast<uint32_t>(viewport_size.width());
  provenance->viewport_height_dip =
      static_cast<uint32_t>(viewport_size.height());
  provenance->output_scale_ppm = output_scale_ppm;
  provenance->captured_at_monotonic_ms = captured_at_monotonic_ms;
  provenance->redacted_region_count = redacted_region_count;
  return provenance;
}

}  // namespace taffy
