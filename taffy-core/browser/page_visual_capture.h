// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_VISUAL_CAPTURE_H_
#define TAFFY_BROWSER_PAGE_VISUAL_CAPTURE_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

class SkBitmap;

namespace taffy {

// Browser-owned, bounded compositor geometry. Redactions are already clipped
// to the selected region and transformed into output-pixel coordinates.
struct PageVisualCapturePlan {
  gfx::Rect capture_bounds;
  gfx::Size output_size;
  std::vector<gfx::Rect> output_redactions;
  uint32_t output_scale_ppm = 0u;
};

std::optional<PageVisualCapturePlan> BuildPageVisualCapturePlan(
    const gfx::Size& viewport,
    gfx::Rect requested_bounds,
    const std::vector<gfx::Rect>& viewport_redactions,
    int maximum_output_dimension);

// Paints every output rectangle opaque black. Invalid geometry or an
// immutable/non-pixel bitmap is a refusal, never an unredacted success.
bool ApplyPageVisualRedactions(
    SkBitmap* bitmap,
    const std::vector<gfx::Rect>& output_redactions);

std::optional<std::vector<uint8_t>> EncodePageVisualPng(
    SkBitmap bitmap,
    std::vector<gfx::Rect> output_redactions);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_VISUAL_CAPTURE_H_
