// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One immutable workspace revision to one validated rich file.
//!
//! The workspace is the source of content and attribution. A model chooses a
//! registered format; it does not author bytes, archive members, formulas, or
//! an uncited replacement document. This adapter projects the portable
//! workspace model into the file engine's narrow sourced-report interface and
//! returns bytes only after that module has validated its complete output.

use file_engine::{
    generate_sourced_report, FindingBasis, Format, GeneratedFile, Limits, ReportFinding,
    ReportSource, SourcedReport,
};
use taffy_storage::workspace::{FactKind, WorkspaceSnapshot, WorkspaceTemplate};
use task_engine::ArtifactKind;

use crate::ports::WorkspaceExportError;

/// Render one exact workspace revision into a bounded validated rich file.
pub(crate) fn generate_workspace_file(
    snapshot: &WorkspaceSnapshot,
    expected_revision: u64,
    format: Format,
) -> Result<GeneratedFile, WorkspaceExportError> {
    if snapshot.revision != expected_revision {
        return Err(WorkspaceExportError::StaleRevision);
    }
    let report = sourced_report(snapshot)?;
    let limits = Limits {
        max_output_bytes: core_service_types::MAX_TASK_ARTIFACT_EXPORT_BYTES,
        max_text_bytes: core_service_types::MAX_WORKSPACE_SNAPSHOT_BYTES,
        ..Limits::default()
    };
    generate_sourced_report(format, &report, limits)
        .map_err(|_| WorkspaceExportError::RenderRefused)
}

/// Maps only the formats this adapter owns; Markdown and CSV retain their
/// existing workspace renderer and cannot accidentally cross this seam.
pub(crate) const fn format_for_artifact(kind: ArtifactKind) -> Option<Format> {
    match kind {
        ArtifactKind::Xlsx => Some(Format::Xlsx),
        ArtifactKind::Pdf => Some(Format::Pdf),
        ArtifactKind::Docx => Some(Format::Docx),
        ArtifactKind::Pptx => Some(Format::Pptx),
        ArtifactKind::Markdown
        | ArtifactKind::Csv
        | ArtifactKind::WaveAudio
        | ArtifactKind::FrameArchive => None,
    }
}

fn sourced_report(snapshot: &WorkspaceSnapshot) -> Result<SourcedReport, WorkspaceExportError> {
    if !snapshot.validate() {
        return Err(WorkspaceExportError::InvalidEvidence);
    }
    let sources = snapshot
        .sources
        .iter()
        .filter(|source| !source.excluded)
        .map(|source| ReportSource {
            key: source.source_id.to_text(),
            title: source.title.clone(),
            display_locator: source.host.clone(),
        })
        .collect();
    let mut findings = Vec::new();
    for fact in &snapshot.facts {
        let active_sources = fact
            .sources
            .iter()
            .filter_map(|source_id| {
                snapshot
                    .source(*source_id)
                    .filter(|source| !source.excluded)
                    .map(|source| source.source_id.to_text())
            })
            .collect::<Vec<_>>();
        if fact.kind != FactKind::UserEntered && active_sources.is_empty() {
            continue;
        }
        findings.push(ReportFinding {
            subject: template_subject(snapshot.template).to_owned(),
            field: fact.media_provenance.as_ref().map_or_else(
                || fact.field.clone(),
                |media| format!("{} ({})", fact.field, media.location_descriptor()),
            ),
            value: fact.value.clone(),
            basis: finding_basis(fact.kind),
            source_keys: active_sources,
        });
        if let Some(correction) = &fact.correction {
            findings.push(ReportFinding {
                subject: "Your correction".to_owned(),
                field: fact.field.clone(),
                value: correction.clone(),
                basis: FindingBasis::User,
                source_keys: Vec::new(),
            });
        }
    }
    if findings.is_empty() {
        return Err(WorkspaceExportError::NoExportableFacts);
    }
    Ok(SourcedReport {
        title: snapshot.display_name.clone(),
        sources,
        findings,
    })
}

const fn finding_basis(kind: FactKind) -> FindingBasis {
    match kind {
        FactKind::FromPage => FindingBasis::Page,
        FactKind::Summarized => FindingBasis::Summary,
        FactKind::TaffyInference => FindingBasis::Inference,
        FactKind::UserEntered => FindingBasis::User,
    }
}

const fn template_subject(template: WorkspaceTemplate) -> &'static str {
    match template {
        WorkspaceTemplate::CompareProducts => "Compared items",
        WorkspaceTemplate::SummarizeEvidence => "Summary",
        WorkspaceTemplate::BuildSourceTable => "Source table",
        WorkspaceTemplate::WebErrand => "Web task",
    }
}
