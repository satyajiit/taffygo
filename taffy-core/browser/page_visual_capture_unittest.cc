// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_visual_capture.h"

#include <utility>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"

namespace taffy {
namespace {

TEST(PageVisualCaptureTest, ClipsDownscalesAndRoundsRedactionsOutward) {
  const auto plan = BuildPageVisualCapturePlan(
      gfx::Size(2000, 1000), gfx::Rect(-10, 0, 2010, 1000),
      {gfx::Rect(999, 100, 3, 3), gfx::Rect(2200, 0, 10, 10)}, 960);

  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->capture_bounds, gfx::Rect(0, 0, 2000, 1000));
  EXPECT_EQ(plan->output_size, gfx::Size(960, 480));
  EXPECT_EQ(plan->output_scale_ppm, 480000u);
  ASSERT_EQ(plan->output_redactions.size(), 1u);
  EXPECT_EQ(plan->output_redactions.front(), gfx::Rect(479, 48, 2, 2));
}

TEST(PageVisualCaptureTest, RefusesEmptyOrDisjointCapture) {
  EXPECT_FALSE(BuildPageVisualCapturePlan(gfx::Size(), gfx::Rect(0, 0, 1, 1),
                                          {}, 960));
  EXPECT_FALSE(BuildPageVisualCapturePlan(gfx::Size(10, 10),
                                          gfx::Rect(20, 20, 4, 4), {}, 960));
}

TEST(PageVisualCaptureTest, PaintsSecretRegionsOpaqueBlack) {
  SkBitmap bitmap;
  ASSERT_TRUE(bitmap.tryAllocN32Pixels(8, 8));
  bitmap.eraseColor(SK_ColorWHITE);

  ASSERT_TRUE(ApplyPageVisualRedactions(&bitmap, {gfx::Rect(2, 3, 3, 2)}));
  EXPECT_EQ(bitmap.getColor(2, 3), SK_ColorBLACK);
  EXPECT_EQ(bitmap.getColor(4, 4), SK_ColorBLACK);
  EXPECT_EQ(bitmap.getColor(1, 3), SK_ColorWHITE);
}

TEST(PageVisualCaptureTest, RefusesOutOfBoundsRedaction) {
  SkBitmap bitmap;
  ASSERT_TRUE(bitmap.tryAllocN32Pixels(4, 4));
  bitmap.eraseColor(SK_ColorWHITE);
  EXPECT_FALSE(
      ApplyPageVisualRedactions(&bitmap, {gfx::Rect(3, 3, 2, 2)}));
}

TEST(PageVisualCaptureTest, EncodingCopiesImmutableSurfaceBeforeRedacting) {
  SkBitmap bitmap;
  ASSERT_TRUE(bitmap.tryAllocN32Pixels(4, 4));
  bitmap.eraseColor(SK_ColorWHITE);
  bitmap.setImmutable();

  const auto encoded =
      EncodePageVisualPng(std::move(bitmap), {gfx::Rect(1, 1, 2, 2)});
  ASSERT_TRUE(encoded.has_value());
  EXPECT_FALSE(encoded->empty());
}

}  // namespace
}  // namespace taffy
