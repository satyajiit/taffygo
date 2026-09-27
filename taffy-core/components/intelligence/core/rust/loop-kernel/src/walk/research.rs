// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rebind a validated research result to the exact retained workspace revision.

use task_engine::{Command, CommandEnvelope, IdempotencyKey, TaskResult, TraceId};

use super::{PlannedCommand, WalkError};
use crate::digest::Sha256Port;

/// The composition owner validated retained facts; the walk still owns every
/// identifier. A changed workspace revision must never reuse the empty-result
/// command's identity, even when the task revision itself did not change.
pub fn bind_research_result(
    mut planned: PlannedCommand,
    task_id: &str,
    workspace_revision: u64,
    result: TaskResult,
    digest: &dyn Sha256Port,
) -> Result<PlannedCommand, WalkError> {
    let identity = super::reviewed::envelope_identity(
        digest,
        task_id,
        planned.envelope.expected_revision,
        &format!("research-result-{workspace_revision}"),
    )
    .map_err(|error| WalkError::Scheduler(super::DurableWorkflowError::Agent(error.into())))?;
    let key = format!("agent-research-result-{identity}");
    let operation_id = format!("agent-operation-{key}");
    if operation_id.len() > core_service_types::MAX_IDENTIFIER_BYTES {
        return Err(WalkError::IdentifierTooLong);
    }
    planned.envelope = CommandEnvelope::new(
        IdempotencyKey::new(key.clone()),
        planned.envelope.expected_revision,
        TraceId::new(key.clone()),
        Command::CompleteResultValidated(result),
    );
    planned.operation.operation_id = operation_id;
    planned.operation.idempotency_key = key;
    Ok(planned)
}
