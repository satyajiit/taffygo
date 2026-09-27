// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bool ValidIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

bool CopyMediaObservation(const mojom::TaskActionOperationKind operation,
                          const mojom::ObservationEffectResult& observation,
                          bridge::BridgeTaskTerminal& out) {
  const bool image =
      operation == mojom::TaskActionOperationKind::kImageDescribe ||
      operation == mojom::TaskActionOperationKind::kImageReadText;
  const bool video = operation == mojom::TaskActionOperationKind::kVideoInspect;
  const bool pdf = operation == mojom::TaskActionOperationKind::kPdfInspect;
  const bool page_screenshot =
      operation == mojom::TaskActionOperationKind::kPageScreenshotInspect;
  const bool expected = image || video || pdf || page_screenshot;
  if (expected != !observation.media.is_null()) {
    return false;
  }
  if (!expected) {
    return true;
  }
  const mojom::MediaObservationResult& media = *observation.media;
  if ((image && media.kind != mojom::MediaObservationKind::kImage) ||
      (video && media.kind != mojom::MediaObservationKind::kVideo) ||
      (pdf && media.kind != mojom::MediaObservationKind::kPdf) ||
      (page_screenshot &&
       media.kind != mojom::MediaObservationKind::kPageScreenshot) ||
      media.facts.size() > mojom::kMaxMediaFacts ||
      media.width_px > mojom::kMaxMediaDimensionPx ||
      media.height_px > mojom::kMaxMediaDimensionPx ||
      (media.scanned_pdf_ocr_required && (!pdf || media.has_meaningful_text)) ||
      (page_screenshot && (!media.facts.empty() || media.has_meaningful_text ||
                           media.scanned_pdf_ocr_required))) {
    return false;
  }
  const bool has_handle = media.attachment_handle.has_value();
  const bool has_mime = media.attachment_mime_type.has_value();
  if (has_handle != has_mime ||
      (has_handle != (media.width_px != 0u && media.height_px != 0u)) ||
      ((image || video || page_screenshot) && !has_handle) ||
      (pdf && has_handle) ||
      (has_handle && (media.attachment_handle->empty() ||
                      media.attachment_handle->size() >
                          mojom::kMaxMediaAttachmentHandleBytes ||
                      *media.attachment_mime_type != "image/png"))) {
    return false;
  }
  const mojom::MediaCaptureProvenance* capture = media.capture_provenance.get();
  if (page_screenshot != (capture != nullptr)) {
    return false;
  }
  if (capture &&
      (capture->capture_x_dip != 0u || capture->capture_y_dip != 0u ||
       capture->capture_width_dip == 0u || capture->capture_height_dip == 0u ||
       capture->capture_width_dip != capture->viewport_width_dip ||
       capture->capture_height_dip != capture->viewport_height_dip ||
       capture->output_scale_ppm == 0u ||
       capture->output_scale_ppm > 1000000u ||
       capture->captured_at_monotonic_ms == 0u ||
       capture->redacted_region_count > 128u)) {
    return false;
  }
  if (capture) {
    const uint64_t width_scale = static_cast<uint64_t>(media.width_px) *
                                 1000000u / capture->capture_width_dip;
    const uint64_t height_scale = static_cast<uint64_t>(media.height_px) *
                                  1000000u / capture->capture_height_dip;
    if (capture->output_scale_ppm != std::min(width_scale, height_scale)) {
      return false;
    }
  }

  size_t total_fact_bytes = 0u;
  for (const mojom::MediaObservationFactPtr& fact : media.facts) {
    if (!fact || fact->text.empty() ||
        fact->text.size() > mojom::kMaxMediaFactTextBytes ||
        fact->source_locator.empty() ||
        fact->source_locator.size() > mojom::kMaxMediaFactLocatorBytes ||
        fact->source_end < fact->source_start ||
        fact->timestamp_end_ms < fact->timestamp_start_ms ||
        fact->confidence_ppm > 1000000u ||
        fact->page_index_plus_one > mojom::kMaxMediaPdfPages ||
        fact->text.size() > mojom::kMaxMediaFactTotalBytes - total_fact_bytes) {
      return false;
    }
    total_fact_bytes += fact->text.size();
    if (fact->source_locator.size() >
        mojom::kMaxMediaFactTotalBytes - total_fact_bytes) {
      return false;
    }
    total_fact_bytes += fact->source_locator.size();
    bridge::BridgeMediaObservationFact projected;
    projected.kind = static_cast<uint8_t>(fact->kind);
    projected.evidence = static_cast<uint8_t>(fact->evidence);
    projected.text = fact->text;
    projected.source_locator = fact->source_locator;
    projected.source_start = fact->source_start;
    projected.source_end = fact->source_end;
    projected.page_index_plus_one = fact->page_index_plus_one;
    projected.timestamp_start_ms = fact->timestamp_start_ms;
    projected.timestamp_end_ms = fact->timestamp_end_ms;
    projected.row_index_plus_one = fact->row_index_plus_one;
    projected.confidence_ppm = fact->confidence_ppm;
    projected.truncated = fact->truncated;
    out.media_observation_facts.push_back(std::move(projected));
  }
  out.has_media_observation = true;
  out.media_observation_kind = static_cast<uint8_t>(media.kind);
  out.has_media_attachment = has_handle;
  if (has_handle) {
    out.media_attachment_handle = *media.attachment_handle;
    out.media_attachment_mime_type = *media.attachment_mime_type;
  }
  out.media_attachment_width_px = media.width_px;
  out.media_attachment_height_px = media.height_px;
  out.media_has_meaningful_text = media.has_meaningful_text;
  out.media_scanned_pdf_ocr_required = media.scanned_pdf_ocr_required;
  out.has_media_capture_provenance = capture != nullptr;
  if (capture) {
    out.media_capture_x_dip = capture->capture_x_dip;
    out.media_capture_y_dip = capture->capture_y_dip;
    out.media_capture_width_dip = capture->capture_width_dip;
    out.media_capture_height_dip = capture->capture_height_dip;
    out.media_viewport_width_dip = capture->viewport_width_dip;
    out.media_viewport_height_dip = capture->viewport_height_dip;
    out.media_output_scale_ppm = capture->output_scale_ppm;
    out.media_captured_at_monotonic_ms = capture->captured_at_monotonic_ms;
    out.media_redacted_region_count = capture->redacted_region_count;
  }
  return true;
}

}  // namespace

