// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Transient, typed media evidence and its bounded model projection.

use core::fmt::Write as _;

/// Which media-specific read produced transient page evidence.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MediaObservationKind {
    Image,
    Video,
    Pdf,
    PageScreenshot,
}

/// What one media fact means. The evidence source below remains separate so
/// an inferred table row can never masquerade as native PDF structure.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MediaFactKind {
    Description,
    OcrText,
    Transcript,
    PdfText,
    PdfTableRow,
    Metadata,
}

/// Where one media fact came from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MediaEvidenceKind {
    Dom,
    Accessibility,
    CaptionTrack,
    PdfTextLayer,
    VisualInference,
    TableHeuristic,
}

/// One bounded, already-redacted fact and its stable source coordinate.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaFact {
    pub kind: MediaFactKind,
    pub evidence: MediaEvidenceKind,
    pub text: String,
    pub source_locator: String,
    pub source_start: u32,
    pub source_end: u32,
    pub page_index_plus_one: u32,
    pub timestamp_start_ms: u64,
    pub timestamp_end_ms: u64,
    pub row_index_plus_one: u32,
    pub confidence_ppm: u32,
    pub truncated: bool,
}

/// Browser-resident rendered bytes represented only by a one-use opaque
/// handle. The isolated core cannot resolve this value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaAttachment {
    pub handle: String,
    pub mime_type: String,
    pub width_px: u32,
    pub height_px: u32,
}

/// Browser-authored rendered-capture geometry. This accompanies the page
/// screenshot fallback so the model can distinguish visible pixels from
/// semantic evidence and account for every painted secret region.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaCaptureProvenance {
    pub capture_x_dip: u32,
    pub capture_y_dip: u32,
    pub capture_width_dip: u32,
    pub capture_height_dip: u32,
    pub viewport_width_dip: u32,
    pub viewport_height_dip: u32,
    pub output_scale_ppm: u32,
    pub captured_at_monotonic_ms: u64,
    pub redacted_region_count: u32,
}

/// Transient media evidence retained beside one exact page observation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaObservation {
    pub kind: MediaObservationKind,
    pub facts: Vec<MediaFact>,
    pub attachment: Option<MediaAttachment>,
    pub capture_provenance: Option<MediaCaptureProvenance>,
    pub has_meaningful_text: bool,
    pub scanned_pdf_ocr_required: bool,
}

pub(super) fn append_media_projection(
    output: &mut String,
    media: &MediaObservation,
    source_number: usize,
    maximum: usize,
) -> bool {
    if output.len() >= maximum {
        return false;
    }
    let kind = match media.kind {
        MediaObservationKind::Image => "image",
        MediaObservationKind::Video => "video",
        MediaObservationKind::Pdf => "PDF",
        MediaObservationKind::PageScreenshot => "redacted page screenshot fallback",
    };
    let heading = format!("Source {source_number} {kind} evidence:\n");
    append_bounded(output, &heading, maximum);
    if media.scanned_pdf_ocr_required {
        append_bounded(
            output,
            "PDF has no usable native text layer; scanned-PDF text recognition is not supported.\n",
            maximum,
        );
        return true;
    }
    if let Some(capture) = &media.capture_provenance {
        let provenance = format!(
            "Attached visible-viewport image: crop ({}, {}) {}x{} DIP inside {}x{} DIP; scale {} ppm; captured at browser monotonic {} ms; {} secret region(s) painted opaque. Pixels outside this crop are not evidence.\n",
            capture.capture_x_dip,
            capture.capture_y_dip,
            capture.capture_width_dip,
            capture.capture_height_dip,
            capture.viewport_width_dip,
            capture.viewport_height_dip,
            capture.output_scale_ppm,
            capture.captured_at_monotonic_ms,
            capture.redacted_region_count,
        );
        append_bounded(output, &provenance, maximum);
        return true;
    }
    let mut appended = false;
    for fact in &media.facts {
        if output.len() >= maximum {
            break;
        }
        let evidence = match fact.evidence {
            MediaEvidenceKind::Dom => "DOM",
            MediaEvidenceKind::Accessibility => "accessibility",
            MediaEvidenceKind::CaptionTrack => "caption track",
            MediaEvidenceKind::PdfTextLayer => "PDF text layer",
            MediaEvidenceKind::VisualInference => "visual inference",
            MediaEvidenceKind::TableHeuristic => "table heuristic",
        };
        let mut prefix = format!(
            "- [{evidence}; {}; bytes {}..{}",
            fact.source_locator, fact.source_start, fact.source_end
        );
        if fact.timestamp_end_ms > 0 {
            let _ = write!(
                prefix,
                "; {}..{} ms",
                fact.timestamp_start_ms, fact.timestamp_end_ms
            );
        }
        if fact.page_index_plus_one > 0 {
            let _ = write!(prefix, "; page {}", fact.page_index_plus_one);
        }
        if fact.row_index_plus_one > 0 {
            let _ = write!(prefix, "; row {}", fact.row_index_plus_one);
        }
        prefix.push_str("] ");
        append_bounded(output, &prefix, maximum);
        append_bounded(output, fact.text.trim(), maximum);
        append_bounded(output, "\n", maximum);
        appended = true;
    }
    appended
}

fn append_bounded(output: &mut String, value: &str, maximum: usize) {
    let remaining = maximum.saturating_sub(output.len());
    if remaining == 0 {
        return;
    }
    output.push_str(utf8_prefix(value, remaining));
}

fn utf8_prefix(value: &str, maximum: usize) -> &str {
    if value.len() <= maximum {
        return value;
    }
    let mut end = maximum;
    while !value.is_char_boundary(end) {
        end = end.saturating_sub(1);
    }
    &value[..end]
}
