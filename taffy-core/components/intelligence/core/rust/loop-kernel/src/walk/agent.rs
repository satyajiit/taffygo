// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Replay-stable scheduling for the assistant loop.
//!
//! The reviewed workflow's sibling, and deliberately a separate module rather
//! than a second branch inside it: the reviewed workflow is a fixed sequence
//! this build performs without a model, and this one is a decision table read
//! against a turn. What they share is the *envelope*, and that is shared as
//! one function rather than as two copies of a digest input — an identity
//! derived two ways is an identity that can disagree with itself after a
//! restart, and the browser's effect journal is what would then be holding the
//! disagreement.

use procedure_engine::{ObservedFields, Procedure};
use task_engine::{
    AgentError, Command, CommandEnvelope, FailureReason, IdempotencyKey, PauseCause,
    PreModelObservation, TraceId, TurnResidency, REVIEWED_NO_MODEL_ROUTE_ID,
};

use crate::digest::Sha256Port;
use crate::ports::TaskEnginePort;
use crate::state::ProviderGapClass;

use super::procedure::{next_procedure_command_envelope, ProcedureWorkflowError};
use super::reviewed::{envelope_identity, next_reviewed_command_envelope, ReviewedWorkflowError};

/// Why the assistant loop could not produce a durable command envelope.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AgentWorkflowError {
    /// The loop refused to guess about the turn in front of it.
    Agent(AgentError),
    /// The local reviewed SHA-256 adapter was unavailable.
    DigestUnavailable,
    /// Bounded envelope identity material could not be represented.
    IdentityOverflow,
}

impl AgentWorkflowError {
    /// Content-free diagnostic spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Agent(error) => error.label(),
            Self::DigestUnavailable => "digest_unavailable",
            Self::IdentityOverflow => "identity_overflow",
        }
    }
}

impl From<ReviewedWorkflowError> for AgentWorkflowError {
    fn from(error: ReviewedWorkflowError) -> Self {
        match error {
            // The shared identity helper reaches only its own two failures;
            // a workflow refusal is unreachable through it and is mapped to
            // the overflow rather than being invented into an `AgentError`.
            ReviewedWorkflowError::Workflow(_) | ReviewedWorkflowError::IdentityOverflow => {
                Self::IdentityOverflow
            }
            ReviewedWorkflowError::DigestUnavailable => Self::DigestUnavailable,
        }
    }
}

/// Builds the next replay-stable agent envelope without applying it.
///
/// Calling this repeatedly at the same durable revision with the same
/// residency returns the same idempotency and trace identities, which is what
/// makes a restart resume rather than repeat.
pub fn next_agent_command_envelope(
    task: &dyn TaskEnginePort,
    residency: Option<&TurnResidency>,
    digest: &dyn Sha256Port,
) -> Result<Option<CommandEnvelope>, AgentWorkflowError> {
    next_agent_command_envelope_after_gap(task, residency, digest, None)
}

/// [`next_agent_command_envelope`], told what the provider answered when the
/// last turn ended in a gap.
///
/// The table's rows 8 to 10 end a task on any gap under one closed reason,
/// because the journal holds no more than that. The loop does hold more for
/// one pass, and it is applied here — before the identity is minted, so the
/// key names the command that is actually submitted.
pub fn next_agent_command_envelope_after_gap(
    task: &dyn TaskEnginePort,
    residency: Option<&TurnResidency>,
    digest: &dyn Sha256Port,
    gap_class: Option<ProviderGapClass>,
) -> Result<Option<CommandEnvelope>, AgentWorkflowError> {
    let adapter = super::reviewed::WorkflowDigestAdapter(digest);
    let Some(command) = task
        .next_agent_command(residency, &adapter)
        .map_err(AgentWorkflowError::Agent)?
    else {
        return Ok(None);
    };
    let command = refine_after_gap(command, gap_class);
    let revision = task.revision();
    let identity = envelope_identity(
        digest,
        task.task_id().as_str(),
        revision,
        command.kind().label(),
    )?;
    Ok(Some(CommandEnvelope::new(
        IdempotencyKey::new(format!("agent-{identity}")),
        revision,
        TraceId::new(format!("agent-{identity}")),
        command,
    )))
}

