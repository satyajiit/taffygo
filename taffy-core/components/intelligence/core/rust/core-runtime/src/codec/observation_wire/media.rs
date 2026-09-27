// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict media-result decoding for one exact requested operation.

use core_service_types as wire;
use loop_kernel::context::{
    MediaAttachment, MediaCaptureProvenance, MediaEvidenceKind, MediaFact, MediaFactKind,
    MediaObservation, MediaObservationKind,
};

use super::{bounded_non_empty, ObservationWireError};

/// Validates the optional media half of one observation against the exact
/// action that requested it. Raw media bytes are structurally impossible here:
/// the generated contract carries only facts and an opaque browser handle.
pub fn decode_media_observation(
    operation: wire::TaskActionOperationKind,
    result: Option<&wire::MediaObservationResult>,
) -> Result<Option<MediaObservation>, ObservationWireError> {
    let expected_kind = expected_media_kind(operation);
    let Some(expected_kind) = expected_kind else {
        return result
            .is_none()
            .then_some(None)
            .ok_or(ObservationWireError::InvalidMedia);
    };
    let result = result.ok_or(ObservationWireError::InvalidMedia)?;
    let kind = match result.kind {
        wire::MediaObservationKind::Image => MediaObservationKind::Image,
        wire::MediaObservationKind::Video => MediaObservationKind::Video,
        wire::MediaObservationKind::Pdf => MediaObservationKind::Pdf,
        wire::MediaObservationKind::PageScreenshot => MediaObservationKind::PageScreenshot,
    };
    if kind != expected_kind || result.facts.len() > wire::MAX_MEDIA_FACTS {
        return Err(ObservationWireError::InvalidMedia);
    }

    let attachment = decode_attachment(result)?;
    if (matches!(
        kind,
        MediaObservationKind::Image
            | MediaObservationKind::Video
            | MediaObservationKind::PageScreenshot
    ) && attachment.is_none())
        || (kind == MediaObservationKind::Pdf && attachment.is_some())
        || (kind == MediaObservationKind::PageScreenshot
            && (!result.facts.is_empty()
                || result.has_meaningful_text
                || result.scanned_pdf_ocr_required))
        || (result.scanned_pdf_ocr_required
            && (kind != MediaObservationKind::Pdf || result.has_meaningful_text))
    {
        return Err(ObservationWireError::InvalidMedia);
    }

    let (facts, native_text) = decode_facts(kind, result)?;
    if result.has_meaningful_text != native_text
        || (kind == MediaObservationKind::Pdf && result.scanned_pdf_ocr_required == native_text)
    {
        return Err(ObservationWireError::InvalidMedia);
    }
    Ok(Some(MediaObservation {
        kind,
        facts,
        attachment,
        capture_provenance: decode_capture_provenance(kind, result)?,
        has_meaningful_text: result.has_meaningful_text,
        scanned_pdf_ocr_required: result.scanned_pdf_ocr_required,
    }))
}

const fn expected_media_kind(
    operation: wire::TaskActionOperationKind,
) -> Option<MediaObservationKind> {
    match operation {
        wire::TaskActionOperationKind::ImageDescribe
        | wire::TaskActionOperationKind::ImageReadText => Some(MediaObservationKind::Image),
        wire::TaskActionOperationKind::VideoInspect => Some(MediaObservationKind::Video),
        wire::TaskActionOperationKind::PdfInspect => Some(MediaObservationKind::Pdf),
        wire::TaskActionOperationKind::PageScreenshotInspect => {
            Some(MediaObservationKind::PageScreenshot)
        }
        _ => None,
    }
}

