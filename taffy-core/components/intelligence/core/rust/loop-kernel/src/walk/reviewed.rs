// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Replay-stable scheduling for the one reviewed provider-free workflow.

use core::fmt::Write as _;

use task_engine::{CommandEnvelope, IdempotencyKey, TraceId, WorkflowDigest, WorkflowError};

use crate::digest::{DigestError, Sha256Port};
use crate::ports::TaskEnginePort;

const ENVELOPE_DOMAIN: &[u8] = b"taffy/reviewed-workflow-envelope/v1";

/// Why the reviewed scheduler could not produce a durable command envelope.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReviewedWorkflowError {
    /// The reducer-owned workflow refused its current durable shape.
    Workflow(WorkflowError),
    /// The local reviewed SHA-256 adapter was unavailable.
    DigestUnavailable,
    /// Bounded envelope identity material could not be represented.
    IdentityOverflow,
}

impl ReviewedWorkflowError {
    /// Content-free diagnostic spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Workflow(error) => error.label(),
            Self::DigestUnavailable => "digest_unavailable",
            Self::IdentityOverflow => "identity_overflow",
        }
    }
}

/// Builds the next replay-stable envelope without applying it.
///
/// Calling this repeatedly at the same durable revision returns the same
/// idempotency and trace identities. The caller must submit the envelope via
/// `CoreRuntime::begin_submit`; it must never publish reducer effects directly.
pub fn next_reviewed_command_envelope(
    task: &dyn TaskEnginePort,
    digest: &dyn Sha256Port,
) -> Result<Option<CommandEnvelope>, ReviewedWorkflowError> {
    let adapter = WorkflowDigestAdapter(digest);
    let Some(command) = task
        .next_reviewed_command(&adapter)
        .map_err(ReviewedWorkflowError::Workflow)?
    else {
        return Ok(None);
    };
    let revision = task.revision();
    let identity = envelope_identity(
        digest,
        task.task_id().as_str(),
        revision,
        command.kind().label(),
    )?;
    Ok(Some(CommandEnvelope::new(
        IdempotencyKey::new(format!("reviewed-{identity}")),
        revision,
        TraceId::new(format!("reviewed-{identity}")),
        command,
    )))
}

pub(super) struct WorkflowDigestAdapter<'a>(pub(super) &'a dyn Sha256Port);

impl WorkflowDigest for WorkflowDigestAdapter<'_> {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], WorkflowError> {
        self.0
            .sha256(input)
            .map_err(|DigestError::Unavailable| WorkflowError::DigestUnavailable)
    }
}

pub(super) fn envelope_identity(
    digest: &dyn Sha256Port,
    task_id: &str,
    revision: u64,
    command_kind: &str,
) -> Result<String, ReviewedWorkflowError> {
    let mut material = Vec::with_capacity(
        ENVELOPE_DOMAIN
            .len()
            .saturating_add(task_id.len())
            .saturating_add(command_kind.len())
            .saturating_add(24),
    );
    material.extend_from_slice(ENVELOPE_DOMAIN);
    append_bounded(&mut material, task_id)?;
    material.extend_from_slice(&revision.to_be_bytes());
    append_bounded(&mut material, command_kind)?;
    let bytes = digest
        .sha256(&material)
        .map_err(|DigestError::Unavailable| ReviewedWorkflowError::DigestUnavailable)?;
    let mut output = String::with_capacity(64);
    for byte in bytes {
        write!(&mut output, "{byte:02x}").map_err(|_| ReviewedWorkflowError::IdentityOverflow)?;
    }
    Ok(output)
}

fn append_bounded(out: &mut Vec<u8>, value: &str) -> Result<(), ReviewedWorkflowError> {
    let length = u32::try_from(value.len()).map_err(|_| ReviewedWorkflowError::IdentityOverflow)?;
    out.extend_from_slice(&length.to_be_bytes());
    out.extend_from_slice(value.as_bytes());
    Ok(())
}
