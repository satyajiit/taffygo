// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_service_types as wire;
use loop_kernel::context::{MediaEvidenceKind, MediaFactKind, MediaObservationKind};

use super::super::{decode_media_observation, ObservationWireError};

fn rendered(kind: wire::MediaObservationKind) -> wire::MediaObservationResult {
    wire::MediaObservationResult {
        kind,
        facts: Vec::new(),
        attachment_handle: Some("media-opaque-handle".to_owned()),
        attachment_mime_type: Some("image/png".to_owned()),
        width_px: 640,
        height_px: 360,
        has_meaningful_text: false,
        scanned_pdf_ocr_required: false,
        capture_provenance: None,
    }
}

fn pdf_fact(
    kind: wire::MediaFactKind,
    evidence: wire::MediaEvidenceKind,
) -> wire::MediaObservationFact {
    wire::MediaObservationFact {
        kind,
        evidence,
        text: "Revenue  42".to_owned(),
        source_locator: "pdf:page/2".to_owned(),
        source_start: 10,
        source_end: 21,
        page_index_plus_one: 2,
        timestamp_start_ms: 0,
        timestamp_end_ms: 0,
        row_index_plus_one: 0,
        confidence_ppm: 1_000_000,
        truncated: false,
    }
}

#[test]
fn exact_image_operation_adopts_only_an_opaque_attachment() {
    let decoded = decode_media_observation(
        wire::TaskActionOperationKind::ImageDescribe,
        Some(&rendered(wire::MediaObservationKind::Image)),
    )
    .expect("valid exact image")
    .expect("media is required");
    assert_eq!(decoded.kind, MediaObservationKind::Image);
    let attachment = decoded.attachment.expect("browser handle");
    assert_eq!(attachment.handle, "media-opaque-handle");
    assert_eq!(attachment.mime_type, "image/png");
    assert!(decoded.facts.is_empty());
}

#[test]
fn media_kind_mime_and_required_attachment_fail_closed() {
    let mut input = rendered(wire::MediaObservationKind::Video);
    assert_eq!(
        decode_media_observation(wire::TaskActionOperationKind::ImageReadText, Some(&input)),
        Err(ObservationWireError::InvalidMedia)
    );
    input.kind = wire::MediaObservationKind::Image;
    input.attachment_mime_type = Some("image/jpeg".to_owned());
    assert_eq!(
        decode_media_observation(wire::TaskActionOperationKind::ImageReadText, Some(&input)),
        Err(ObservationWireError::InvalidMedia)
    );
    assert_eq!(
        decode_media_observation(wire::TaskActionOperationKind::VideoInspect, None),
        Err(ObservationWireError::InvalidMedia)
    );
}

#[test]
fn video_keeps_loaded_caption_cue_timestamps_separate_from_visual_inference() {
    let mut input = rendered(wire::MediaObservationKind::Video);
    input.facts.push(wire::MediaObservationFact {
        kind: wire::MediaFactKind::Transcript,
        evidence: wire::MediaEvidenceKind::CaptionTrack,
        text: "Battery life starts here".to_owned(),
        source_locator: "video:track/1/cue/7".to_owned(),
        source_start: 0,
        source_end: 24,
        page_index_plus_one: 0,
        timestamp_start_ms: 12_500,
        timestamp_end_ms: 15_000,
        row_index_plus_one: 0,
        confidence_ppm: 1_000_000,
        truncated: false,
    });
    input.has_meaningful_text = true;
    let decoded =
        decode_media_observation(wire::TaskActionOperationKind::VideoInspect, Some(&input))
            .expect("valid caption-backed video")
            .expect("video result");
    assert_eq!(decoded.facts[0].kind, MediaFactKind::Transcript);
    assert_eq!(decoded.facts[0].evidence, MediaEvidenceKind::CaptionTrack);
    assert_eq!(decoded.facts[0].timestamp_start_ms, 12_500);
    assert_eq!(decoded.facts[0].timestamp_end_ms, 15_000);
}

