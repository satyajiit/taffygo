// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one walk: what a task is asked next, and under which identity.
//!
//! Decision 0072 moves the ordering rules here from the service bridge, so a
//! host test can drive them. The order is fixed: a refused model call settles
//! first, because every choice made after it would be made against a turn the
//! reducer still believes is outstanding; loop-local calls settle on the
//! residency next; then whichever scheduler the consented route owns answers,
//! and the envelope's replay-stable identity is minted. Everything here is
//! pure given `now_monotonic_ms` — the bridge keeps transport and sequencing,
//! and stops making loop decisions.

use core_service_types as wire;
use procedure_engine::Procedure;
use task_engine::{Command, CommandEnvelope, FailureReason, IdempotencyKey, TaskState, TraceId};

use crate::digest::Sha256Port;
use crate::ports::TaskEnginePort;
use crate::state::LoopState;
use crate::turn::person_answer::ask_subject_from_residency;

use super::agent::DurableWorkflowError;
use super::agent::{PreModelObservationEnvelope, TaskWorkflow};

#[cfg(test)]
mod tests;

/// How long the browser is given to commit one walk-chosen command.
pub const WORKFLOW_COMMAND_DEADLINE_MS: u64 = 30_000;
/// How long a model call is given, first attempt and retries alike.
///
/// Longer than the other commands because the effect this command carries
/// waits on a provider: a reasoning model over a page-sized transcript takes
/// its time, and the transport's own timeout follows the operation deadline.
/// Thirty seconds ended slow turns as "couldn't reach your provider" while
/// the provider was still answering.
pub const MODEL_CALL_DEADLINE_MS: u64 = 120_000;

// A model call is the one command allowed longer than the workflow deadline;
// the two constants may not drift into the other order.
const _: () = assert!(MODEL_CALL_DEADLINE_MS > WORKFLOW_COMMAND_DEADLINE_MS);
const REVIEWED_OPERATION_PREFIX: &str = "reviewed-operation-";
const AGENT_OPERATION_PREFIX: &str = "agent-operation-";
const PROCEDURE_OPERATION_PREFIX: &str = "procedure-operation-";
const MODEL_GAP_KEY_PREFIX: &str = "model-gap-";
const MODEL_GAP_TRACE_PREFIX: &str = "model-gap-trace-";
const CONTEXT_EVICTION_KEY_PREFIX: &str = "context-eviction-";
const CONTEXT_EVICTION_TRACE_PREFIX: &str = "context-eviction-trace-";
const ARTIFACT_FAILURE_KEY_PREFIX: &str = "artifact-failure-";
const ARTIFACT_FAILURE_TRACE_PREFIX: &str = "artifact-failure-trace-";
const DISCOVERY_FAILURE_KEY_PREFIX: &str = "discovery-failure-";
const DISCOVERY_FAILURE_TRACE_PREFIX: &str = "discovery-failure-trace-";

/// One command the walk chose, with the operation identity it travels under.
#[derive(Debug)]
pub struct PlannedCommand {
    pub envelope: CommandEnvelope,
    pub operation: wire::OperationEnvelope,
    /// Whether this plan staged an ask prompt that must be rolled back if the
    /// submission is not accepted.
    pub staged_ask: bool,
}

/// Why the walk could not truthfully answer.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WalkError {
    /// A task names a procedure version absent from the restored catalogue.
    SelectedProcedureUnavailable,
    /// The route-owned scheduler could not compute its command.
    Scheduler(DurableWorkflowError),
    /// A minted identifier exceeded the wire bound.
    IdentifierTooLong,
    /// The deadline arithmetic overflowed.
    DeadlineOverflow,
}

/// Chooses and identifies the next durable command for one task.
///
/// `Ok(None)` means there is truthfully nothing to start: the reducer is
/// waiting or terminal and no refused call is outstanding. The caller submits
/// the returned command and rolls the staged ask back when admission does not
/// accept it; nothing here has applied anything.
pub fn advance(
    task: &dyn TaskEnginePort,
    state: &mut LoopState,
    digest: &dyn Sha256Port,
    service_generation: u64,
    now_monotonic_ms: u64,
) -> Result<Option<PlannedCommand>, WalkError> {
    advance_with_procedure(
        task,
        None,
        None,
        state,
        digest,
        service_generation,
        now_monotonic_ms,
    )
}