/// Turns the table's one provider failure into the state the answer deserves.
///
/// Only the plain provider failure is touched, and only when the loop still
/// holds the class behind it. A refused key or request fails the task under
/// its own reason, because asking again changes nothing. A limit or a lost
/// network pauses it instead, with a resume offered, because asking again
/// later is exactly what those two want (the ux-spec's failure table). Any
/// other command passes through untouched, whatever class is held.
fn refine_after_gap(command: Command, gap_class: Option<ProviderGapClass>) -> Command {
    let Command::FailTask {
        reason: FailureReason::ProviderUnavailable,
    } = command
    else {
        return command;
    };
    match gap_class {
        Some(ProviderGapClass::Refused) => Command::FailTask {
            reason: FailureReason::ProviderRefused,
        },
        Some(ProviderGapClass::Limit) => Command::PauseTask {
            cause: PauseCause::ProviderLimit,
        },
        Some(ProviderGapClass::Busy) => Command::PauseTask {
            cause: PauseCause::ProviderBusy,
        },
        Some(ProviderGapClass::Offline) => Command::PauseTask {
            cause: PauseCause::Offline,
        },
        None => command,
    }
}

/// The result of checking the transient-page prerequisite for a paid turn.
#[derive(Debug)]
pub enum PreModelObservationEnvelope {
    /// A matching verified page exists; the planned model command may stand.
    Ready,
    /// The read is already in the ordinary action pipeline.
    Waiting,
    /// This command replaces the model request for the current walk pass.
    Command(CommandEnvelope),
}

/// Builds the replay-stable command that observes a missing or stale page.
pub fn next_pre_model_observation_envelope(
    task: &dyn TaskEnginePort,
    live_observations: &[task_engine::LiveSourceObservation],
    digest: &dyn Sha256Port,
) -> Result<PreModelObservationEnvelope, AgentWorkflowError> {
    let adapter = super::reviewed::WorkflowDigestAdapter(digest);
    match task
        .next_pre_model_observation(live_observations, &adapter)
        .map_err(AgentWorkflowError::Agent)?
    {
        PreModelObservation::Ready => Ok(PreModelObservationEnvelope::Ready),
        PreModelObservation::Waiting => Ok(PreModelObservationEnvelope::Waiting),
        PreModelObservation::Command(command) => {
            let revision = task.revision();
            let identity = envelope_identity(
                digest,
                task.task_id().as_str(),
                revision,
                command.kind().label(),
            )?;
            Ok(PreModelObservationEnvelope::Command(CommandEnvelope::new(
                IdempotencyKey::new(format!("agent-observation-command-{identity}")),
                revision,
                TraceId::new(format!("agent-observation-command-{identity}")),
                command,
            )))
        }
    }
}

/// Which scheduler produces the next command for a task that disclosed `route`.
///
/// The reviewed table and the assistant table both return `None` when waiting.
/// Choosing by "the other table had nothing" would ask a model for a turn
/// while an observation is in flight. The consented route is the only fact
/// that is true of the task in both those states.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskWorkflow {
    /// `no_model_required`: the fixed provider-free sequence.
    Reviewed,
    /// Any other disclosed route: the assistant decision table.
    Agent,
    /// No route was disclosed, so neither scheduler owns the task.
    Unspecified,
}

/// Why neither scheduler could produce a durable command envelope.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DurableWorkflowError {
    /// An explicitly selected saved procedure refused its current shape.
    Procedure(ProcedureWorkflowError),
    /// The reviewed sequence refused its current durable shape.
    Reviewed(ReviewedWorkflowError),
    /// The assistant loop refused to guess about the turn in front of it.
    Agent(AgentWorkflowError),
}

impl DurableWorkflowError {
    /// Content-free diagnostic spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Procedure(error) => error.label(),
            Self::Reviewed(error) => error.label(),
            Self::Agent(error) => error.label(),
        }
    }
}

