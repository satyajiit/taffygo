// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Images, screenshots and PDF facts as transient page content.

use super::{evidence, source_id};
use crate::context::arena::PageArena;
use crate::context::page::{
    LivePage, MediaAttachment, MediaCaptureProvenance, MediaEvidenceKind, MediaFact, MediaFactKind,
    MediaObservation, MediaObservationKind,
};

#[test]
fn sole_visual_attachment_is_transient_page_content() {
    let mut page = LivePage::new();
    page.replace_source_with_media(
        source_id(),
        &evidence(),
        PageArena::new(),
        Some(MediaObservation {
            kind: MediaObservationKind::Image,
            facts: Vec::new(),
            attachment: Some(MediaAttachment {
                handle: "media-handle".to_owned(),
                mime_type: "image/png".to_owned(),
                width_px: 640,
                height_px: 360,
            }),
            capture_provenance: None,
            has_meaningful_text: false,
            scanned_pdf_ocr_required: false,
        }),
    )
    .unwrap_or_else(|_| unreachable!("canonical media evidence"));

    let preview = page.preview();
    assert!(preview.carries_page_content);
    let attachment = preview.media_attachment.expect("one exact image");
    assert_eq!(attachment.handle, "media-handle");
    assert_eq!(attachment.mime_type, "image/png");
}

#[test]
fn screenshot_fallback_projects_browser_capture_provenance() {
    let mut page = LivePage::new();
    page.replace_source_with_media(
        source_id(),
        &evidence(),
        PageArena::new(),
        Some(MediaObservation {
            kind: MediaObservationKind::PageScreenshot,
            facts: Vec::new(),
            attachment: Some(MediaAttachment {
                handle: "screenshot-handle".to_owned(),
                mime_type: "image/png".to_owned(),
                width_px: 960,
                height_px: 480,
            }),
            capture_provenance: Some(MediaCaptureProvenance {
                capture_x_dip: 0,
                capture_y_dip: 0,
                capture_width_dip: 2_000,
                capture_height_dip: 1_000,
                viewport_width_dip: 2_000,
                viewport_height_dip: 1_000,
                output_scale_ppm: 480_000,
                captured_at_monotonic_ms: 42,
                redacted_region_count: 2,
            }),
            has_meaningful_text: false,
            scanned_pdf_ocr_required: false,
        }),
    )
    .unwrap_or_else(|_| unreachable!("canonical screenshot evidence"));

    let preview = page.preview();
    assert!(preview.carries_page_content);
    let text = preview.text.as_deref().unwrap_or("");
    assert!(text.contains("redacted page screenshot fallback"));
    assert!(text.contains("2 secret region(s) painted opaque"));
    assert!(text.contains("Pixels outside this crop are not evidence"));
}

#[test]
fn pdf_fact_projection_keeps_page_row_and_source_offsets() {
    let mut page = LivePage::new();
    page.replace_source_with_media(
        source_id(),
        &evidence(),
        PageArena::new(),
        Some(MediaObservation {
            kind: MediaObservationKind::Pdf,
            facts: vec![MediaFact {
                kind: MediaFactKind::PdfTableRow,
                evidence: MediaEvidenceKind::TableHeuristic,
                text: "Revenue  42".to_owned(),
                source_locator: "pdf:page/2".to_owned(),
                source_start: 10,
                source_end: 21,
                page_index_plus_one: 2,
                timestamp_start_ms: 0,
                timestamp_end_ms: 0,
                row_index_plus_one: 1,
                confidence_ppm: 650_000,
                truncated: false,
            }],
            attachment: None,
            capture_provenance: None,
            has_meaningful_text: true,
            scanned_pdf_ocr_required: false,
        }),
    )
    .unwrap_or_else(|_| unreachable!("canonical PDF evidence"));

    let text = page.preview().text.expect("PDF fact projection");
    assert!(text.contains("table heuristic; pdf:page/2; bytes 10..21; page 2; row 1"));
    assert!(text.contains("Revenue  42"));
}
