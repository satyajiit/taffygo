// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_screenshot_fallback.h"

#include <algorithm>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

namespace taffy {
namespace {

const AdapterReport* ReportFor(const ObservationEnvelope& observation,
                               AdapterKind kind) {
  const auto found = std::ranges::find(observation.adapters, kind,
                                       &AdapterReport::adapter);
  return found == observation.adapters.end() ? nullptr : &*found;
}

bool IsUnavailable(const AdapterReport* report) {
  return !report || report->status == AdapterStatus::kUnsupported ||
         report->status == AdapterStatus::kFailed;
}

bool IsUsableFormReport(const AdapterReport* report) {
  return report && report->status != AdapterStatus::kUnsupported &&
         report->status != AdapterStatus::kFailed;
}

}  // namespace

bool PageScreenshotFallbackIsEligible(
    const ObservationEnvelope& observation) {
  if (observation.is_incognito || observation.truncation.truncated ||
      observation.truncation.may_change_answer ||
      observation.frames.size() != 1u ||
      !observation.frames.front().is_main_frame ||
      observation.frames.front().is_cross_origin_to_parent ||
      observation.redaction.policy_filtered_frame_count != 0u ||
      observation.node_count > 2u) {
    return false;
  }

  const AdapterReport* dom = ReportFor(observation, AdapterKind::kDom);
  const AdapterReport* accessibility =
      ReportFor(observation, AdapterKind::kAccessibility);
  const AdapterReport* forms = ReportFor(observation, AdapterKind::kForms);
  if (IsUsableFormReport(forms) ||
      (dom && dom->status == AdapterStatus::kConflicted) ||
      (accessibility && accessibility->status == AdapterStatus::kConflicted)) {
    return false;
  }

  const bool canvas_without_semantics = std::ranges::contains(
      observation.warning_codes,
      static_cast<uint8_t>(mojom::WarningCode::kCanvasWithoutSemantics));
  const bool every_primary_adapter_unavailable =
      IsUnavailable(dom) && IsUnavailable(accessibility) && IsUnavailable(forms);
  return canvas_without_semantics || every_primary_adapter_unavailable;
}

bool PageScreenshotResultMatchesObservation(
    const ObservationEnvelope& observation,
    const core_service::mojom::MediaObservationResult& media) {
  const core_service::mojom::MediaCaptureProvenance* capture =
      media.capture_provenance.get();
  if (media.kind != core_service::mojom::MediaObservationKind::kPageScreenshot ||
      !capture || !media.attachment_handle || media.attachment_handle->empty() ||
      media.attachment_handle->size() >
          core_service::mojom::kMaxMediaAttachmentHandleBytes ||
      !media.attachment_mime_type ||
      *media.attachment_mime_type != "image/png" ||
      media.width_px == 0u || media.height_px == 0u || !media.facts.empty() ||
      media.width_px > core_service::mojom::kMaxMediaDimensionPx ||
      media.height_px > core_service::mojom::kMaxMediaDimensionPx ||
      media.has_meaningful_text || media.scanned_pdf_ocr_required ||
      capture->capture_x_dip != 0u || capture->capture_y_dip != 0u ||
      capture->capture_width_dip == 0u || capture->capture_height_dip == 0u ||
      capture->capture_width_dip != capture->viewport_width_dip ||
      capture->capture_height_dip != capture->viewport_height_dip ||
      capture->output_scale_ppm == 0u ||
      capture->output_scale_ppm > 1'000'000u ||
      capture->captured_at_monotonic_ms == 0u ||
      capture->captured_at_monotonic_ms <
          observation.capture_time_monotonic_ms ||
      capture->redacted_region_count > 128u ||
      (observation.redaction.sensitive_zone_count > 0u &&
       capture->redacted_region_count == 0u)) {
    return false;
  }
  const uint64_t width_scale =
      static_cast<uint64_t>(media.width_px) * 1'000'000u /
      capture->capture_width_dip;
  const uint64_t height_scale =
      static_cast<uint64_t>(media.height_px) * 1'000'000u /
      capture->capture_height_dip;
  return capture->output_scale_ppm == std::min(width_scale, height_scale);
}

}  // namespace taffy
