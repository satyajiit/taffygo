// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic workspace export through the task-engine artifact renderer.

use core_api_types::{WorkspaceExportFormat, WorkspaceExportView, MAX_EXPORT_CONTENT_BYTES};
use taffy_storage::workspace::{
    FactKind, WorkspaceFact, WorkspaceMediaEvidenceKind, WorkspaceSnapshot,
};
use task_engine::{
    generate_within, ArtifactId, ArtifactKind, ArtifactRequest, CitedFact, DeletionState, Fact,
    FactClassification, FactId, FactStatus, Ownership, ProvenanceId, ProvenanceKind,
    ProvenanceLocator, Sensitivity, Source, SourceId, SourceKind, WorkspaceId,
};

use super::workspace_export_time::{decode_id, timestamp_from_epoch_ms};
use super::workspace_store::WorkspaceStore;
use crate::ports::{WorkspaceExportError, WorkspaceStoreError};

/// One rendered revision waiting for an explicit export request.
///
/// It is deliberately not a [`WorkspaceExportView`]. That generated view is
/// published to Android, so storing a draft in it would make model-requested
/// bytes exportable before the person asked to save them. A committed
/// workspace mutation clears this record together with the published export.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct PreparedWorkspaceArtifact {
    artifact_id: String,
    workspace_id: String,
    revision: u64,
    format: WorkspaceExportFormat,
    content: String,
}

impl PreparedWorkspaceArtifact {
    fn matches(
        &self,
        artifact_id: &str,
        workspace_id: &str,
        revision: u64,
        format: WorkspaceExportFormat,
    ) -> bool {
        self.artifact_id == artifact_id
            && self.workspace_id == workspace_id
            && self.revision == revision
            && self.format == format
    }

    pub(crate) fn belongs_to(&self, workspace_id: &str) -> bool {
        self.workspace_id == workspace_id
    }

    pub(crate) fn confirmation_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        for value in [
            self.artifact_id.as_bytes(),
            self.workspace_id.as_bytes(),
            self.content.as_bytes(),
        ] {
            bytes.extend_from_slice(&u64::try_from(value.len()).unwrap_or(u64::MAX).to_be_bytes());
            bytes.extend_from_slice(value);
        }
        bytes.extend_from_slice(&self.revision.to_be_bytes());
        bytes.push(match self.format {
            WorkspaceExportFormat::Markdown => 0,
            WorkspaceExportFormat::Csv => 1,
        });
        bytes
    }
}