fn decode_capture_provenance(
    kind: MediaObservationKind,
    result: &wire::MediaObservationResult,
) -> Result<Option<MediaCaptureProvenance>, ObservationWireError> {
    let Some(capture) = result.capture_provenance.as_ref() else {
        return (kind != MediaObservationKind::PageScreenshot)
            .then_some(None)
            .ok_or(ObservationWireError::InvalidMedia);
    };
    if kind != MediaObservationKind::PageScreenshot
        || capture.capture_x_dip != 0
        || capture.capture_y_dip != 0
        || capture.capture_width_dip == 0
        || capture.capture_height_dip == 0
        || capture.capture_width_dip != capture.viewport_width_dip
        || capture.capture_height_dip != capture.viewport_height_dip
        || !(1..=1_000_000).contains(&capture.output_scale_ppm)
        || capture.captured_at_monotonic_ms == 0
        || capture.redacted_region_count > 128
    {
        return Err(ObservationWireError::InvalidMedia);
    }
    let width_scale = u64::from(result.width_px) * 1_000_000 / u64::from(capture.capture_width_dip);
    let height_scale =
        u64::from(result.height_px) * 1_000_000 / u64::from(capture.capture_height_dip);
    if u64::from(capture.output_scale_ppm) != width_scale.min(height_scale) {
        return Err(ObservationWireError::InvalidMedia);
    }
    Ok(Some(MediaCaptureProvenance {
        capture_x_dip: capture.capture_x_dip,
        capture_y_dip: capture.capture_y_dip,
        capture_width_dip: capture.capture_width_dip,
        capture_height_dip: capture.capture_height_dip,
        viewport_width_dip: capture.viewport_width_dip,
        viewport_height_dip: capture.viewport_height_dip,
        output_scale_ppm: capture.output_scale_ppm,
        captured_at_monotonic_ms: capture.captured_at_monotonic_ms,
        redacted_region_count: capture.redacted_region_count,
    }))
}

fn decode_attachment(
    result: &wire::MediaObservationResult,
) -> Result<Option<MediaAttachment>, ObservationWireError> {
    match (
        result.attachment_handle.as_deref(),
        result.attachment_mime_type.as_deref(),
    ) {
        (None, None) if result.width_px == 0 && result.height_px == 0 => Ok(None),
        (Some(handle), Some("image/png"))
            if bounded_non_empty(handle, wire::MAX_MEDIA_ATTACHMENT_HANDLE_BYTES)
                && (1..=u32::try_from(wire::MAX_MEDIA_DIMENSION_PX).unwrap_or(u32::MAX))
                    .contains(&result.width_px)
                && (1..=u32::try_from(wire::MAX_MEDIA_DIMENSION_PX).unwrap_or(u32::MAX))
                    .contains(&result.height_px) =>
        {
            Ok(Some(MediaAttachment {
                handle: handle.to_owned(),
                mime_type: "image/png".to_owned(),
                width_px: result.width_px,
                height_px: result.height_px,
            }))
        }
        _ => Err(ObservationWireError::InvalidMedia),
    }
}

fn decode_facts(
    kind: MediaObservationKind,
    result: &wire::MediaObservationResult,
) -> Result<(Vec<MediaFact>, bool), ObservationWireError> {
    let mut total = 0_usize;
    let mut facts = Vec::with_capacity(result.facts.len());
    let mut native_text = false;
    for fact in &result.facts {
        if fact.text.trim().is_empty()
            || fact.text.len() > wire::MAX_MEDIA_FACT_TEXT_BYTES
            || !bounded_non_empty(
                fact.source_locator.as_str(),
                wire::MAX_MEDIA_FACT_LOCATOR_BYTES,
            )
            || fact.source_end < fact.source_start
            || fact.confidence_ppm > 1_000_000
            || fact.timestamp_end_ms < fact.timestamp_start_ms
        {
            return Err(ObservationWireError::InvalidMedia);
        }
        total = total
            .checked_add(fact.text.len())
            .and_then(|value| value.checked_add(fact.source_locator.len()))
            .ok_or(ObservationWireError::InvalidMedia)?;
        if total > wire::MAX_MEDIA_FACT_TOTAL_BYTES {
            return Err(ObservationWireError::InvalidMedia);
        }
        let fact_kind = media_fact_kind(fact.kind);
        let evidence = media_evidence_kind(fact.evidence);
        if !media_fact_shape_is_valid(kind, fact_kind, evidence, fact) {
            return Err(ObservationWireError::InvalidMedia);
        }
        native_text |= matches!(
            fact_kind,
            MediaFactKind::Transcript | MediaFactKind::PdfText
        );
        facts.push(MediaFact {
            kind: fact_kind,
            evidence,
            text: fact.text.clone(),
            source_locator: fact.source_locator.clone(),
            source_start: fact.source_start,
            source_end: fact.source_end,
            page_index_plus_one: fact.page_index_plus_one,
            timestamp_start_ms: fact.timestamp_start_ms,
            timestamp_end_ms: fact.timestamp_end_ms,
            row_index_plus_one: fact.row_index_plus_one,
            confidence_ppm: fact.confidence_ppm,
            truncated: fact.truncated,
        });
    }
    Ok((facts, native_text))
}

