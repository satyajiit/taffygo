// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exhaustive reducer-command transaction projections.
//!
//! The two functions here are the indexes of the persisted command surface:
//! every arm is one line naming its body, and the bodies live in
//! `command_body` (or a subject module such as `handover` and `turn`), so a
//! reader of the index sees the whole surface without stepping over a decode.

use bip_types::identity::{SemanticNodeId, TabId};
use core_service_types as wire;
use task_engine::field_values::{FieldNodeIds, FieldValueRequestId, SuppliedValueCount};
use task_engine::{Command, IdempotencyKey, PlanStepId, TraceId};

use super::action_value::{proposal, unproposal};
use super::command_body::{
    persisted_action_outcome, persisted_approval, persisted_complete_result, persisted_dispatch,
    persisted_eviction, persisted_fact_correction, persisted_model_turn,
    persisted_permission_request, persisted_permission_result, persisted_policy_decision,
    persisted_source_exclusion, persisted_tool_job_outcome, restored_action_outcome,
    restored_approval, restored_dispatch, restored_fact, restored_model_turn,
    restored_permission_request, restored_permission_result, restored_policy_decision,
    restored_source, restored_tool_job_outcome, uneviction,
};
use super::enum_task::{
    artifact, failure, pause, step_state, unartifact, unfailure, unpause, unstep_state,
};
use super::handover::{persisted_handover, restored_handover};
use super::value::{
    action, approval, artifact_id as restored_artifact_id, plan, preview, result, scope, unplan,
    unpreview, unresult, unscope,
};
use super::ConversionError;

// One arm per persisted variant, and the length is the size of the vocabulary
// rather than a function doing several things. Splitting it at an arbitrary
// point would hide the property that matters here: it is total, and a command
// added later is a compile error in this file rather than a payload that
// silently fails to journal.
#[allow(clippy::too_many_lines)]
pub(super) fn command(value: &Command) -> Result<wire::PersistedCommand, ConversionError> {
    Ok(match value {
        Command::CreateTask => wire::PersistedCommand::CreateTask,
        Command::EditScope(value) => wire::PersistedCommand::EditScope {
            scope: scope(value),
        },
        Command::StartTask(value) => wire::PersistedCommand::StartTask {
            preview: preview(value),
        },
        Command::AcceptInitialConsent(value) => wire::PersistedCommand::AcceptInitialConsent {
            approval: value.0.clone(),
        },
        Command::RecordDiscoveryTab {
            discovery_tab_id,
            browser_session_id,
        } => wire::PersistedCommand::RecordDiscoveryTab {
            discovery_tab_id: discovery_tab_id.as_str().to_owned(),
            browser_session_id: browser_session_id.as_str().to_owned(),
        },
        Command::ApproveAction {
            approval,
            still_current,
            expires_at_monotonic_ms,
            expires_at_utc_ms,
            browser_session_id,
        } => persisted_approval(
            approval,
            *still_current,
            *expires_at_monotonic_ms,
            *expires_at_utc_ms,
            browser_session_id,
        ),
        Command::DenyAction { approval } => wire::PersistedCommand::DenyAction {
            approval: approval.0.clone(),
        },
        Command::PauseTask { cause } => wire::PersistedCommand::PauseTask {
            cause: pause(*cause),
        },
        Command::TakeOver => wire::PersistedCommand::TakeOver,
        Command::PauseSettled => wire::PersistedCommand::PauseSettled,
        Command::ResumeTask => wire::PersistedCommand::ResumeTask,
        Command::CancelTask => wire::PersistedCommand::CancelTask,
        Command::CancelSettled => wire::PersistedCommand::CancelSettled,
        Command::ExecutorStarted => wire::PersistedCommand::ExecutorStarted,
        Command::SetPlan(value) => wire::PersistedCommand::SetPlan { plan: plan(value)? },
        Command::AdvanceStep { plan_step_id, to } => wire::PersistedCommand::AdvanceStep {
            plan_step_id: plan_step_id.as_str().to_owned(),
            to: step_state(*to),
        },
        Command::RequestApproval { action_id } => wire::PersistedCommand::RequestApproval {
            action_id: action_id.0.clone(),
        },
        Command::RequestUserInput => wire::PersistedCommand::RequestUserInput,
        Command::SupplyUserInput => wire::PersistedCommand::SupplyUserInput,
        Command::ProposeAction(value) => wire::PersistedCommand::ProposeAction {
            proposal: proposal(value)?,
        },
        Command::RecordPolicyDecision {
            action_id,
            decision: value,
            dispatch_id,
        } => persisted_policy_decision(action_id, value, dispatch_id.as_ref()),
        Command::DispatchAction {
            action_id,
            dispatch_id,
        } => persisted_dispatch(action_id, dispatch_id),
        Command::RecordActionOutcome {
            action_id,
            outcome: value,
        } => persisted_action_outcome(action_id, value),
        value @ (Command::RequestModelTurn { .. }
        | Command::RequestModelAttempt { .. }
        | Command::RecordModelTurn { .. }
        | Command::RecordModelTurnGap { .. }) => persisted_model_turn(value)?,
        Command::RecordContextEviction { through_turn } => persisted_eviction(*through_turn),
        Command::RecordToolJobOutcome {
            action_id,
            job_id,
            outcome,
        } => persisted_tool_job_outcome(action_id, job_id, outcome),
        Command::ResultCandidateReady => wire::PersistedCommand::ResultCandidateReady,
        Command::CompleteResultValidated(value) => persisted_complete_result(value),
        Command::PartialResultValidated(value) => wire::PersistedCommand::PartialResultValidated {
            result: result(value),
        },
        Command::ResumeForCorrection => wire::PersistedCommand::ResumeForCorrection,
        Command::FollowUp => wire::PersistedCommand::FollowUp,
        Command::FailTask { reason } => wire::PersistedCommand::FailTask {
            reason: failure(*reason),
        },
        Command::CorrectFact { fact_id } => persisted_fact_correction(*fact_id),
        Command::ExcludeSource { source_id } => persisted_source_exclusion(source_id),
        Command::RequestArtifact {
            artifact_id,
            format,
            workspace_revision,
        } => wire::PersistedCommand::RequestArtifact {
            artifact_id: artifact_id.as_str().to_owned(),
            format: artifact(*format),
            workspace_revision: *workspace_revision,
        },
        Command::AcceptArtifact { artifact_id } => wire::PersistedCommand::AcceptArtifact {
            artifact_id: artifact_id.as_str().to_owned(),
        },
        Command::ExportArtifact {
            artifact_id,
            format,
        } => wire::PersistedCommand::ExportArtifact {
            artifact_id: artifact_id.as_str().to_owned(),
            format: artifact(*format),
        },
        Command::RequestPermission(request) => persisted_permission_request(request),
        Command::RecordPermissionResult(result) => persisted_permission_result(result),
        value @ (Command::RequestHandover { .. }
        | Command::CompleteHandover(_)
        | Command::ExpireHandover { .. }) => persisted_handover(value)?,
        value @ (Command::RequestFieldValues { .. } | Command::SupplyFieldValues { .. }) => {
            persisted_field_values(value)?
        }
    })
}

