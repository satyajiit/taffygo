// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Media-specific workspace scopes and coordinate-preserving fact derivation.

use loop_kernel::context::{MediaEvidenceKind, MediaFact, MediaFactKind, MediaObservationKind};
use taffy_storage::ids::FactId;
use taffy_storage::workspace::{
    WorkspaceMediaEvidenceKind, WorkspaceMediaFactKind, WorkspaceMediaKind,
    WorkspaceMediaProvenance,
};
use task_engine::action::{ActionIntent, BrowserIntent};

use crate::account::Sha256Port;
use crate::ports::{TaskIdEntropy, WorkspacePageFact};

use super::{domain_input, WorkspaceCommitError};

const MEDIA_FACT_ID_DOMAIN: &[u8] = b"\0taffy.workspace-media-fact.v1\0";
const MAX_WORKSPACE_MEDIA_FACTS: usize = 16;
const MAX_WORKSPACE_MEDIA_FACT_TOTAL_BYTES: usize = 32 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum WorkspaceIngestionScope {
    Dom,
    Media(MediaObservationKind),
}

pub(super) const fn workspace_ingestion_scope(
    intent: &ActionIntent,
) -> Option<WorkspaceIngestionScope> {
    match intent {
        ActionIntent::Browser(
            BrowserIntent::DomRead { target: None, .. } | BrowserIntent::DomQuery { .. },
        ) => Some(WorkspaceIngestionScope::Dom),
        ActionIntent::Browser(
            BrowserIntent::ImageDescribe { .. } | BrowserIntent::ImageReadText { .. },
        ) => Some(WorkspaceIngestionScope::Media(MediaObservationKind::Image)),
        ActionIntent::Browser(BrowserIntent::VideoInspect { .. }) => {
            Some(WorkspaceIngestionScope::Media(MediaObservationKind::Video))
        }
        ActionIntent::Browser(BrowserIntent::PdfInspect { .. }) => {
            Some(WorkspaceIngestionScope::Media(MediaObservationKind::Pdf))
        }
        _ => None,
    }
}

#[allow(clippy::too_many_arguments)]
pub(super) fn derive_media_facts(
    media: &[MediaFact],
    media_kind: MediaObservationKind,
    entropy: &TaskIdEntropy,
    workspace_id: &str,
    source_id: &str,
    page_epoch: &str,
    graph_revision: u64,
    digest_port: &dyn Sha256Port,
) -> Result<Vec<WorkspacePageFact>, WorkspaceCommitError> {
    let storage_media_kind =
        storage_media_kind(media_kind).ok_or(WorkspaceCommitError::InvalidTaskFacts)?;
    let mut facts = Vec::with_capacity(media.len().min(MAX_WORKSPACE_MEDIA_FACTS));
    let mut retained_bytes = 0_usize;
    for fact in media.iter().take(MAX_WORKSPACE_MEDIA_FACTS) {
        let text = fact.text.trim();
        if text.is_empty() {
            continue;
        }
        if retained_bytes == MAX_WORKSPACE_MEDIA_FACT_TOTAL_BYTES {
            break;
        }
        let remaining = MAX_WORKSPACE_MEDIA_FACT_TOTAL_BYTES.saturating_sub(retained_bytes);
        let value = utf8_prefix(text, remaining).to_owned();
        if value.is_empty() {
            break;
        }
        let truncated = fact.truncated || value.len() < text.len();
        retained_bytes = retained_bytes.saturating_add(value.len());
        let kind_tag = [media_fact_kind_tag(fact.kind)];
        let evidence_tag = [media_evidence_kind_tag(fact.evidence)];
        let source_start = fact.source_start.to_be_bytes();
        let source_end = fact.source_end.to_be_bytes();
        let page = fact.page_index_plus_one.to_be_bytes();
        let time_start = fact.timestamp_start_ms.to_be_bytes();
        let time_end = fact.timestamp_end_ms.to_be_bytes();
        let row = fact.row_index_plus_one.to_be_bytes();
        let revision = graph_revision.to_be_bytes();
        let media_kind_bytes = [media_kind_tag(media_kind)];
        let input = domain_input(
            MEDIA_FACT_ID_DOMAIN,
            &[
                entropy.as_bytes(),
                workspace_id.as_bytes(),
                source_id.as_bytes(),
                page_epoch.as_bytes(),
                &revision,
                &media_kind_bytes,
                &kind_tag,
                &evidence_tag,
                fact.source_locator.as_bytes(),
                &source_start,
                &source_end,
                &page,
                &time_start,
                &time_end,
                &row,
                value.as_bytes(),
            ],
        );
        let digest = digest_port
            .sha256(&input)
            .map_err(|_| WorkspaceCommitError::DigestUnavailable)?;
        let mut bytes = [0_u8; 16];
        bytes.copy_from_slice(&digest[..16]);
        facts.push(WorkspacePageFact {
            fact_id: FactId::from_bytes(bytes).to_text(),
            field: media_field(fact.kind).to_owned(),
            value,
            media_provenance: Some(WorkspaceMediaProvenance {
                media_kind: storage_media_kind,
                fact_kind: storage_media_fact_kind(fact.kind),
                evidence_kind: storage_media_evidence_kind(fact.evidence),
                source_locator: fact.source_locator.clone(),
                source_start: fact.source_start,
                source_end: fact.source_end,
                page_index_plus_one: fact.page_index_plus_one,
                timestamp_start_ms: fact.timestamp_start_ms,
                timestamp_end_ms: fact.timestamp_end_ms,
                row_index_plus_one: fact.row_index_plus_one,
                confidence_ppm: fact.confidence_ppm,
                truncated,
            }),
        });
    }
    facts.sort_by(|left, right| left.fact_id.cmp(&right.fact_id));
    facts.dedup_by(|left, right| left.fact_id == right.fact_id);
    Ok(facts)
}