const fn media_fact_kind(value: wire::MediaFactKind) -> MediaFactKind {
    match value {
        wire::MediaFactKind::Description => MediaFactKind::Description,
        wire::MediaFactKind::OcrText => MediaFactKind::OcrText,
        wire::MediaFactKind::Transcript => MediaFactKind::Transcript,
        wire::MediaFactKind::PdfText => MediaFactKind::PdfText,
        wire::MediaFactKind::PdfTableRow => MediaFactKind::PdfTableRow,
        wire::MediaFactKind::Metadata => MediaFactKind::Metadata,
    }
}

const fn media_evidence_kind(value: wire::MediaEvidenceKind) -> MediaEvidenceKind {
    match value {
        wire::MediaEvidenceKind::Dom => MediaEvidenceKind::Dom,
        wire::MediaEvidenceKind::Accessibility => MediaEvidenceKind::Accessibility,
        wire::MediaEvidenceKind::CaptionTrack => MediaEvidenceKind::CaptionTrack,
        wire::MediaEvidenceKind::PdfTextLayer => MediaEvidenceKind::PdfTextLayer,
        wire::MediaEvidenceKind::VisualInference => MediaEvidenceKind::VisualInference,
        wire::MediaEvidenceKind::TableHeuristic => MediaEvidenceKind::TableHeuristic,
    }
}

fn media_fact_shape_is_valid(
    observation: MediaObservationKind,
    kind: MediaFactKind,
    evidence: MediaEvidenceKind,
    fact: &wire::MediaObservationFact,
) -> bool {
    let has_page = fact.page_index_plus_one > 0;
    let has_time = fact.timestamp_end_ms > 0;
    let has_row = fact.row_index_plus_one > 0;
    match observation {
        MediaObservationKind::Image => {
            !has_page
                && !has_time
                && !has_row
                && matches!(kind, MediaFactKind::Metadata)
                && matches!(
                    evidence,
                    MediaEvidenceKind::Dom | MediaEvidenceKind::Accessibility
                )
                && fact.source_locator.starts_with("image:")
        }
        MediaObservationKind::Video => match kind {
            MediaFactKind::Transcript => {
                has_time
                    && !has_page
                    && !has_row
                    && evidence == MediaEvidenceKind::CaptionTrack
                    && fact.source_locator.starts_with("video:track/")
            }
            MediaFactKind::Metadata => {
                !has_page
                    && !has_time
                    && !has_row
                    && evidence == MediaEvidenceKind::Dom
                    && fact.source_locator.starts_with("video:")
            }
            _ => false,
        },
        MediaObservationKind::Pdf => match kind {
            MediaFactKind::PdfText => {
                has_page
                    && !has_time
                    && !has_row
                    && evidence == MediaEvidenceKind::PdfTextLayer
                    && fact.source_locator.starts_with("pdf:page/")
            }
            MediaFactKind::PdfTableRow => {
                has_page
                    && !has_time
                    && has_row
                    && evidence == MediaEvidenceKind::TableHeuristic
                    && fact.source_locator.starts_with("pdf:page/")
            }
            MediaFactKind::Metadata => {
                !has_time
                    && !has_row
                    && evidence == MediaEvidenceKind::PdfTextLayer
                    && fact.source_locator.starts_with("pdf:")
            }
            _ => false,
        },
        MediaObservationKind::PageScreenshot => false,
    }
}