// Total, one arm per variant, for the reason `command` above states.
#[allow(clippy::too_many_lines)]
pub(super) fn uncommand(value: wire::PersistedCommand) -> Result<Command, ConversionError> {
    Ok(match value {
        wire::PersistedCommand::CreateTask => Command::CreateTask,
        wire::PersistedCommand::EditScope { scope } => Command::EditScope(unscope(scope)?),
        wire::PersistedCommand::StartTask { preview } => Command::StartTask(unpreview(preview)?),
        wire::PersistedCommand::AcceptInitialConsent { approval: raw } => {
            Command::AcceptInitialConsent(approval(raw)?)
        }
        wire::PersistedCommand::RecordDiscoveryTab {
            discovery_tab_id,
            browser_session_id,
        } => {
            if !super::consent::valid_tab_id(&discovery_tab_id) {
                return Err(ConversionError::InvalidIdentifier);
            }
            Command::RecordDiscoveryTab {
                discovery_tab_id: TabId::new(discovery_tab_id),
                browser_session_id: task_engine::BrowserSessionId::new(browser_session_id)
                    .map_err(|_| ConversionError::InvalidIdentifier)?,
            }
        }
        wire::PersistedCommand::ApproveAction {
            approval: raw,
            still_current,
            expires_at_monotonic_ms,
            expires_at_utc_ms,
            browser_session_id,
        } => restored_approval(
            raw,
            still_current,
            expires_at_monotonic_ms,
            expires_at_utc_ms,
            browser_session_id,
        )?,
        wire::PersistedCommand::DenyAction { approval: raw } => Command::DenyAction {
            approval: approval(raw)?,
        },
        wire::PersistedCommand::PauseTask { cause } => Command::PauseTask {
            cause: unpause(cause),
        },
        wire::PersistedCommand::TakeOver => Command::TakeOver,
        wire::PersistedCommand::PauseSettled => Command::PauseSettled,
        wire::PersistedCommand::ResumeTask => Command::ResumeTask,
        wire::PersistedCommand::CancelTask => Command::CancelTask,
        wire::PersistedCommand::CancelSettled => Command::CancelSettled,
        wire::PersistedCommand::ExecutorStarted => Command::ExecutorStarted,
        wire::PersistedCommand::SetPlan { plan } => Command::SetPlan(unplan(plan)?),
        wire::PersistedCommand::AdvanceStep { plan_step_id, to } => Command::AdvanceStep {
            plan_step_id: PlanStepId::new(plan_step_id),
            to: unstep_state(to),
        },
        wire::PersistedCommand::RequestApproval { action_id } => Command::RequestApproval {
            action_id: action(action_id)?,
        },
        wire::PersistedCommand::RequestUserInput => Command::RequestUserInput,
        wire::PersistedCommand::SupplyUserInput => Command::SupplyUserInput,
        wire::PersistedCommand::ProposeAction { proposal } => {
            Command::ProposeAction(Box::new(unproposal(proposal)?))
        }
        wire::PersistedCommand::RecordPolicyDecision {
            action_id,
            decision: value,
            dispatch_id,
        } => restored_policy_decision(action_id, value, dispatch_id)?,
        wire::PersistedCommand::DispatchAction {
            action_id,
            dispatch_id,
        } => restored_dispatch(action_id, dispatch_id)?,
        wire::PersistedCommand::RecordActionOutcome {
            action_id,
            outcome: value,
        } => restored_action_outcome(action_id, value)?,
        wire::PersistedCommand::ResultCandidateReady => Command::ResultCandidateReady,
        wire::PersistedCommand::CompleteResultValidated { result: value } => {
            Command::CompleteResultValidated(unresult(value))
        }
        wire::PersistedCommand::PartialResultValidated { result: value } => {
            Command::PartialResultValidated(unresult(value))
        }
        wire::PersistedCommand::ResumeForCorrection => Command::ResumeForCorrection,
        wire::PersistedCommand::FollowUp => Command::FollowUp,
        wire::PersistedCommand::FailTask { reason } => Command::FailTask {
            reason: unfailure(reason),
        },
        wire::PersistedCommand::CorrectFact { fact_id } => restored_fact(&fact_id)?,
        wire::PersistedCommand::ExcludeSource { source_id } => restored_source(&source_id)?,
        wire::PersistedCommand::RequestArtifact {
            artifact_id,
            format,
            workspace_revision,
        } => {
            if workspace_revision == 0 {
                return Err(ConversionError::InvalidValue);
            }
            Command::RequestArtifact {
                artifact_id: restored_artifact_id(artifact_id)?,
                format: unartifact(format),
                workspace_revision,
            }
        }
        wire::PersistedCommand::AcceptArtifact { artifact_id } => Command::AcceptArtifact {
            artifact_id: restored_artifact_id(artifact_id)?,
        },
        wire::PersistedCommand::ExportArtifact {
            artifact_id,
            format,
        } => Command::ExportArtifact {
            artifact_id: restored_artifact_id(artifact_id)?,
            format: unartifact(format),
        },
        wire::PersistedCommand::RequestPermission { request } => {
            restored_permission_request(request)?
        }
        wire::PersistedCommand::RecordPermissionResult { result } => {
            restored_permission_result(result)?
        }
        // The three model-turn bodies had their ordinals frozen at
        // transaction version 7, ahead of the reducer that produces them
        // (decision 0052 section 2). They are restored rather than refused
        // now, and restoring them is the point: a replay that could not
        // reconstruct `RequestModelTurn` would reach a state before the first
        // model call and propose every paid call again.
        value @ (wire::PersistedCommand::RequestModelTurn { .. }
        | wire::PersistedCommand::RequestModelAttempt { .. }
        | wire::PersistedCommand::RecordModelTurn { .. }
        | wire::PersistedCommand::RecordModelTurnGap { .. }) => restored_model_turn(value)?,
        wire::PersistedCommand::RecordContextEviction { through_turn } => uneviction(through_turn),
        wire::PersistedCommand::RecordToolJobOutcome {
            action_id,
            job_id,
            outcome,
        } => restored_tool_job_outcome(action_id, job_id, &outcome),
        value @ (wire::PersistedCommand::RequestHandover { .. }
        | wire::PersistedCommand::CompleteHandover { .. }
        | wire::PersistedCommand::ExpireHandover { .. }) => restored_handover(value)?,
        value @ (wire::PersistedCommand::RequestFieldValues { .. }
        | wire::PersistedCommand::SupplyFieldValues { .. }) => restored_field_values(value)?,
    })
}

