// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Automatic workspace derivation and page-fact staging inputs.

use bip_types::ActionResultCode;
use loop_kernel::context::RedactedPageContent;
use policy_engine::origin::normalize_serialization;
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    initial_display_name, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate,
};
use task_engine::{Command, TaskState, TaskTemplateId};

use crate::account::Sha256Port;
use crate::contract::OperationId;
use crate::ports::{
    TaskEngineLoad, TaskEnginePort, TaskIdEntropy, WorkspacePageFact, WorkspacePersistRequest,
    WorkspacePort,
};

use super::{CoreRuntime, WorkspaceCommitError};

mod media;

use self::media::{derive_media_facts, workspace_ingestion_scope, WorkspaceIngestionScope};

const WORKSPACE_ID_DOMAIN: &[u8] = b"\0taffy.workspace-id.v1\0";
const PAGE_FACT_ID_DOMAIN: &[u8] = b"\0taffy.workspace-page-fact.v1\0";

pub(super) struct PageIngestion {
    pub workspace_id: String,
    pub source: WorkspaceSource,
    pub facts: Vec<WorkspacePageFact>,
}

impl CoreRuntime {
    pub(super) fn attach_derived_workspace(
        &self,
        load: &mut TaskEngineLoad,
    ) -> Result<bool, WorkspaceCommitError> {
        let TaskEngineLoad::Fresh {
            seed, id_entropy, ..
        } = load
        else {
            return Ok(false);
        };
        if seed.workspace_id.is_some() {
            return Ok(false);
        }
        let digest = self
            .components
            .digest
            .sha256(&domain_input(WORKSPACE_ID_DOMAIN, &[id_entropy.as_bytes()]))
            .map_err(|_| WorkspaceCommitError::DigestUnavailable)?;
        let mut bytes = [0_u8; 16];
        bytes.copy_from_slice(&digest[..16]);
        seed.workspace_id = Some(task_engine::WorkspaceId::from_bytes(bytes));
        Ok(true)
    }

    pub(super) fn prepare_page_ingestion(
        &self,
        task_id: &bip_types::identity::TaskId,
        command: &task_engine::CommandEnvelope,
    ) -> Result<Option<PageIngestion>, WorkspaceCommitError> {
        let Command::RecordActionOutcome { action_id, outcome } = &command.command else {
            return Ok(None);
        };
        if outcome.code != ActionResultCode::Verified {
            return Ok(None);
        }
        let Some(evidence) = outcome.observation.as_ref() else {
            return Ok(None);
        };
        let session = self
            .tasks
            .get(task_id.as_str())
            .ok_or(WorkspaceCommitError::InvalidTaskFacts)?;
        let action = session
            .task
            .action_effect_facts(action_id)
            .ok_or(WorkspaceCommitError::InvalidTaskFacts)?;
        let Some(scope) = workspace_ingestion_scope(action.proposal.intent()) else {
            return Ok(None);
        };
        if action.proposal.tab_id() != &evidence.tab_id {
            return Ok(None);
        }
        let task_facts = session
            .task
            .view_facts()
            .map_err(|_| WorkspaceCommitError::InvalidTaskFacts)?;
        let Some(workspace_id) = task_facts.workspace_id else {
            return Ok(None);
        };
        let Some(source) = task_facts.accepted_consent.as_ref().and_then(|consent| {
            consent.sources.iter().find(|source| {
                source.tab_id == evidence.tab_id
                    && source.normalized_origin == evidence.normalized_origin
            })
        }) else {
            return Ok(None);
        };
        let Some(page) = self.loop_state(task_id.as_str()).map(|state| &state.page) else {
            return Ok(None);
        };
        if !page.has_exact_observation(source, evidence) {
            return Ok(None);
        }
        let source_id = session
            .task
            .library_refresh_context()
            .and_then(|context| {
                let locator = source.canonical_locator.as_deref()?;
                context
                    .sources
                    .into_iter()
                    .find(|expected| expected.canonical_locator == locator)
                    .map(|expected| expected.source_id.to_text())
            })
            .unwrap_or_else(|| source.source_id.to_text());
        let facts = match scope {
            WorkspaceIngestionScope::Dom => {
                let Some(content) = page.redacted_workspace_content(evidence) else {
                    return Ok(None);
                };
                derive_page_facts(
                    &content,
                    &session.id_entropy,
                    &workspace_id,
                    &source_id,
                    evidence.page_epoch.0.as_str(),
                    evidence.graph_revision,
                    self.components.digest.as_ref(),
                )?
            }
            WorkspaceIngestionScope::Media(kind) => {
                let Some(media) = page.redacted_workspace_media_facts(evidence, kind) else {
                    return Ok(None);
                };
                derive_media_facts(
                    media,
                    kind,
                    &session.id_entropy,
                    &workspace_id,
                    &source_id,
                    evidence.page_epoch.0.as_str(),
                    evidence.graph_revision,
                    self.components.digest.as_ref(),
                )?
            }
        };
        Ok(Some(PageIngestion {
            workspace_id,
            source: observed_workspace_source(source, &source_id)?,
            facts,
        }))
    }