const fn media_kind_tag(value: MediaObservationKind) -> u8 {
    match value {
        MediaObservationKind::Image => 0,
        MediaObservationKind::Video => 1,
        MediaObservationKind::Pdf => 2,
        MediaObservationKind::PageScreenshot => 3,
    }
}

const fn media_fact_kind_tag(value: MediaFactKind) -> u8 {
    match value {
        MediaFactKind::Description => 0,
        MediaFactKind::OcrText => 1,
        MediaFactKind::Transcript => 2,
        MediaFactKind::PdfText => 3,
        MediaFactKind::PdfTableRow => 4,
        MediaFactKind::Metadata => 5,
    }
}

const fn media_evidence_kind_tag(value: MediaEvidenceKind) -> u8 {
    match value {
        MediaEvidenceKind::Dom => 0,
        MediaEvidenceKind::Accessibility => 1,
        MediaEvidenceKind::CaptionTrack => 2,
        MediaEvidenceKind::PdfTextLayer => 3,
        MediaEvidenceKind::VisualInference => 4,
        MediaEvidenceKind::TableHeuristic => 5,
    }
}

const fn storage_media_kind(value: MediaObservationKind) -> Option<WorkspaceMediaKind> {
    match value {
        MediaObservationKind::Image => Some(WorkspaceMediaKind::Image),
        MediaObservationKind::Video => Some(WorkspaceMediaKind::Video),
        MediaObservationKind::Pdf => Some(WorkspaceMediaKind::Pdf),
        MediaObservationKind::PageScreenshot => None,
    }
}

const fn storage_media_fact_kind(value: MediaFactKind) -> WorkspaceMediaFactKind {
    match value {
        MediaFactKind::Description => WorkspaceMediaFactKind::Description,
        MediaFactKind::OcrText => WorkspaceMediaFactKind::OcrText,
        MediaFactKind::Transcript => WorkspaceMediaFactKind::Transcript,
        MediaFactKind::PdfText => WorkspaceMediaFactKind::PdfText,
        MediaFactKind::PdfTableRow => WorkspaceMediaFactKind::PdfTableRow,
        MediaFactKind::Metadata => WorkspaceMediaFactKind::Metadata,
    }
}

const fn storage_media_evidence_kind(value: MediaEvidenceKind) -> WorkspaceMediaEvidenceKind {
    match value {
        MediaEvidenceKind::Dom => WorkspaceMediaEvidenceKind::Dom,
        MediaEvidenceKind::Accessibility => WorkspaceMediaEvidenceKind::Accessibility,
        MediaEvidenceKind::CaptionTrack => WorkspaceMediaEvidenceKind::CaptionTrack,
        MediaEvidenceKind::PdfTextLayer => WorkspaceMediaEvidenceKind::PdfTextLayer,
        MediaEvidenceKind::VisualInference => WorkspaceMediaEvidenceKind::VisualInference,
        MediaEvidenceKind::TableHeuristic => WorkspaceMediaEvidenceKind::TableHeuristic,
    }
}

const fn media_field(value: MediaFactKind) -> &'static str {
    match value {
        MediaFactKind::Description => "media description",
        MediaFactKind::OcrText => "image text",
        MediaFactKind::Transcript => "video transcript",
        MediaFactKind::PdfText => "PDF text",
        MediaFactKind::PdfTableRow => "PDF table row",
        MediaFactKind::Metadata => "media metadata",
    }
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

#[cfg(test)]
mod tests {
    use crate::account::crypto::ReferenceSha256;

    use super::*;

    #[test]
    fn media_fact_derivation_keeps_coordinates_and_deduplicates_replay() {
        let entropy = TaskIdEntropy::new(core::array::from_fn(|index| {
            u8::try_from(index).unwrap_or_default()
        }))
        .unwrap_or_else(|_| unreachable!("fixture entropy is non-degenerate"));
        let fact = MediaFact {
            kind: MediaFactKind::PdfTableRow,
            evidence: MediaEvidenceKind::TableHeuristic,
            text: "Revenue 42".to_owned(),
            source_locator: "pdf:page/2".to_owned(),
            source_start: 10,
            source_end: 21,
            page_index_plus_one: 2,
            timestamp_start_ms: 0,
            timestamp_end_ms: 0,
            row_index_plus_one: 1,
            confidence_ppm: 650_000,
            truncated: false,
        };
        let facts = derive_media_facts(
            &[fact.clone(), fact],
            MediaObservationKind::Pdf,
            &entropy,
            "workspace-1",
            "source-1",
            "epoch-1",
            7,
            &ReferenceSha256,
        )
        .unwrap_or_else(|_| unreachable!("valid media facts derive"));
        assert_eq!(facts.len(), 1);
        let derived = facts
            .first()
            .unwrap_or_else(|| unreachable!("one fact remains after replay deduplication"));
        assert_eq!(derived.field, "PDF table row");
        assert_eq!(derived.value, "Revenue 42");
        assert!(derived
            .media_provenance
            .as_ref()
            .is_some_and(|media| media.location_descriptor()
                == "pdf:page/2; bytes 10..21; page 2; row 1; confidence 650000 ppm"));
    }
}