/// Chooses through the same walk, giving an explicitly selected procedure
/// ownership of scheduling when one was frozen into the task. A configured
/// tool allowlist can only narrow the task's reviewed set and is applied to
/// loop-local settlement before scheduling.
pub fn advance_with_procedure(
    task: &dyn TaskEnginePort,
    procedure: Option<&Procedure>,
    configured_tool_allowlist: Option<&[String]>,
    state: &mut LoopState,
    digest: &dyn Sha256Port,
    service_generation: u64,
    now_monotonic_ms: u64,
) -> Result<Option<PlannedCommand>, WalkError> {
    // A call that could not be planned is settled before anything else is
    // asked of this task. The reducer charged the budget and put the turn in
    // flight when it asked, so until the gap is durable the task is waiting
    // on an answer to a request that was never made. A refusal for a call the
    // reducer is no longer holding answers nothing and is dropped: the gap it
    // would record was already recorded, or the task ended.
    if let Some(refused) = state.refused.take() {
        if task.model_turn_in_flight().as_deref() == Some(refused.call_id.as_str()) {
            return gap_command(task, refused, service_generation, now_monotonic_ms).map(Some);
        }
    }
    // A discovery bootstrap the browser definitely refused ends the task
    // under one closed reason. It is ended from the walk rather than from the
    // bridge because the reducer admits `FailTask` only once the task is
    // running: the flag outlives the `ExecutorStarted` hop that gets a queued
    // task there, and is cleared here, when the command recording it is
    // minted. Until then the scheduler is not asked, so no model turn is
    // composed for a task that has nowhere to look.
    if state.discovery_unavailable && task_is_running(task) {
        state.discovery_unavailable = false;
        return discovery_failure_command(task.revision(), service_generation, now_monotonic_ms)
            .map(Some);
    }
    settle_loop_calls_with_configuration(task, state, configured_tool_allowlist);
    // The provider's answer behind the last gap is good for exactly one
    // scheduler pass: the pass that turns the gap into a state. Taken rather
    // than read, so a class left over from a turn the table did not end the
    // task on cannot colour a later, unrelated failure.
    let gap_class = state.gap_class.take();
    let scheduled = if let Some(procedure) = procedure {
        super::procedure::next_live_procedure_command_envelope(task, procedure, state, digest)
            .map_err(DurableWorkflowError::Procedure)
    } else {
        super::agent::next_durable_command_envelope_after_gap(
            task,
            None,
            state.residency.as_ref(),
            digest,
            gap_class,
        )
    };
    let Some(mut envelope) = scheduled.map_err(WalkError::Scheduler)? else {
        return Ok(None);
    };
    // A paid turn never composes over an empty or stale arena. Only the Agent
    // scheduler reaches this branch; reviewed and procedure paths keep their
    // own observation sequencing. The replacement is an ordinary proposal,
    // so policy and browser dispatch remain the only authority path.
    let agent_route = procedure.is_none()
        && super::agent::workflow_for_provider_route(
            task.model_turn_facts().provider_route_id.as_deref(),
        ) == TaskWorkflow::Agent;
    if agent_route && matches!(envelope.command, Command::RequestModelTurn { .. }) {
        let sources = task.pre_model_observation_sources();
        let live_observations = state.page.matching_observations(&sources);
        match super::agent::next_pre_model_observation_envelope(task, &live_observations, digest)
            .map_err(|error| WalkError::Scheduler(DurableWorkflowError::Agent(error)))?
        {
            PreModelObservationEnvelope::Ready => {}
            PreModelObservationEnvelope::Waiting => return Ok(None),
            PreModelObservationEnvelope::Command(observation) => envelope = observation,
        }
    }
    // A model turn is requested only over a conversation whose shape is
    // already durable. If the byte ladder would drop turns past the journaled
    // boundary, the boundary moves first — one bookkeeping command through
    // the same one-command walk — and the next pass requests the turn over a
    // transcript the floor alone determines, byte-stable across replays
    // (decision 0074).
    if matches!(envelope.command, Command::RequestModelTurn { .. }) {
        let facts = task.model_turn_facts();
        if let crate::window::WindowPlan::Evict { through_turn } =
            crate::window::plan_context_window(&facts.transcript)
        {
            return eviction_command(task, through_turn, service_generation, now_monotonic_ms)
                .map(Some);
        }
    }
    let staged_ask = matches!(envelope.command, Command::RequestUserInput)
        .then(|| {
            state
                .residency
                .as_ref()
                .and_then(ask_subject_from_residency)
        })
        .flatten();
    let staged = staged_ask.is_some();
    if let Some(prompt) = staged_ask {
        state.ask_prompt = Some(prompt);
    }
    let idempotency_key = envelope.idempotency_key.as_str().to_owned();
    let prefix = if idempotency_key.starts_with("agent-") {
        AGENT_OPERATION_PREFIX
    } else if idempotency_key.starts_with("procedure-") {
        PROCEDURE_OPERATION_PREFIX
    } else {
        REVIEWED_OPERATION_PREFIX
    };
    let operation_id = format!("{prefix}{idempotency_key}");
    let operation = operation_envelope(
        operation_id,
        idempotency_key,
        service_generation,
        envelope.expected_revision,
        now_monotonic_ms,
        command_deadline_ms(&envelope.command),
    )?;
    Ok(Some(PlannedCommand {
        envelope,
        operation,
        staged_ask: staged,
    }))
}