#[test]
fn native_pdf_text_and_heuristic_rows_keep_source_coordinates() {
    let mut row = pdf_fact(
        wire::MediaFactKind::PdfTableRow,
        wire::MediaEvidenceKind::TableHeuristic,
    );
    row.row_index_plus_one = 1;
    row.confidence_ppm = 650_000;
    let input = wire::MediaObservationResult {
        kind: wire::MediaObservationKind::Pdf,
        facts: vec![
            pdf_fact(
                wire::MediaFactKind::PdfText,
                wire::MediaEvidenceKind::PdfTextLayer,
            ),
            row,
        ],
        attachment_handle: None,
        attachment_mime_type: None,
        width_px: 0,
        height_px: 0,
        has_meaningful_text: true,
        scanned_pdf_ocr_required: false,
        capture_provenance: None,
    };
    let decoded = decode_media_observation(wire::TaskActionOperationKind::PdfInspect, Some(&input))
        .expect("valid native PDF")
        .expect("PDF result");
    assert_eq!(decoded.kind, MediaObservationKind::Pdf);
    assert_eq!(decoded.facts[0].kind, MediaFactKind::PdfText);
    assert_eq!(decoded.facts[0].evidence, MediaEvidenceKind::PdfTextLayer);
    assert_eq!(decoded.facts[0].page_index_plus_one, 2);
    assert_eq!(decoded.facts[0].source_start, 10);
    assert_eq!(decoded.facts[1].kind, MediaFactKind::PdfTableRow);
    assert_eq!(decoded.facts[1].row_index_plus_one, 1);
}

#[test]
fn scanned_pdf_is_explicit_and_never_claims_native_text() {
    let input = wire::MediaObservationResult {
        kind: wire::MediaObservationKind::Pdf,
        facts: Vec::new(),
        attachment_handle: None,
        attachment_mime_type: None,
        width_px: 0,
        height_px: 0,
        has_meaningful_text: false,
        scanned_pdf_ocr_required: true,
        capture_provenance: None,
    };
    let decoded = decode_media_observation(wire::TaskActionOperationKind::PdfInspect, Some(&input))
        .expect("explicit scanned PDF result")
        .expect("PDF result");
    assert!(decoded.scanned_pdf_ocr_required);
    assert!(!decoded.has_meaningful_text);
    assert!(decoded.facts.is_empty());
}

#[test]
fn page_screenshot_requires_bounded_full_viewport_provenance() {
    let mut input = rendered(wire::MediaObservationKind::PageScreenshot);
    input.width_px = 960;
    input.height_px = 480;
    input.capture_provenance = Some(wire::MediaCaptureProvenance {
        capture_x_dip: 0,
        capture_y_dip: 0,
        capture_width_dip: 2_000,
        capture_height_dip: 1_000,
        viewport_width_dip: 2_000,
        viewport_height_dip: 1_000,
        output_scale_ppm: 480_000,
        captured_at_monotonic_ms: 7,
        redacted_region_count: 2,
    });
    let decoded = decode_media_observation(
        wire::TaskActionOperationKind::PageScreenshotInspect,
        Some(&input),
    )
    .expect("bounded page screenshot")
    .expect("page screenshot result");
    assert_eq!(decoded.kind, MediaObservationKind::PageScreenshot);
    assert_eq!(
        decoded
            .capture_provenance
            .expect("browser provenance")
            .redacted_region_count,
        2
    );

    input
        .capture_provenance
        .as_mut()
        .expect("fixture provenance")
        .capture_x_dip = 1;
    assert_eq!(
        decode_media_observation(
            wire::TaskActionOperationKind::PageScreenshotInspect,
            Some(&input),
        ),
        Err(ObservationWireError::InvalidMedia)
    );
}

#[test]
fn non_media_actions_reject_smuggled_media() {
    assert_eq!(
        decode_media_observation(
            wire::TaskActionOperationKind::DomRead,
            Some(&rendered(wire::MediaObservationKind::Image)),
        ),
        Err(ObservationWireError::InvalidMedia)
    );
}