bool CopyTaskActionCompletion(const mojom::TaskEffectBinding& effect,
                              const mojom::TaskEffectCompletion& completion,
                              bridge::BridgeTaskTerminal& out) {
  if (!effect.action || !effect.action->executable) {
    return false;
  }
  const auto operation = effect.action->executable->operation_kind;
  if (operation == mojom::TaskActionOperationKind::kTabsList ||
      operation == mojom::TaskActionOperationKind::kTabsActivate ||
      operation == mojom::TaskActionOperationKind::kTabsClose) {
    return CopyTaskTabCompletion(effect, completion, out);
  }
  if (operation == mojom::TaskActionOperationKind::kDownloadStart ||
      operation == mojom::TaskActionOperationKind::kDownloadList ||
      operation == mojom::TaskActionOperationKind::kDownloadCancel) {
    return CopyTaskDownloadCompletion(effect, completion, out);
  }
  if (operation == mojom::TaskActionOperationKind::kHistorySearch ||
      operation == mojom::TaskActionOperationKind::kHistoryRecent ||
      operation == mojom::TaskActionOperationKind::kBookmarksSearch ||
      operation == mojom::TaskActionOperationKind::kBookmarksList ||
      operation == mojom::TaskActionOperationKind::kOpenTabsList) {
    return CopyTaskStoreCompletion(effect, completion, out);
  }
  switch (effect.action->executable->action_class) {
    case mojom::PolicyActionClass::kObservePage: {
      // A read the browser refused before any renderer saw it has no
      // observation to carry its reason, and `BipObservationStatus` has no
      // word for most of them: a frame that changed, an origin that changed,
      // a document the browser knows has moved. Those are
      // `TaskActionResultCode` members, so such a refusal names one the way
      // every other refused action does. Without this the gate sent a bare
      // refusal, which the bridge reads as the generic policy denial —
      // `DoNotRetry` for a page that had simply settled under it
      // (decision 0207).
      if (completion.effect_result &&
          completion.effect_result->kind == mojom::EffectKind::kBrowserAction) {
        return CopyRefusedActionTerminal(effect, completion, out);
      }
      if (!completion.effect_result || !completion.effect_result->operation ||
          !SameOperationEnvelope(*effect.operation,
                         *completion.effect_result->operation) ||
          completion.effect_result->effect_id != effect.effect_id ||
          completion.effect_result->kind !=
              mojom::EffectKind::kPageObservation ||
          !completion.effect_result->observation) {
        return false;
      }
      if (completion.effect_result->status != mojom::EffectStatus::kCompleted) {
        return CopyRefusedObservationTerminal(effect, completion, out);
      }
      const auto& observation = completion.effect_result->observation;
      out.has_observation = true;
      out.observation_status = static_cast<uint8_t>(observation->status);
      out.observation_schema_version = observation->schema_version;
      out.observation_tab_id = observation->tab_id;
      out.observation_frame_id = observation->frame_id;
      out.observation_page_epoch = observation->page_epoch;
      out.observation_graph_revision = observation->graph_revision;
      out.observation_origin = observation->origin;
      out.observation_is_potentially_trustworthy =
          observation->is_potentially_trustworthy;
      out.observation_private_profile = observation->private_profile;
      out.observation_node_count = observation->node_count;
      out.observation_total_bytes = observation->total_bytes;
      out.observation_truncated = observation->truncated;
      out.observation_may_change_answer = observation->may_change_answer;
      out.observation_redacted_field_count = observation->redacted_field_count;
      out.observation_suppressed_secret_value_count =
          observation->suppressed_secret_value_count;
      out.observation_sensitive_zone_count = observation->sensitive_zone_count;
      out.observation_policy_filtered_frame_count =
          observation->policy_filtered_frame_count;
      out.observation_highest_sensitivity =
          static_cast<uint8_t>(observation->highest_sensitivity);
      out.observation_graph_encoding =
          static_cast<uint8_t>(observation->graph_encoding);
      out.observation_graph_payload.reserve(observation->graph_payload.size());
      for (uint8_t byte : observation->graph_payload) {
        out.observation_graph_payload.push_back(byte);
      }
      return CopyMediaObservation(effect.action->executable->operation_kind,
                                  *observation, out);
    }
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kScrollIntoView:
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kControlTab: {
      const bool discovery_navigation =
          (effect.action->executable->operation_kind ==
               mojom::TaskActionOperationKind::kSearch ||
           effect.action->executable->operation_kind ==
               mojom::TaskActionOperationKind::kNavigate) &&
          effect.action->document &&
          effect.action->document->opaque_origin_id.has_value() &&
          effect.action->document->normalized_origin.empty();
      if (!completion.effect_result) {
        return !discovery_navigation;
      }
      const auto& result = completion.effect_result;
      if (!result->operation ||
          !SameOperationEnvelope(*effect.operation, *result->operation) ||
          result->effect_id != effect.effect_id ||
          result->kind != mojom::EffectKind::kBrowserAction ||
          result->status != mojom::EffectStatus::kCompleted ||
          !result->browser_action ||
          result->browser_action->outcome !=
              mojom::BrowserActionOutcome::kCompleted ||
          !result->browser_action->dispatch_id ||
          *result->browser_action->dispatch_id != effect.action->dispatch_id ||
          result->browser_action->discovery_tab_id ||
          result->browser_action->browser_session_id ||
          result->browser_action->task_tab ||
          result->browser_action->task_download ||
          result->browser_action->task_store) {
        return false;
      }
      const auto& source = result->browser_action->discovered_source;
      if (discovery_navigation && !source) {
        return false;
      }
      if (source) {
        const bool new_tab = effect.action->executable->operation_kind ==
                             mojom::TaskActionOperationKind::kTabsOpen;
        const bool may_discover =
            new_tab ||
            effect.action->executable->operation_kind ==
                mojom::TaskActionOperationKind::kNavigate ||
            effect.action->executable->operation_kind ==
                mojom::TaskActionOperationKind::kSearch ||
            effect.action->executable->operation_kind ==
                mojom::TaskActionOperationKind::kHistoryBack ||
            effect.action->executable->operation_kind ==
                mojom::TaskActionOperationKind::kHistoryForward ||
            effect.action->executable->operation_kind ==
                mojom::TaskActionOperationKind::kReload ||
            effect.action->executable->operation_kind ==
                mojom::TaskActionOperationKind::kLinkOpen;
        if (!ValidIdentifier(source->source_id) ||
            !ValidIdentifier(source->tab_id) || !may_discover ||
            (new_tab ==
             (source->tab_id == effect.action->executable->tab_id)) ||
            source->normalized_origin.empty() ||
            source->normalized_origin.size() >
                mojom::kMaxNormalizedOriginBytes ||
            (source->canonical_locator &&
             (source->canonical_locator->empty() ||
              source->canonical_locator->size() >
                  mojom::kMaxSourceLocatorBytes))) {
          return false;
        }
        out.has_discovered_source = true;
        out.discovered_source_id = source->source_id;
        out.discovered_source_tab_id = source->tab_id;
        out.discovered_source_normalized_origin = source->normalized_origin;
        out.has_discovered_source_canonical_locator =
            source->canonical_locator.has_value();
        out.discovered_source_canonical_locator =
            source->canonical_locator.value_or("");
      }
      return true;
    }
    case mojom::PolicyActionClass::kMoveFocus:
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kExecuteToolJob:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kMemoryWrite:
    // Every store operation was answered above; a store class on any other
    // operation is a shape the action conversion already refused.
    case mojom::PolicyActionClass::kProfileStoreRead:
      return false;
  }
  return false;
}

}  // namespace taffy::core_service_internal