/// The deadline a command travels under: a model call gets the long one.
const fn command_deadline_ms(command: &Command) -> u64 {
    match command {
        Command::RequestModelTurn { .. } | Command::RequestModelAttempt { .. } => {
            MODEL_CALL_DEADLINE_MS
        }
        _ => WORKFLOW_COMMAND_DEADLINE_MS,
    }
}

/// Settles every pending loop-local call on this task's residency.
///
/// Search, activate and spawn run here: no browser effect, no capability.
/// Activated names and a nested goal are copied onto the loop state so the
/// next compose can see them after this turn's residency is replaced. Names
/// are appended in activation order and never removed, which is what keeps
/// the composed prompt prefix byte-stable as the tool catalog grows.
pub fn settle_loop_calls(task: &dyn TaskEnginePort, state: &mut LoopState) {
    settle_loop_calls_with_configuration(task, state, None);
}

/// Settles loop-local calls after applying a live configuration narrowing.
///
/// The task allowlist is the reviewed upper bound. Configuration is applied as
/// a subset operation over that bound, exactly as it is during model request
/// composition, so a setting changed between turns cannot make search reveal
/// or activation load a tool the next request will omit.
fn settle_loop_calls_with_configuration(
    task: &dyn TaskEnginePort,
    state: &mut LoopState,
    configured_tool_allowlist: Option<&[String]>,
) {
    if state.residency.is_none() {
        return;
    }
    let facts = task.model_turn_facts();
    let reviewed_tools = task_engine::EffectiveToolSet::for_template(
        facts.template_id,
        facts.milestone,
        &facts.tool_allowlist,
    );
    let effective_tools = match configured_tool_allowlist {
        Some(allowlist) => reviewed_tools.narrow_by(allowlist),
        None => reviewed_tools,
    };
    let Some(residency) = state.residency.as_mut() else {
        return;
    };
    let mut names: Vec<&str> = state.activated.clone();
    names.extend(residency.activated_names().iter().copied());
    let mut tools = effective_tools.clone().with_activated(&names);
    loop {
        let pending = task
            .pending_loop_call(residency)
            .map(|(sequence, call, entry)| (sequence, call.clone(), entry));
        let Some((sequence, call, entry)) = pending else {
            break;
        };
        let (outcome, result) = if entry.name == task_engine::TABLE_RESHAPE_TOOL {
            crate::native_table::reshape_call(&call)
        } else {
            task_engine::loop_tool_result(entry, &call, &tools)
        };
        let activated = matches!(
            outcome,
            task_engine::LoopOutcome::Activated { name_known: true }
        );
        if !residency.settle_loop_with_result(sequence, outcome, result) {
            break;
        }
        if activated {
            names.clear();
            names.extend(state.activated.iter().copied());
            names.extend(residency.activated_names().iter().copied());
            tools = effective_tools.clone().with_activated(&names);
        }
    }
    for name in residency.activated_names() {
        if !state.activated.contains(name) {
            state.activated.push(*name);
        }
    }
    if let Some(goal) = residency.nested_goal() {
        state.nested_goal = Some(goal.to_owned());
    }
}

