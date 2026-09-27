// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_visual_capture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/gfx/codec/png_codec.h"

namespace taffy {
namespace {

int ScaleFloor(int value, int output, int input) {
  return static_cast<int>((static_cast<int64_t>(value) * output) / input);
}

int ScaleCeil(int value, int output, int input) {
  return static_cast<int>((static_cast<int64_t>(value) * output + input - 1) /
                          input);
}

}  // namespace

std::optional<PageVisualCapturePlan> BuildPageVisualCapturePlan(
    const gfx::Size& viewport,
    gfx::Rect requested_bounds,
    const std::vector<gfx::Rect>& viewport_redactions,
    int maximum_output_dimension) {
  if (viewport.IsEmpty() || requested_bounds.IsEmpty() ||
      maximum_output_dimension <= 0) {
    return std::nullopt;
  }
  requested_bounds.Intersect(gfx::Rect(viewport));
  if (requested_bounds.IsEmpty()) {
    return std::nullopt;
  }
  const double scale =
      std::min({1.0,
                static_cast<double>(maximum_output_dimension) /
                    requested_bounds.width(),
                static_cast<double>(maximum_output_dimension) /
                    requested_bounds.height()});
  PageVisualCapturePlan plan;
  plan.capture_bounds = requested_bounds;
  plan.output_size = gfx::Size(
      std::max(1, static_cast<int>(std::floor(requested_bounds.width() * scale))),
      std::max(1,
               static_cast<int>(std::floor(requested_bounds.height() * scale))));
  const uint64_t x_scale =
      static_cast<uint64_t>(plan.output_size.width()) * 1'000'000u /
      static_cast<uint32_t>(requested_bounds.width());
  const uint64_t y_scale =
      static_cast<uint64_t>(plan.output_size.height()) * 1'000'000u /
      static_cast<uint32_t>(requested_bounds.height());
  plan.output_scale_ppm =
      static_cast<uint32_t>(std::min<uint64_t>({1'000'000u, x_scale, y_scale}));

  for (gfx::Rect redaction : viewport_redactions) {
    redaction.Intersect(requested_bounds);
    if (redaction.IsEmpty()) {
      continue;
    }
    redaction.Offset(-requested_bounds.x(), -requested_bounds.y());
    const int left = ScaleFloor(redaction.x(), plan.output_size.width(),
                                requested_bounds.width());
    const int top = ScaleFloor(redaction.y(), plan.output_size.height(),
                               requested_bounds.height());
    const int right = ScaleCeil(redaction.right(), plan.output_size.width(),
                                requested_bounds.width());
    const int bottom = ScaleCeil(redaction.bottom(), plan.output_size.height(),
                                 requested_bounds.height());
    gfx::Rect output(left, top, right - left, bottom - top);
    output.Intersect(gfx::Rect(plan.output_size));
    if (!output.IsEmpty()) {
      plan.output_redactions.push_back(output);
    }
  }
  return plan;
}

bool ApplyPageVisualRedactions(
    SkBitmap* bitmap,
    const std::vector<gfx::Rect>& output_redactions) {
  if (!bitmap || bitmap->drawsNothing() || !bitmap->getPixels() ||
      bitmap->isImmutable()) {
    return false;
  }
  SkCanvas canvas(*bitmap);
  SkPaint paint;
  paint.setColor(SK_ColorBLACK);
  paint.setBlendMode(SkBlendMode::kSrc);
  for (const gfx::Rect& redaction : output_redactions) {
    if (redaction.IsEmpty() || redaction.x() < 0 || redaction.y() < 0 ||
        redaction.right() > bitmap->width() ||
        redaction.bottom() > bitmap->height()) {
      return false;
    }
    canvas.drawIRect(SkIRect::MakeXYWH(redaction.x(), redaction.y(),
                                      redaction.width(), redaction.height()),
                     paint);
  }
  return true;
}

std::optional<std::vector<uint8_t>> EncodePageVisualPng(
    SkBitmap bitmap,
    std::vector<gfx::Rect> output_redactions) {
  if (bitmap.drawsNothing()) {
    return std::nullopt;
  }
  if (!output_redactions.empty() && bitmap.isImmutable()) {
    SkBitmap mutable_bitmap;
    if (!mutable_bitmap.tryAllocPixels(bitmap.info()) ||
        !bitmap.readPixels(mutable_bitmap.pixmap())) {
      return std::nullopt;
    }
    bitmap = std::move(mutable_bitmap);
  }
  if (!output_redactions.empty() &&
      !ApplyPageVisualRedactions(&bitmap, output_redactions)) {
    return std::nullopt;
  }
  bitmap.setImmutable();
  return gfx::PNGCodec::FastEncodeBGRASkBitmap(bitmap,
                                               /*discard_transparency=*/false);
}

}  // namespace taffy