/// Picks the scheduler from the consented route, never from an empty answer.
pub fn workflow_for_provider_route(route: Option<&str>) -> TaskWorkflow {
    match route {
        Some(REVIEWED_NO_MODEL_ROUTE_ID) => TaskWorkflow::Reviewed,
        Some(_) => TaskWorkflow::Agent,
        None => TaskWorkflow::Unspecified,
    }
}

/// Builds the next replay-stable envelope for whichever scheduler the route owns.
///
/// The residency is read only by the assistant table. Passing it to a reviewed
/// task is harmless and ignored; omitting it from an agent task is how the
/// table waits on a paid call (row 5) rather than inventing a turn.
pub fn next_durable_command_envelope(
    task: &dyn TaskEnginePort,
    residency: Option<&TurnResidency>,
    digest: &dyn Sha256Port,
) -> Result<Option<CommandEnvelope>, DurableWorkflowError> {
    next_durable_command_envelope_with_procedure(task, None, residency, digest)
}

/// Builds the next envelope for an explicitly selected procedure, otherwise
/// for the scheduler owned by the consented provider route.
pub fn next_durable_command_envelope_with_procedure(
    task: &dyn TaskEnginePort,
    procedure: Option<&Procedure>,
    residency: Option<&TurnResidency>,
    digest: &dyn Sha256Port,
) -> Result<Option<CommandEnvelope>, DurableWorkflowError> {
    next_durable_command_envelope_after_gap(task, procedure, residency, digest, None)
}

/// [`next_durable_command_envelope_with_procedure`], carrying the provider's
/// answer behind the last gap to the assistant table; the other two
/// schedulers never ask a model and ignore it.
pub fn next_durable_command_envelope_after_gap(
    task: &dyn TaskEnginePort,
    procedure: Option<&Procedure>,
    residency: Option<&TurnResidency>,
    digest: &dyn Sha256Port,
    gap_class: Option<ProviderGapClass>,
) -> Result<Option<CommandEnvelope>, DurableWorkflowError> {
    if let Some(procedure) = procedure {
        return next_procedure_command_envelope(task, procedure, ObservedFields::none(), digest)
            .map_err(DurableWorkflowError::Procedure);
    }
    match workflow_for_provider_route(task.model_turn_facts().provider_route_id.as_deref()) {
        TaskWorkflow::Reviewed => {
            next_reviewed_command_envelope(task, digest).map_err(DurableWorkflowError::Reviewed)
        }
        TaskWorkflow::Agent => {
            next_agent_command_envelope_after_gap(task, residency, digest, gap_class)
                .map_err(DurableWorkflowError::Agent)
        }
        TaskWorkflow::Unspecified => Ok(None),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const PROVIDER_FAILURE: Command = Command::FailTask {
        reason: FailureReason::ProviderUnavailable,
    };

    #[test]
    fn a_refused_key_fails_the_task_under_its_own_reason() {
        assert_eq!(
            refine_after_gap(PROVIDER_FAILURE, Some(ProviderGapClass::Refused)),
            Command::FailTask {
                reason: FailureReason::ProviderRefused,
            }
        );
    }

    #[test]
    fn a_limit_and_a_lost_network_pause_the_task_to_be_resumed() {
        assert_eq!(
            refine_after_gap(PROVIDER_FAILURE, Some(ProviderGapClass::Limit)),
            Command::PauseTask {
                cause: PauseCause::ProviderLimit,
            }
        );
        assert_eq!(
            refine_after_gap(PROVIDER_FAILURE, Some(ProviderGapClass::Offline)),
            Command::PauseTask {
                cause: PauseCause::Offline,
            }
        );
    }

    #[test]
    fn without_a_class_or_for_any_other_command_nothing_changes() {
        assert_eq!(refine_after_gap(PROVIDER_FAILURE, None), PROVIDER_FAILURE);
        let other = Command::FailTask {
            reason: FailureReason::BudgetExhausted,
        };
        assert_eq!(
            refine_after_gap(other.clone(), Some(ProviderGapClass::Refused)),
            other
        );
        assert_eq!(
            refine_after_gap(Command::ExecutorStarted, Some(ProviderGapClass::Offline)),
            Command::ExecutorStarted
        );
    }
}
