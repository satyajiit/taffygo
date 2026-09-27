// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The command bodies the two persisted-command indexes name.
//!
//! `command` and `uncommand` (in `command.rs`) are the indexes of the
//! persisted command surface; a body written or decoded inline there is a
//! body a reader has to step over to read the index. Every body longer than a
//! line lives here instead, one function per persisted shape, in the pairing
//! the codec keeps everywhere: `persisted_*` writes, `restored_*` reads.

use bip_types::identity::{ActionId, DispatchId};
use core_service_types as wire;
use task_engine::{
    BrowserSessionId, Command, FactId, PermissionRequest, PermissionRequestId, PermissionResult,
    SourceId,
};

use super::action_value::{decision, outcome, undecision, unoutcome};
use super::enum_permission::{
    permission, permission_decision, unpermission, unpermission_decision,
};
use super::turn::{
    persisted_model_attempt, persisted_model_gap, persisted_model_reply, persisted_model_request,
    restored_model_attempt, restored_model_gap, restored_model_reply, restored_model_request,
};
use super::value::{action, approval, result};
use super::ConversionError;

pub(super) fn persisted_approval(
    approval: &bip_types::identity::ApprovalReceiptReference,
    still_current: bool,
    expires_at_monotonic_ms: u64,
    expires_at_utc_ms: u64,
    browser_session_id: &BrowserSessionId,
) -> wire::PersistedCommand {
    wire::PersistedCommand::ApproveAction {
        approval: approval.0.clone(),
        still_current,
        expires_at_monotonic_ms,
        expires_at_utc_ms,
        browser_session_id: browser_session_id.as_str().to_owned(),
    }
}

pub(super) fn persisted_permission_request(request: &PermissionRequest) -> wire::PersistedCommand {
    wire::PersistedCommand::RequestPermission {
        request: wire::PersistedPermissionRequest {
            request_id: request.request_id().as_str().to_owned(),
            permission: permission(request.permission()),
            deadline_monotonic_ms: request.deadline_monotonic_ms(),
            deadline_utc_ms: request.deadline_utc_ms(),
            browser_session_id: request.browser_session_id().as_str().to_owned(),
        },
    }
}

pub(super) fn persisted_permission_result(result: &PermissionResult) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordPermissionResult {
        result: wire::PersistedPermissionResult {
            request_id: result.request_id().as_str().to_owned(),
            permission: permission(result.permission()),
            decision: permission_decision(result.decision()),
        },
    }
}

/// The two record-identifier bodies, written.
pub(super) fn persisted_fact_correction(fact_id: task_engine::FactId) -> wire::PersistedCommand {
    wire::PersistedCommand::CorrectFact {
        fact_id: fact_id.to_text(),
    }
}

pub(super) fn persisted_source_exclusion(
    source_id: &task_engine::SourceId,
) -> wire::PersistedCommand {
    wire::PersistedCommand::ExcludeSource {
        source_id: source_id.to_text(),
    }
}

/// The four model-turn bodies, written. Grouped for the reason
/// [`super::handover::persisted_handover`] gives.
pub(super) fn persisted_model_turn(
    value: &Command,
) -> Result<wire::PersistedCommand, ConversionError> {
    Ok(match value {
        Command::RequestModelTurn { call_id } => persisted_model_request(call_id),
        Command::RequestModelAttempt {
            call_id,
            attempt_ordinal,
            candidate_ordinal,
            kind,
        } => persisted_model_attempt(call_id, *attempt_ordinal, *candidate_ordinal, *kind),
        Command::RecordModelTurn { call_id, digest } => persisted_model_reply(call_id, digest),
        Command::RecordModelTurnGap { call_id, gap } => persisted_model_gap(call_id, *gap),
        _ => return Err(ConversionError::InvalidValue),
    })
}

pub(super) fn persisted_policy_decision(
    action_id: &ActionId,
    value: &task_engine::ProposalDecision,
    dispatch_id: Option<&DispatchId>,
) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordPolicyDecision {
        action_id: action_id.0.clone(),
        decision: decision(value),
        dispatch_id: dispatch_id.map(|id| id.0.clone()),
    }
}

