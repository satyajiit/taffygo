// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Replay-stable scheduling for an explicitly selected saved procedure.

use procedure_engine::{ObservedFields, Procedure, ReplayRefusal};
use task_engine::{Command, CommandEnvelope, IdempotencyKey, TraceId};

use crate::digest::Sha256Port;
use crate::ports::TaskEnginePort;

use super::reviewed::{envelope_identity, ReviewedWorkflowError, WorkflowDigestAdapter};

/// Why a saved procedure could not produce a durable command envelope.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProcedureWorkflowError {
    /// The selected definition refused the reducer's current durable shape.
    Replay(ReplayRefusal),
    /// The local SHA-256 adapter was unavailable.
    DigestUnavailable,
    /// Bounded envelope identity material could not be represented.
    IdentityOverflow,
}

impl ProcedureWorkflowError {
    /// Content-free diagnostic spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Replay(_) => "procedure_replay_refused",
            Self::DigestUnavailable => "digest_unavailable",
            Self::IdentityOverflow => "identity_overflow",
        }
    }
}

impl From<ReviewedWorkflowError> for ProcedureWorkflowError {
    fn from(error: ReviewedWorkflowError) -> Self {
        match error {
            ReviewedWorkflowError::DigestUnavailable => Self::DigestUnavailable,
            ReviewedWorkflowError::Workflow(_) | ReviewedWorkflowError::IdentityOverflow => {
                Self::IdentityOverflow
            }
        }
    }
}

/// Builds the next replay-stable procedure envelope without applying it.
pub fn next_procedure_command_envelope(
    task: &dyn TaskEnginePort,
    procedure: &Procedure,
    observed: ObservedFields<'_>,
    digest: &dyn Sha256Port,
) -> Result<Option<CommandEnvelope>, ProcedureWorkflowError> {
    let adapter = WorkflowDigestAdapter(digest);
    let Some(command) = task
        .next_procedure_command(procedure, observed, &adapter)
        .map_err(ProcedureWorkflowError::Replay)?
    else {
        return Ok(None);
    };
    command_envelope(task, command, digest).map(Some)
}

/// Supplies the selected procedure with this generation's complete page and
/// manager-bound download progress. Neither is reconstructed from a record.
pub(super) fn next_live_procedure_command_envelope(
    task: &dyn TaskEnginePort,
    procedure: &Procedure,
    state: &crate::state::LoopState,
    digest: &dyn Sha256Port,
) -> Result<Option<CommandEnvelope>, ProcedureWorkflowError> {
    let sources = task.pre_model_observation_sources();
    let projection = match sources.as_slice() {
        [source] => state.page.procedure_page(source),
        _ => None,
    };
    let browser_session_id = task.browser_session_id();
    let completed_downloads = if browser_session_id.is_some()
        && state.task_downloads.browser_session_id() == browser_session_id
    {
        state.task_downloads.completed_task_downloads()
    } else {
        0
    };
    let pending_downloads = (state.task_downloads.browser_session_id() == browser_session_id)
        .then(|| state.task_downloads.pending_task_downloads())
        .flatten();
    let page = projection
        .as_ref()
        .map(|projection| procedure_engine::ReplayPage {
            evidence: projection.evidence,
            nodes: &projection.nodes,
            browser_session_id,
            completed_downloads,
            pending_downloads,
        });
    let adapter = WorkflowDigestAdapter(digest);
    let Some(command) = task
        .next_procedure_command_with_page(procedure, ObservedFields::none(), page, &adapter)
        .map_err(ProcedureWorkflowError::Replay)?
    else {
        return Ok(None);
    };
    command_envelope(task, command, digest).map(Some)
}

fn command_envelope(
    task: &dyn TaskEnginePort,
    command: Command,
    digest: &dyn Sha256Port,
) -> Result<CommandEnvelope, ProcedureWorkflowError> {
    let revision = task.revision();
    let identity = envelope_identity(
        digest,
        task.task_id().as_str(),
        revision,
        command.kind().label(),
    )?;
    Ok(CommandEnvelope::new(
        IdempotencyKey::new(format!("procedure-{identity}")),
        revision,
        TraceId::new(format!("procedure-{identity}")),
        command,
    ))
}
