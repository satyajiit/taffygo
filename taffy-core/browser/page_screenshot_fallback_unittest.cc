// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_screenshot_fallback.h"

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

ObservationEnvelope BaseObservation() {
  ObservationEnvelope value;
  value.code = ObservationResultCode::kIncomplete;
  value.node_count = 2u;
  value.warning_codes = {
      static_cast<uint8_t>(mojom::WarningCode::kCanvasWithoutSemantics)};
  FrameSummary frame;
  frame.is_main_frame = true;
  value.frames.push_back(frame);
  value.adapters = {
      AdapterReport{.adapter = AdapterKind::kDom,
                    .status = AdapterStatus::kOk},
      AdapterReport{.adapter = AdapterKind::kAccessibility,
                    .status = AdapterStatus::kOk},
      AdapterReport{.adapter = AdapterKind::kForms,
                    .status = AdapterStatus::kUnsupported},
  };
  return value;
}

TEST(PageScreenshotFallbackTest, AdmitsBoundedCanvasOnlyObservation) {
  EXPECT_TRUE(PageScreenshotFallbackIsEligible(BaseObservation()));
}

TEST(PageScreenshotFallbackTest, AdmitsPageWithNoAvailableSemanticAdapter) {
  ObservationEnvelope value = BaseObservation();
  value.node_count = 0u;
  value.warning_codes.clear();
  for (AdapterReport& report : value.adapters) {
    report.status = AdapterStatus::kUnsupported;
  }
  EXPECT_TRUE(PageScreenshotFallbackIsEligible(value));
}

TEST(PageScreenshotFallbackTest, RefusesUsableStructureOrForm) {
  ObservationEnvelope value = BaseObservation();
  value.node_count = 3u;
  EXPECT_FALSE(PageScreenshotFallbackIsEligible(value));

  value = BaseObservation();
  value.adapters.back().status = AdapterStatus::kIncomplete;
  EXPECT_FALSE(PageScreenshotFallbackIsEligible(value));
}

TEST(PageScreenshotFallbackTest, RefusesPrivateTruncatedAndFramedPages) {
  ObservationEnvelope value = BaseObservation();
  value.is_incognito = true;
  EXPECT_FALSE(PageScreenshotFallbackIsEligible(value));

  value = BaseObservation();
  value.truncation.truncated = true;
  EXPECT_FALSE(PageScreenshotFallbackIsEligible(value));

  value = BaseObservation();
  value.frames.push_back(FrameSummary{});
  EXPECT_FALSE(PageScreenshotFallbackIsEligible(value));
}

TEST(PageScreenshotFallbackTest, ValidatesCaptureProvenanceAndRedaction) {
  ObservationEnvelope observation = BaseObservation();
  observation.capture_time_monotonic_ms = 40u;
  observation.redaction.sensitive_zone_count = 1u;
  auto media = core_service::mojom::MediaObservationResult::New();
  media->kind = core_service::mojom::MediaObservationKind::kPageScreenshot;
  media->attachment_handle = "media-handle";
  media->attachment_mime_type = "image/png";
  media->width_px = 960u;
  media->height_px = 480u;
  media->capture_provenance =
      core_service::mojom::MediaCaptureProvenance::New();
  media->capture_provenance->capture_width_dip = 2000u;
  media->capture_provenance->capture_height_dip = 1000u;
  media->capture_provenance->viewport_width_dip = 2000u;
  media->capture_provenance->viewport_height_dip = 1000u;
  media->capture_provenance->output_scale_ppm = 480000u;
  media->capture_provenance->captured_at_monotonic_ms = 41u;
  media->capture_provenance->redacted_region_count = 1u;

  EXPECT_TRUE(PageScreenshotResultMatchesObservation(observation, *media));
  media->capture_provenance->redacted_region_count = 0u;
  EXPECT_FALSE(PageScreenshotResultMatchesObservation(observation, *media));
  media->capture_provenance->redacted_region_count = 1u;
  media->capture_provenance->capture_width_dip = 1999u;
  EXPECT_FALSE(PageScreenshotResultMatchesObservation(observation, *media));
}

}  // namespace
}  // namespace taffy