/// The two halves of one field-value request, going out (decision 0088).
///
/// Apart from `command` for the same reason `persisted_handover` is: it keeps
/// that match readable, and these two arms are one subject.
///
/// `SupplyFieldValues` carries a count and nothing else. What the person typed
/// was minted into the browser's vault and never entered this process, so a
/// journal entry holding anything more would be holding something the core
/// never had.
///
/// The ask's outcome is dropped here on purpose and is not an omission
/// (decision 0215). `TaskTransactionBatch` is versioned by exact equality, so
/// a member added to the durable format refuses every batch already written,
/// and an outcome is advice for one turn rather than a fact about the task: a
/// process that died and came back is past the turn the advice was for. The
/// restore below therefore answers `None`, which the turn renders as "the
/// reason did not survive a restart" — never as `Answered`, which would be
/// this process claiming the person did something.
///
/// The field identities are dropped for the same reason, on both halves
/// (decision 0238): the companions a request asked about beside the named
/// line, and the field each held value was minted for. Both are advice for the
/// fills that follow one answer, and a replay emits no effects and proposes
/// nothing, so neither is needed to rebuild the task. A task rebuilt part way
/// through placing values keeps the fill records it already journalled, which
/// is what says which positions are placed; the rest are the model's to fill.
fn persisted_field_values(value: &Command) -> Result<wire::PersistedCommand, ConversionError> {
    Ok(match value {
        Command::RequestFieldValues {
            request_id,
            tab_id,
            node_id,
            companion_node_ids: _,
        } => wire::PersistedCommand::RequestFieldValues {
            request_id: request_id.as_str().to_owned(),
            tab_id: tab_id.0.clone(),
            node_id: node_id.0.clone(),
        },
        Command::SupplyFieldValues {
            request_id,
            supplied,
            outcome: _,
            field_node_ids: _,
        } => wire::PersistedCommand::SupplyFieldValues {
            request_id: request_id.as_str().to_owned(),
            supplied: supplied.get(),
        },
        _ => return Err(ConversionError::InvalidValue),
    })
}

