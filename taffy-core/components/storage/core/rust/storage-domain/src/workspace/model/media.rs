// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Coordinate-bearing provenance for already-redacted workspace media facts.

use core::fmt::Write as _;

/// Maximum bytes in one browser-validated media coordinate.
pub const MAX_MEDIA_LOCATOR_BYTES: usize = 512;
/// Parts per million, including the certain endpoint.
pub const MAX_CONFIDENCE_PPM: u32 = 1_000_000;

/// Which media surface produced a retained coordinate-bearing page fact.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspaceMediaKind {
    Image,
    Video,
    Pdf,
}

impl WorkspaceMediaKind {
    pub(crate) const fn wire(self) -> u8 {
        match self {
            Self::Image => 0,
            Self::Video => 1,
            Self::Pdf => 2,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Image),
            1 => Some(Self::Video),
            2 => Some(Self::Pdf),
            _ => None,
        }
    }
}

/// Closed meaning of one retained media fact.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspaceMediaFactKind {
    Description,
    OcrText,
    Transcript,
    PdfText,
    PdfTableRow,
    Metadata,
}

impl WorkspaceMediaFactKind {
    pub(crate) const fn wire(self) -> u8 {
        match self {
            Self::Description => 0,
            Self::OcrText => 1,
            Self::Transcript => 2,
            Self::PdfText => 3,
            Self::PdfTableRow => 4,
            Self::Metadata => 5,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Description),
            1 => Some(Self::OcrText),
            2 => Some(Self::Transcript),
            3 => Some(Self::PdfText),
            4 => Some(Self::PdfTableRow),
            5 => Some(Self::Metadata),
            _ => None,
        }
    }
}

/// Browser-verified evidence source for one retained media fact.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspaceMediaEvidenceKind {
    Dom,
    Accessibility,
    CaptionTrack,
    PdfTextLayer,
    VisualInference,
    TableHeuristic,
}

impl WorkspaceMediaEvidenceKind {
    pub(crate) const fn wire(self) -> u8 {
        match self {
            Self::Dom => 0,
            Self::Accessibility => 1,
            Self::CaptionTrack => 2,
            Self::PdfTextLayer => 3,
            Self::VisualInference => 4,
            Self::TableHeuristic => 5,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Dom),
            1 => Some(Self::Accessibility),
            2 => Some(Self::CaptionTrack),
            3 => Some(Self::PdfTextLayer),
            4 => Some(Self::VisualInference),
            5 => Some(Self::TableHeuristic),
            _ => None,
        }
    }
}

/// Stable media coordinate retained beside already-redacted fact text.
///
/// Raw image, video and PDF bytes never enter this record. The coordinate is
/// the same bounded browser-verified shape that justified the transient fact,
/// so an export can cite where it came from after the utility process dies.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceMediaProvenance {
    pub media_kind: WorkspaceMediaKind,
    pub fact_kind: WorkspaceMediaFactKind,
    pub evidence_kind: WorkspaceMediaEvidenceKind,
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

impl WorkspaceMediaProvenance {
    /// Deterministic plain-language coordinate shared by every export format.
    pub fn location_descriptor(&self) -> String {
        let mut value = format!(
            "{}; bytes {}..{}",
            self.source_locator, self.source_start, self.source_end
        );
        if self.page_index_plus_one > 0 {
            let _ = write!(value, "; page {}", self.page_index_plus_one);
        }
        if self.timestamp_end_ms > 0 {
            let _ = write!(
                value,
                "; {}..{} ms",
                self.timestamp_start_ms, self.timestamp_end_ms
            );
        }
        if self.row_index_plus_one > 0 {
            let _ = write!(value, "; row {}", self.row_index_plus_one);
        }
        let _ = write!(value, "; confidence {} ppm", self.confidence_ppm);
        if self.truncated {
            value.push_str("; truncated");
        }
        value
    }
}

/// Replacement scope for one observation-derived fact batch.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspacePageFactScope {
    Dom,
    Media(WorkspaceMediaKind),
}

pub(super) fn valid_media_provenance(value: &WorkspaceMediaProvenance) -> bool {
    if value.source_locator.is_empty()
        || value.source_locator.len() > MAX_MEDIA_LOCATOR_BYTES
        || value.source_locator.trim() != value.source_locator
        || value.source_locator.chars().any(char::is_control)
        || value.source_start > value.source_end
        || value.timestamp_start_ms > value.timestamp_end_ms
        || value.confidence_ppm > MAX_CONFIDENCE_PPM
    {
        return false;
    }
    let has_page = value.page_index_plus_one > 0;
    let has_time = value.timestamp_end_ms > 0;
    let has_row = value.row_index_plus_one > 0;
    match value.media_kind {
        WorkspaceMediaKind::Image => {
            !has_page
                && !has_time
                && !has_row
                && matches!(value.fact_kind, WorkspaceMediaFactKind::Metadata)
                && matches!(
                    value.evidence_kind,
                    WorkspaceMediaEvidenceKind::Dom | WorkspaceMediaEvidenceKind::Accessibility
                )
                && value.source_locator.starts_with("image:")
        }
        WorkspaceMediaKind::Video => match value.fact_kind {
            WorkspaceMediaFactKind::Transcript => {
                has_time
                    && !has_page
                    && !has_row
                    && value.evidence_kind == WorkspaceMediaEvidenceKind::CaptionTrack
                    && value.source_locator.starts_with("video:track/")
            }
            WorkspaceMediaFactKind::Metadata => {
                !has_page
                    && !has_time
                    && !has_row
                    && value.evidence_kind == WorkspaceMediaEvidenceKind::Dom
                    && value.source_locator.starts_with("video:")
            }
            WorkspaceMediaFactKind::Description
            | WorkspaceMediaFactKind::OcrText
            | WorkspaceMediaFactKind::PdfText
            | WorkspaceMediaFactKind::PdfTableRow => false,
        },
        WorkspaceMediaKind::Pdf => match value.fact_kind {
            WorkspaceMediaFactKind::PdfText => {
                has_page
                    && !has_time
                    && !has_row
                    && value.evidence_kind == WorkspaceMediaEvidenceKind::PdfTextLayer
                    && value.source_locator.starts_with("pdf:page/")
            }
            WorkspaceMediaFactKind::PdfTableRow => {
                has_page
                    && !has_time
                    && has_row
                    && value.evidence_kind == WorkspaceMediaEvidenceKind::TableHeuristic
                    && value.source_locator.starts_with("pdf:page/")
            }
            WorkspaceMediaFactKind::Metadata => {
                !has_time
                    && !has_row
                    && value.evidence_kind == WorkspaceMediaEvidenceKind::PdfTextLayer
                    && value.source_locator.starts_with("pdf:")
            }
            WorkspaceMediaFactKind::Description
            | WorkspaceMediaFactKind::OcrText
            | WorkspaceMediaFactKind::Transcript => false,
        },
    }
}