pub(super) fn restored_approval(
    raw: String,
    still_current: bool,
    expires_at_monotonic_ms: u64,
    expires_at_utc_ms: u64,
    browser_session_id: String,
) -> Result<Command, ConversionError> {
    Ok(Command::ApproveAction {
        approval: approval(raw)?,
        still_current,
        expires_at_monotonic_ms,
        expires_at_utc_ms,
        browser_session_id: BrowserSessionId::new(browser_session_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
    })
}

pub(super) fn restored_permission_request(
    request: wire::PersistedPermissionRequest,
) -> Result<Command, ConversionError> {
    let request = PermissionRequest::new(
        PermissionRequestId::new(request.request_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        unpermission(request.permission),
        request.deadline_monotonic_ms,
        request.deadline_utc_ms,
        BrowserSessionId::new(request.browser_session_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
    )
    .map_err(|_| ConversionError::InvalidValue)?;
    Ok(Command::RequestPermission(request))
}

pub(super) fn restored_permission_result(
    result: wire::PersistedPermissionResult,
) -> Result<Command, ConversionError> {
    Ok(Command::RecordPermissionResult(PermissionResult::new(
        PermissionRequestId::new(result.request_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        unpermission(result.permission),
        unpermission_decision(result.decision),
    )))
}

pub(super) fn restored_policy_decision(
    action_id: String,
    value: wire::PersistedProposalDecision,
    dispatch_id: Option<String>,
) -> Result<Command, ConversionError> {
    Ok(Command::RecordPolicyDecision {
        action_id: action(action_id)?,
        decision: Box::new(undecision(value)?),
        dispatch_id: dispatch_id.map(DispatchId),
    })
}

pub(super) fn restored_action_outcome(
    action_id: String,
    value: wire::PersistedActionOutcome,
) -> Result<Command, ConversionError> {
    Ok(Command::RecordActionOutcome {
        action_id: action(action_id)?,
        outcome: Box::new(unoutcome(value)?),
    })
}

pub(super) fn restored_dispatch(
    action_id: String,
    dispatch_id: String,
) -> Result<Command, ConversionError> {
    if dispatch_id.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(Command::DispatchAction {
        action_id: action(action_id)?,
        dispatch_id: DispatchId(dispatch_id),
    })
}

pub(super) fn persisted_complete_result(value: &task_engine::TaskResult) -> wire::PersistedCommand {
    wire::PersistedCommand::CompleteResultValidated {
        result: result(value),
    }
}

pub(super) fn persisted_dispatch(
    action_id: &ActionId,
    dispatch_id: &DispatchId,
) -> wire::PersistedCommand {
    wire::PersistedCommand::DispatchAction {
        action_id: action_id.0.clone(),
        dispatch_id: dispatch_id.0.clone(),
    }
}

pub(super) fn persisted_action_outcome(
    action_id: &ActionId,
    value: &task_engine::ActionOutcome,
) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordActionOutcome {
        action_id: action_id.0.clone(),
        outcome: outcome(value),
    }
}

pub(super) fn persisted_tool_job_outcome(
    action_id: &ActionId,
    job_id: &task_engine::ToolJobId,
    outcome: &task_engine::ToolJobOutcome,
) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordToolJobOutcome {
        action_id: action_id.0.clone(),
        job_id: job_id.as_str().to_owned(),
        outcome: wire::PersistedToolJobOutcome {
            status: super::enum_action::tool_job_status(outcome.status),
            output_digest: outcome.output_digest.clone(),
            output_bytes: outcome.output_bytes,
            output_chunks: outcome.output_chunks,
        },
    }
}

pub(super) fn restored_tool_job_outcome(
    action_id: String,
    job_id: String,
    outcome: &wire::PersistedToolJobOutcome,
) -> Command {
    Command::RecordToolJobOutcome {
        action_id: ActionId::new(action_id),
        job_id: task_engine::ToolJobId::new(job_id),
        outcome: Box::new(task_engine::ToolJobOutcome {
            status: super::enum_action::untool_job_status(outcome.status),
            output_digest: outcome.output_digest.clone(),
            output_bytes: outcome.output_bytes,
            output_chunks: outcome.output_chunks,
        }),
    }
}

pub(super) const fn persisted_eviction(through_turn: u64) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordContextEviction { through_turn }
}

pub(super) const fn uneviction(through_turn: u64) -> Command {
    Command::RecordContextEviction { through_turn }
}

pub(super) fn restored_fact(fact_id: &str) -> Result<Command, ConversionError> {
    Ok(Command::CorrectFact {
        fact_id: FactId::parse(fact_id).map_err(|_| ConversionError::InvalidIdentifier)?,
    })
}

pub(super) fn restored_source(source_id: &str) -> Result<Command, ConversionError> {
    Ok(Command::ExcludeSource {
        source_id: SourceId::parse(source_id).map_err(|_| ConversionError::InvalidIdentifier)?,
    })
}

/// The three model-turn bodies, restored. Grouped for the reason the
/// handover bodies are.
pub(super) fn restored_model_turn(
    value: wire::PersistedCommand,
) -> Result<Command, ConversionError> {
    Ok(match value {
        wire::PersistedCommand::RequestModelTurn { call_id } => restored_model_request(call_id)?,
        wire::PersistedCommand::RequestModelAttempt {
            call_id,
            attempt_ordinal,
            candidate_ordinal,
            kind,
        } => restored_model_attempt(call_id, attempt_ordinal, candidate_ordinal, kind)?,
        wire::PersistedCommand::RecordModelTurn { call_id, digest } => {
            restored_model_reply(call_id, &digest)?
        }
        wire::PersistedCommand::RecordModelTurnGap { call_id, gap } => {
            restored_model_gap(call_id, gap)?
        }
        _ => return Err(ConversionError::InvalidValue),
    })
}