impl WorkspaceStore {
    /// Renders one exact revision without publishing it as exportable bytes.
    ///
    /// This is the local half of artifact generation. The model can ask for
    /// the deterministic file, but only [`Self::request_export`] promotes the
    /// current prepared bytes into `latest_export` for Android's document
    /// picker flow.
    pub fn prepare_export(
        &mut self,
        request_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<(), WorkspaceExportError> {
        let prepared = self.render_export(request_id, workspace_id, expected_revision, format)?;
        self.set_prepared_export(prepared);
        Ok(())
    }

    pub fn request_export(
        &mut self,
        request_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        let revision = self
            .snapshot(workspace_id)
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?
            .revision;
        if revision != expected_revision {
            return Err(WorkspaceExportError::StaleRevision);
        }
        let content = match self.prepared_export() {
            Some(prepared) if prepared.matches(request_id, workspace_id, revision, format) => {
                prepared.content.clone()
            }
            Some(_) | None => {
                self.render_export(request_id, workspace_id, expected_revision, format)?
                    .content
            }
        };
        let view = WorkspaceExportView {
            request_id: request_id.to_owned(),
            workspace_id: workspace_id.to_owned(),
            revision,
            format,
            content,
        };
        self.set_latest_export(view.clone());
        Ok(view)
    }

    /// Renders the current workspace revision under one durable artifact id.
    pub fn prepare_artifact(
        &mut self,
        artifact_id: &str,
        workspace_id: &str,
        format: WorkspaceExportFormat,
    ) -> Result<u64, WorkspaceExportError> {
        let revision = self
            .snapshot(workspace_id)
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?
            .revision;
        self.prepare_export(artifact_id, workspace_id, revision, format)?;
        Ok(revision)
    }

    /// Recreates a prepared artifact after a process restart without
    /// publishing it to Android.
    pub fn ensure_artifact(
        &mut self,
        artifact_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<(), WorkspaceExportError> {
        if self.prepared_export().is_some_and(|prepared| {
            prepared.matches(artifact_id, workspace_id, expected_revision, format)
        }) {
            return Ok(());
        }
        self.prepare_export(artifact_id, workspace_id, expected_revision, format)
    }

    /// Publishes only the exact prepared artifact and current evidence
    /// revision named by an accepted task result.
    pub fn publish_artifact(
        &mut self,
        request_id: &str,
        artifact_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        self.ensure_artifact(artifact_id, workspace_id, expected_revision, format)?;
        let content = self
            .prepared_export()
            .filter(|prepared| {
                prepared.matches(artifact_id, workspace_id, expected_revision, format)
            })
            .map(|prepared| prepared.content.clone())
            .ok_or(WorkspaceExportError::RenderRefused)?;
        let view = WorkspaceExportView {
            request_id: request_id.to_owned(),
            workspace_id: workspace_id.to_owned(),
            revision: expected_revision,
            format,
            content,
        };
        self.set_latest_export(view.clone());
        Ok(view)
    }

    fn render_export(
        &self,
        request_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<PreparedWorkspaceArtifact, WorkspaceExportError> {
        let snapshot = self
            .snapshot(workspace_id)
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?;
        if snapshot.revision != expected_revision {
            return Err(WorkspaceExportError::StaleRevision);
        }
        let request = artifact_request(request_id, snapshot)?;
        let kind = match format {
            WorkspaceExportFormat::Markdown => ArtifactKind::Markdown,
            WorkspaceExportFormat::Csv => ArtifactKind::Csv,
        };
        let limit = u64::try_from(MAX_EXPORT_CONTENT_BYTES)
            .map_err(|_| WorkspaceExportError::RenderRefused)?;
        let artifact = generate_within(&request, kind, limit)
            .map_err(|_| WorkspaceExportError::RenderRefused)?;
        Ok(PreparedWorkspaceArtifact {
            artifact_id: request_id.to_owned(),
            workspace_id: workspace_id.to_owned(),
            revision: snapshot.revision,
            format,
            content: artifact.content,
        })
    }
}

fn artifact_request(
    request_id: &str,
    snapshot: &WorkspaceSnapshot,
) -> Result<ArtifactRequest, WorkspaceExportError> {
    let workspace_id = WorkspaceId::parse(&snapshot.workspace_id.to_text())
        .map_err(|_| WorkspaceExportError::InvalidEvidence)?;
    let sources = snapshot
        .sources
        .iter()
        .filter(|source| !source.excluded)
        .map(source_record)
        .collect::<Result<Vec<_>, _>>()?;
    let mut facts = Vec::new();
    for fact in &snapshot.facts {
        let active = fact
            .sources
            .iter()
            .filter(|source_id| {
                snapshot
                    .source(**source_id)
                    .is_some_and(|source| !source.excluded)
            })
            .copied()
            .collect::<Vec<_>>();
        if fact.kind != FactKind::UserEntered && active.is_empty() {
            continue;
        }
        facts.push(cited_fact(snapshot, workspace_id, fact, &active)?);
        if let Some(correction) = &fact.correction {
            facts.push(correction_fact(snapshot, workspace_id, fact, correction)?);
        }
    }
    if facts.is_empty() {
        return Err(WorkspaceExportError::NoExportableFacts);
    }
    Ok(ArtifactRequest {
        artifact_id: ArtifactId::new(request_id),
        title: snapshot.display_name.clone(),
        schema_version: 1,
        sources,
        facts,
    })
}

fn source_record(
    source: &taffy_storage::workspace::WorkspaceSource,
) -> Result<Source, WorkspaceExportError> {
    let timestamp = timestamp_from_epoch_ms(source.read_at_epoch_ms)?;
    Ok(Source {
        source_id: SourceId::parse(&source.source_id.to_text())
            .map_err(|_| WorkspaceExportError::InvalidEvidence)?,
        kind: SourceKind::WebPage,
        canonical_locator: None,
        display_locator: source.host.clone(),
        origin: Some(source.host.clone()),
        title: Some(source.title.clone()),
        first_seen_at: timestamp.clone(),
        last_observed_at: Some(timestamp),
        ownership: Ownership::External,
        sensitivity: Sensitivity::Public,
        retention_class: "workspace".to_owned(),
        deletion_state: DeletionState::Active,
    })
}

fn cited_fact(
    snapshot: &WorkspaceSnapshot,
    workspace_id: WorkspaceId,
    fact: &WorkspaceFact,
    active_sources: &[taffy_storage::ids::SourceId],
) -> Result<CitedFact, WorkspaceExportError> {
    let fact_id = FactId::parse(&fact.fact_id.to_text())
        .map_err(|_| WorkspaceExportError::InvalidEvidence)?;
    let observed_ms = active_sources
        .iter()
        .filter_map(|id| snapshot.source(*id))
        .map(|source| source.read_at_epoch_ms)
        .max()
        .unwrap_or(snapshot.last_updated_epoch_ms);
    let provenance = active_sources
        .iter()
        .map(|source_id| provenance(fact_id, *source_id, observed_ms, fact))
        .collect::<Result<Vec<_>, _>>()?;
    Ok(CitedFact {
        fact: Fact {
            fact_id,
            workspace_id,
            subject_key: template_key(snapshot),
            predicate: fact.field.clone(),
            typed_value: fact.value.clone(),
            unit: None,
            classification: classification(fact.kind),
            confidence_basis_points: None,
            observation_time: timestamp_from_epoch_ms(observed_ms)?,
            sensitivity: Sensitivity::Public,
            status: if fact.correction.is_some() {
                FactStatus::Corrected
            } else {
                FactStatus::Accepted
            },
            supersedes_fact_id: None,
            retention_class: "workspace".to_owned(),
        },
        provenance,
    })
}

fn correction_fact(
    snapshot: &WorkspaceSnapshot,
    workspace_id: WorkspaceId,
    fact: &WorkspaceFact,
    correction: &str,
) -> Result<CitedFact, WorkspaceExportError> {
    let mut corrected = decode_id(&fact.fact_id.to_text())?;
    for byte in &mut corrected {
        *byte ^= 0xff;
    }
    Ok(CitedFact {
        fact: Fact {
            fact_id: FactId::from_bytes(corrected),
            workspace_id,
            subject_key: "user_correction".to_owned(),
            predicate: fact.field.clone(),
            typed_value: correction.to_owned(),
            unit: None,
            classification: FactClassification::UserEntered,
            confidence_basis_points: None,
            observation_time: timestamp_from_epoch_ms(snapshot.last_updated_epoch_ms)?,
            sensitivity: Sensitivity::Personal,
            status: FactStatus::Accepted,
            supersedes_fact_id: Some(
                FactId::parse(&fact.fact_id.to_text())
                    .map_err(|_| WorkspaceExportError::InvalidEvidence)?,
            ),
            retention_class: "workspace".to_owned(),
        },
        provenance: Vec::new(),
    })
}

fn provenance(
    fact_id: FactId,
    source_id: taffy_storage::ids::SourceId,
    observed_ms: u64,
    fact: &WorkspaceFact,
) -> Result<ProvenanceLocator, WorkspaceExportError> {
    let source_text = source_id.to_text();
    let source =
        SourceId::parse(&source_text).map_err(|_| WorkspaceExportError::InvalidEvidence)?;
    let left = decode_id(&fact_id.to_text())?;
    let right = decode_id(&source_text)?;
    let mut identity = [0u8; 16];
    for ((output, left), right) in identity.iter_mut().zip(left).zip(right) {
        *output = left ^ right;
    }
    Ok(ProvenanceLocator {
        provenance_id: ProvenanceId::from_bytes(identity),
        fact_id,
        source_id: source,
        observation_id: None,
        kind: match fact.kind {
            FactKind::FromPage => {
                fact.media_provenance
                    .as_ref()
                    .map_or(ProvenanceKind::Dom, |media| match media.evidence_kind {
                        WorkspaceMediaEvidenceKind::Dom => ProvenanceKind::Dom,
                        WorkspaceMediaEvidenceKind::Accessibility => ProvenanceKind::Accessibility,
                        WorkspaceMediaEvidenceKind::CaptionTrack
                        | WorkspaceMediaEvidenceKind::PdfTextLayer
                        | WorkspaceMediaEvidenceKind::TableHeuristic => {
                            ProvenanceKind::StructuredData
                        }
                        WorkspaceMediaEvidenceKind::VisualInference => ProvenanceKind::Tool,
                    })
            }
            FactKind::Summarized | FactKind::TaffyInference => ProvenanceKind::Model,
            FactKind::UserEntered => ProvenanceKind::User,
        },
        location_descriptor: fact
            .media_provenance
            .as_ref()
            .map(taffy_storage::workspace::WorkspaceMediaProvenance::location_descriptor),
        extraction_rule_version: None,
        transformation_chain: if fact.media_provenance.is_some() {
            "workspace_media_snapshot_v1"
        } else {
            "workspace_snapshot_v1"
        }
        .to_owned(),
        captured_at: timestamp_from_epoch_ms(observed_ms)?,
    })
}

const fn classification(kind: FactKind) -> FactClassification {
    match kind {
        FactKind::FromPage => FactClassification::Extracted,
        FactKind::Summarized => FactClassification::Summarized,
        FactKind::TaffyInference => FactClassification::Inferred,
        FactKind::UserEntered => FactClassification::UserEntered,
    }
}

fn template_key(snapshot: &WorkspaceSnapshot) -> String {
    match snapshot.template {
        taffy_storage::workspace::WorkspaceTemplate::CompareProducts => "compare_products",
        taffy_storage::workspace::WorkspaceTemplate::SummarizeEvidence => "summarize_evidence",
        taffy_storage::workspace::WorkspaceTemplate::BuildSourceTable => "build_source_table",
        taffy_storage::workspace::WorkspaceTemplate::WebErrand => "web_errand",
    }
    .to_owned()
}