/// The same two halves coming back.
fn restored_field_values(value: wire::PersistedCommand) -> Result<Command, ConversionError> {
    Ok(match value {
        wire::PersistedCommand::RequestFieldValues {
            request_id,
            tab_id,
            node_id,
        } => Command::RequestFieldValues {
            request_id: FieldValueRequestId::new(request_id)
                .map_err(|_| ConversionError::InvalidIdentifier)?,
            tab_id: TabId::new(tab_id),
            node_id: SemanticNodeId::new(node_id),
            companion_node_ids: FieldNodeIds::none(),
        },
        wire::PersistedCommand::SupplyFieldValues {
            request_id,
            supplied,
        } => Command::SupplyFieldValues {
            request_id: FieldValueRequestId::new(request_id)
                .map_err(|_| ConversionError::InvalidIdentifier)?,
            supplied: SuppliedValueCount::new(supplied)
                .map_err(|_| ConversionError::InvalidIdentifier)?,
            outcome: None,
            field_node_ids: None,
        },
        _ => return Err(ConversionError::InvalidValue),
    })
}

pub(super) fn envelope(
    value: &task_engine::CommandEnvelope,
) -> Result<wire::PersistedCommandEnvelope, ConversionError> {
    Ok(wire::PersistedCommandEnvelope {
        idempotency_key: value.idempotency_key.as_str().to_owned(),
        expected_revision: value.expected_revision,
        trace_id: value.trace_id.as_str().to_owned(),
        command: command(&value.command)?,
    })
}

pub(super) fn unenvelope(
    value: wire::PersistedCommandEnvelope,
) -> Result<task_engine::CommandEnvelope, ConversionError> {
    if value.idempotency_key.is_empty() || value.trace_id.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(task_engine::CommandEnvelope::new(
        IdempotencyKey::new(value.idempotency_key),
        value.expected_revision,
        TraceId::new(value.trace_id),
        uncommand(value.command)?,
    ))
}