/// Records that one model call produced no reply, and why, as a durable fact.
///
/// The identities are derived from the call rather than from a counter,
/// because the call identity is itself derived from the task and the turn
/// ordinal: a replay reaches the same key and the journal refuses the second
/// application instead of recording one gap twice.
fn gap_command(
    task: &dyn TaskEnginePort,
    refused: crate::state::RefusedModelCall,
    service_generation: u64,
    now_monotonic_ms: u64,
) -> Result<PlannedCommand, WalkError> {
    let revision = task.revision();
    let call = refused.call_id.as_str();
    let idempotency_key = format!("{MODEL_GAP_KEY_PREFIX}{call}");
    let operation_id = format!("{REVIEWED_OPERATION_PREFIX}{idempotency_key}");
    let trace_id = format!("{MODEL_GAP_TRACE_PREFIX}{call}");
    if trace_id.len() > wire::MAX_IDENTIFIER_BYTES {
        return Err(WalkError::IdentifierTooLong);
    }
    let operation = operation_envelope(
        operation_id,
        idempotency_key.clone(),
        service_generation,
        revision,
        now_monotonic_ms,
        WORKFLOW_COMMAND_DEADLINE_MS,
    )?;
    let envelope = CommandEnvelope::new(
        IdempotencyKey::new(idempotency_key),
        revision,
        TraceId::new(trace_id),
        Command::RecordModelTurnGap {
            call_id: refused.call_id,
            gap: refused.gap,
        },
    );
    Ok(PlannedCommand {
        envelope,
        operation,
        staged_ask: false,
    })
}

/// Moves the durable eviction boundary before the next model turn.
///
/// The identities are derived from the boundary rather than from a counter:
/// the boundary is strictly monotone within one task, so a replay reaches the
/// same key and the journal refuses the second application rather than
/// recording one eviction twice.
fn eviction_command(
    task: &dyn TaskEnginePort,
    through_turn: u64,
    service_generation: u64,
    now_monotonic_ms: u64,
) -> Result<PlannedCommand, WalkError> {
    let revision = task.revision();
    let idempotency_key = format!("{CONTEXT_EVICTION_KEY_PREFIX}{through_turn}");
    let operation_id = format!("{REVIEWED_OPERATION_PREFIX}{idempotency_key}");
    let trace_id = format!("{CONTEXT_EVICTION_TRACE_PREFIX}{through_turn}");
    if trace_id.len() > wire::MAX_IDENTIFIER_BYTES {
        return Err(WalkError::IdentifierTooLong);
    }
    let operation = operation_envelope(
        operation_id,
        idempotency_key.clone(),
        service_generation,
        revision,
        now_monotonic_ms,
        WORKFLOW_COMMAND_DEADLINE_MS,
    )?;
    let envelope = CommandEnvelope::new(
        IdempotencyKey::new(idempotency_key),
        revision,
        TraceId::new(trace_id),
        Command::RecordContextEviction { through_turn },
    );
    Ok(PlannedCommand {
        envelope,
        operation,
        staged_ask: false,
    })
}