    pub(super) fn preflight_task_workspace_update(
        &self,
        workspace_id: Option<&str>,
    ) -> Result<(), WorkspaceCommitError> {
        let Some(workspace_id) = workspace_id else {
            return Ok(());
        };
        self.components
            .workspaces
            .preflight_task_update(workspace_id)
            .map_err(WorkspaceCommitError::Store)
    }

    pub(super) fn stage_task_workspace_update(
        &mut self,
        workspace_id: Option<&str>,
        plan: Option<PageIngestion>,
        phase: WorkspacePhase,
        operation_id: &OperationId,
        now_utc_millis: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceCommitError> {
        let Some(workspace_id) = workspace_id else {
            return Ok(None);
        };
        let page_facts = match plan {
            Some(plan) if plan.workspace_id == workspace_id => Some((plan.source, plan.facts)),
            Some(_) => return Err(WorkspaceCommitError::InvalidTaskFacts),
            None => None,
        };
        let result = match page_facts {
            Some((source, facts)) => {
                let source_id = source.source_id.to_text();
                self.components.workspaces.begin_task_update(
                    operation_id.as_str().to_owned(),
                    workspace_id,
                    phase,
                    Some((source_id.as_str(), facts)),
                    Some(source),
                    now_utc_millis,
                )
            }
            None => self.components.workspaces.begin_task_update(
                operation_id.as_str().to_owned(),
                workspace_id,
                phase,
                None,
                None,
                now_utc_millis,
            ),
        };
        result.map_err(WorkspaceCommitError::Store)
    }
}

fn observed_workspace_source(
    source: &task_engine::ConsentedSource,
    source_id: &str,
) -> Result<WorkspaceSource, WorkspaceCommitError> {
    let origin = normalize_serialization(&source.normalized_origin)
        .map_err(|_| WorkspaceCommitError::InvalidOrigin)?;
    if origin.display() != source.normalized_origin {
        return Err(WorkspaceCommitError::InvalidOrigin);
    }
    let host = origin
        .host()
        .ok_or(WorkspaceCommitError::InvalidOrigin)?
        .to_owned();
    Ok(WorkspaceSource {
        source_id: SourceId::parse(source_id)
            .map_err(|_| WorkspaceCommitError::InvalidIdentifier)?,
        title: host.clone(),
        host,
        canonical_locator: source.canonical_locator.clone(),
        read_at_epoch_ms: 0,
        excluded: false,
    })
}

pub(super) fn derive_page_facts(
    content: &RedactedPageContent,
    entropy: &TaskIdEntropy,
    workspace_id: &str,
    source_id: &str,
    page_epoch: &str,
    graph_revision: u64,
    digest_port: &dyn Sha256Port,
) -> Result<Vec<WorkspacePageFact>, WorkspaceCommitError> {
    let mut facts = Vec::with_capacity(content.values().len());
    for (ordinal, value) in content.values().iter().enumerate() {
        let ordinal = u64::try_from(ordinal)
            .map_err(|_| WorkspaceCommitError::InvalidTaskFacts)?
            .to_be_bytes();
        let revision = graph_revision.to_be_bytes();
        let input = domain_input(
            PAGE_FACT_ID_DOMAIN,
            &[
                entropy.as_bytes(),
                workspace_id.as_bytes(),
                source_id.as_bytes(),
                page_epoch.as_bytes(),
                &revision,
                &ordinal,
            ],
        );
        let digest = digest_port
            .sha256(&input)
            .map_err(|_| WorkspaceCommitError::DigestUnavailable)?;
        let mut bytes = [0_u8; 16];
        bytes.copy_from_slice(&digest[..16]);
        facts.push(WorkspacePageFact {
            fact_id: FactId::from_bytes(bytes).to_text(),
            field: "page".to_owned(),
            value: value.clone(),
            media_provenance: None,
        });
    }
    facts.sort_by(|left, right| left.fact_id.cmp(&right.fact_id));
    Ok(facts)
}

pub(super) fn initial_workspace_snapshot(
    task: &dyn TaskEnginePort,
    updated_at_epoch_ms: u64,
) -> Result<WorkspaceSnapshot, WorkspaceCommitError> {
    let facts = task
        .view_facts()
        .map_err(|_| WorkspaceCommitError::InvalidTaskFacts)?;
    let accepted = facts
        .accepted_consent
        .ok_or(WorkspaceCommitError::InvalidTaskFacts)?;
    let workspace_id = facts
        .workspace_id
        .as_deref()
        .ok_or(WorkspaceCommitError::InvalidIdentifier)
        .and_then(|value| {
            WorkspaceId::parse(value).map_err(|_| WorkspaceCommitError::InvalidIdentifier)
        })?;
    let refresh = task.library_refresh_context();
    let mut sources = Vec::with_capacity(
        refresh
            .as_ref()
            .map_or(accepted.sources.len(), |context| context.sources.len()),
    );
    if let Some(context) = refresh {
        for source in context.sources {
            sources.push(WorkspaceSource {
                source_id: SourceId::parse(&source.source_id.to_text())
                    .map_err(|_| WorkspaceCommitError::InvalidIdentifier)?,
                title: source.title,
                host: source.host,
                canonical_locator: Some(source.canonical_locator),
                read_at_epoch_ms: 0,
                excluded: false,
            });
        }
    } else {
        for source in accepted.sources {
            let origin = normalize_serialization(&source.normalized_origin)
                .map_err(|_| WorkspaceCommitError::InvalidOrigin)?;
            if origin.display() != source.normalized_origin {
                return Err(WorkspaceCommitError::InvalidOrigin);
            }
            let host = origin
                .host()
                .ok_or(WorkspaceCommitError::InvalidOrigin)?
                .to_owned();
            sources.push(WorkspaceSource {
                source_id: SourceId::parse(&source.source_id.to_text())
                    .map_err(|_| WorkspaceCommitError::InvalidIdentifier)?,
                title: host.clone(),
                host,
                canonical_locator: source.canonical_locator,
                read_at_epoch_ms: 0,
                excluded: false,
            });
        }
    }
    sources.sort_by_key(|source| source.source_id);
    let snapshot = WorkspaceSnapshot {
        workspace_id,
        revision: 1,
        display_name: initial_display_name(&facts.goal),
        goal: facts.goal,
        phase: workspace_phase_for_task(facts.state),
        saved: false,
        last_updated_epoch_ms: updated_at_epoch_ms,
        template: workspace_template(facts.template_id),
        sources,
        facts: Vec::new(),
    };
    snapshot
        .validate()
        .then_some(snapshot)
        .ok_or(WorkspaceCommitError::InvalidTaskFacts)
}

pub(super) const fn workspace_phase_for_task(state: TaskState) -> WorkspacePhase {
    match state {
        TaskState::AwaitingConsent | TaskState::WaitingUser => WorkspacePhase::WaitingForUser,
        TaskState::Paused => WorkspacePhase::Paused,
        TaskState::Completed => WorkspacePhase::Done,
        TaskState::Partial => WorkspacePhase::PartlyDone,
        TaskState::Cancelled => WorkspacePhase::Stopped,
        TaskState::Failed => WorkspacePhase::Failed,
        TaskState::Draft
        | TaskState::Queued
        | TaskState::Running
        | TaskState::Pausing
        | TaskState::Cancelling
        | TaskState::Completing => WorkspacePhase::Running,
    }
}

fn domain_input(domain: &[u8], segments: &[&[u8]]) -> Vec<u8> {
    let mut input = Vec::with_capacity(
        domain
            .len()
            .saturating_add(segments.iter().map(|value| value.len() + 8).sum()),
    );
    input.extend_from_slice(domain);
    for segment in segments {
        input.extend_from_slice(
            &u64::try_from(segment.len())
                .unwrap_or(u64::MAX)
                .to_be_bytes(),
        );
        input.extend_from_slice(segment);
    }
    input
}

const fn workspace_template(template: TaskTemplateId) -> WorkspaceTemplate {
    match template {
        TaskTemplateId::CompareProducts => WorkspaceTemplate::CompareProducts,
        TaskTemplateId::SummarizeEvidence => WorkspaceTemplate::SummarizeEvidence,
        TaskTemplateId::BuildSourceTable => WorkspaceTemplate::BuildSourceTable,
        TaskTemplateId::WebErrand => WorkspaceTemplate::WebErrand,
    }
}

#[cfg(test)]
mod tests;