/// Ends a task under one replay-stable closed reason when artifact preflight
/// cannot truthfully produce bytes.
///
/// Rendering happens before the reducer records `RequestArtifact`, so there is
/// no artifact command to settle on refusal. A terminal command is preferable
/// to turning one bad workspace into a core outage or leaving the same model
/// call spinning forever. Its identity names the requested artifact and the
/// closed failure, never page or model text.
pub fn artifact_failure_command(
    task_revision: u64,
    artifact_id: &str,
    reason: FailureReason,
    service_generation: u64,
    now_monotonic_ms: u64,
) -> Result<PlannedCommand, WalkError> {
    let idempotency_key = format!(
        "{ARTIFACT_FAILURE_KEY_PREFIX}{artifact_id}-{}",
        reason.label()
    );
    let operation_id = format!("{AGENT_OPERATION_PREFIX}{idempotency_key}");
    let trace_id = format!(
        "{ARTIFACT_FAILURE_TRACE_PREFIX}{artifact_id}-{}",
        reason.label()
    );
    if trace_id.len() > wire::MAX_IDENTIFIER_BYTES {
        return Err(WalkError::IdentifierTooLong);
    }
    let operation = operation_envelope(
        operation_id,
        idempotency_key.clone(),
        service_generation,
        task_revision,
        now_monotonic_ms,
        WORKFLOW_COMMAND_DEADLINE_MS,
    )?;
    Ok(PlannedCommand {
        envelope: CommandEnvelope::new(
            IdempotencyKey::new(idempotency_key),
            task_revision,
            TraceId::new(trace_id),
            Command::FailTask { reason },
        ),
        operation,
        staged_ask: false,
    })
}

/// Whether the reducer would admit a terminal failure for this task now.
fn task_is_running(task: &dyn TaskEnginePort) -> bool {
    task.view_facts()
        .is_ok_and(|facts| facts.state == TaskState::Running)
}

/// Ends a task whose zero-source discovery bootstrap the browser refused.
///
/// The reducer emitted `PrepareDiscoveryTab` because the errand named no
/// source, so a refusal leaves the task with nowhere to look and no way to
/// be given one: `SourcesUnavailable` is the closed reason that says so. The
/// identity names the task revision the verdict was reached at and the
/// reason, never a tab or a page, and a replay reaches the same key.
pub fn discovery_failure_command(
    task_revision: u64,
    service_generation: u64,
    now_monotonic_ms: u64,
) -> Result<PlannedCommand, WalkError> {
    let reason = FailureReason::SourcesUnavailable;
    let idempotency_key = format!("{DISCOVERY_FAILURE_KEY_PREFIX}{}", reason.label());
    let operation_id = format!("{REVIEWED_OPERATION_PREFIX}{idempotency_key}");
    let trace_id = format!(
        "{DISCOVERY_FAILURE_TRACE_PREFIX}{task_revision}-{}",
        reason.label()
    );
    if trace_id.len() > wire::MAX_IDENTIFIER_BYTES {
        return Err(WalkError::IdentifierTooLong);
    }
    let operation = operation_envelope(
        operation_id,
        idempotency_key.clone(),
        service_generation,
        task_revision,
        now_monotonic_ms,
        WORKFLOW_COMMAND_DEADLINE_MS,
    )?;
    Ok(PlannedCommand {
        envelope: CommandEnvelope::new(
            IdempotencyKey::new(idempotency_key),
            task_revision,
            TraceId::new(trace_id),
            Command::FailTask { reason },
        ),
        operation,
        staged_ask: false,
    })
}

fn operation_envelope(
    operation_id: String,
    idempotency_key: String,
    service_generation: u64,
    task_revision: u64,
    now_monotonic_ms: u64,
    deadline_ms: u64,
) -> Result<wire::OperationEnvelope, WalkError> {
    if operation_id.len() > wire::MAX_IDENTIFIER_BYTES
        || idempotency_key.len() > wire::MAX_IDENTIFIER_BYTES
    {
        return Err(WalkError::IdentifierTooLong);
    }
    let deadline_monotonic_ms = now_monotonic_ms
        .checked_add(deadline_ms)
        .ok_or(WalkError::DeadlineOverflow)?;
    Ok(wire::OperationEnvelope {
        operation_id,
        service_generation,
        task_revision,
        deadline_monotonic_ms,
        idempotency_key,
    })
}
