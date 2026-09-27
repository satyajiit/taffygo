// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Representative reducer-command payloads used by transition tests.

use bip_types::identity::{ActionId, DispatchId, SemanticNodeId, TabId};
use task_engine::authority::ActorLeaseId;
use task_engine::command::{Command, CommandKind as K, PauseCause};
use task_engine::field_values::{
    field_value_request_id_for_call, FieldValueAskOutcome, FieldValueRequestId, SuppliedValueCount,
};
use task_engine::handover::{handover_id_for_call, HandoverCompletion, HandoverId, PersonInput};
use task_engine::permission::{PermissionDecision, PermissionResult};
use task_engine::plan::StepState;
use task_engine::task::SourceScope;
use task_engine::{ArtifactId, BrowserSessionId};

use super::{
    authorize, complete_result, fact_id, partial_result, permission_request, permission_result,
    plan_draft, preview, proposal, receipt, source_id, turn_digest, verified_outcome, Fixture,
};

/// A granted result for whatever native prompt the fixture is holding, or a
/// stand-in when it is holding none.
fn granted(fixture: &Fixture) -> PermissionResult {
    fixture
        .reducer
        .pending_permission()
        .map_or_else(permission_result, |pending| {
            PermissionResult::new(
                pending.request_id().clone(),
                pending.permission(),
                PermissionDecision::Granted,
            )
        })
}

/// The handover identity the fixtures open, which is also the one the agent
/// loop derives for the first call of the first turn.
fn probe_handover() -> HandoverId {
    handover_id_for_call(0, 0)
}

fn probe_field_value_request() -> FieldValueRequestId {
    field_value_request_id_for_call(0, 0)
}

/// The two halves of one field-value request (decision 0088), kept out of
/// `command_for` so that function stays under the file's line cap.
fn field_value_command(kind: K) -> Command {
    match kind {
        K::SupplyFieldValues => Command::SupplyFieldValues {
            request_id: probe_field_value_request(),
            supplied: SuppliedValueCount::new(1).unwrap_or_else(|_| unreachable!()),
            outcome: Some(FieldValueAskOutcome::Answered),
            field_node_ids: None,
        },
        _ => Command::RequestFieldValues {
            request_id: probe_field_value_request(),
            tab_id: TabId::new("tab_probe"),
            node_id: SemanticNodeId::new("node_probe"),
            companion_node_ids: task_engine::FieldNodeIds::none(),
        },
    }
}

/// The handover the fixture is holding, or a stand-in when it holds none.
fn open_handover(fixture: &Fixture) -> HandoverId {
    fixture
        .reducer
        .pending_handover()
        .cloned()
        .unwrap_or_else(probe_handover)
}

/// A completion for whatever handover the fixture is holding.
///
/// The two lease identities differ, so the cell exercises
/// `Guard::PendingHandoverMatches` rather than tripping over
/// `Guard::ResumptionLeaseIsNew` on the way to it. The reused-lease refusal has
/// its own test, where it is the subject rather than an obstacle.
fn resumed(fixture: &Fixture) -> HandoverCompletion {
    HandoverCompletion::new(
        open_handover(fixture),
        ActorLeaseId::new("lease-before-handover"),
        ActorLeaseId::new("lease-after-handover"),
        PersonInput::observed(2),
    )
    .unwrap_or_else(|_| unreachable!("both probe lease identities are non-empty"))
}

/// A job identity no action ever dispatched, so the grid pins the
/// outcome-match refusal exactly as the probe action outcome does.
fn probe_tool_job(action_id: ActionId) -> Command {
    Command::RecordToolJobOutcome {
        action_id,
        job_id: task_engine::ToolJobId::new("job-probe"),
        outcome: Box::new(task_engine::ToolJobOutcome {
            status: task_engine::ToolJobStatus::Succeeded,
            output_digest: None,
            output_bytes: 0,
            output_chunks: 0,
        }),
    }
}

/// A probe identity rather than the one a task would derive.
///
/// Every model-turn cell of the table is therefore exercised through the guard
/// that refuses an invented call, which is the refusal the grid is there to
/// pin down. The loop's own tests drive the derived identity.
fn probe_call() -> task_engine::ModelCallId {
    task_engine::ModelCallId::new("model-call-probe")
}

fn result_command(kind: K) -> Command {
    match kind {
        K::ResultCandidateReady => Command::ResultCandidateReady,
        K::CompleteResultValidated => Command::CompleteResultValidated(complete_result()),
        K::PartialResultValidated => Command::PartialResultValidated(partial_result()),
        K::ResumeForCorrection => Command::ResumeForCorrection,
        K::FollowUp => Command::FollowUp,
        K::FailTask => Command::FailTask {
            reason: task_engine::task::FailureReason::SourcesUnavailable,
        },
        K::CorrectFact => Command::CorrectFact {
            fact_id: fact_id(1),
        },
        K::ExcludeSource => Command::ExcludeSource {
            source_id: source_id(1),
        },
        K::RequestArtifact => Command::RequestArtifact {
            artifact_id: ArtifactId::new("artifact_0"),
            format: task_engine::artifact::ArtifactKind::Markdown,
            workspace_revision: 1,
        },
        K::AcceptArtifact => Command::AcceptArtifact {
            artifact_id: ArtifactId::new("artifact_0"),
        },
        K::ExportArtifact => Command::ExportArtifact {
            artifact_id: ArtifactId::new("artifact_0"),
            format: task_engine::artifact::ArtifactKind::Markdown,
        },
        _ => unreachable!("only result and artifact commands reach this fixture helper"),
    }
}

fn model_command(kind: K) -> Command {
    match kind {
        K::RequestModelTurn => Command::RequestModelTurn {
            call_id: probe_call(),
        },
        K::RequestModelAttempt => Command::RequestModelAttempt {
            call_id: probe_call(),
            attempt_ordinal: 1,
            candidate_ordinal: 0,
            kind: task_engine::ModelAttemptKind::Retry,
        },
        K::RecordModelTurn => Command::RecordModelTurn {
            call_id: probe_call(),
            digest: Box::new(turn_digest()),
        },
        K::RecordModelTurnGap => Command::RecordModelTurnGap {
            call_id: probe_call(),
            gap: task_engine::TurnGap::Cancelled,
        },
        // A boundary no task can have reached: with no turn started, every
        // eviction is out of range, so the grid pins the guard's refusal the
        // way the probe call pins the model guards'.
        K::RecordContextEviction => Command::RecordContextEviction { through_turn: 0 },
        _ => unreachable!("only model commands reach this fixture helper"),
    }
}

/// A representative payload for `kind`, built against `fixture`.
pub fn command_for(kind: K, fixture: &Fixture) -> Command {
    let action_id = fixture
        .action_id
        .clone()
        .unwrap_or_else(|| ActionId::new("act_absent"));
    let step_id = fixture
        .reducer
        .plan()
        .and_then(|plan| plan.steps().first().map(|step| step.plan_step_id().clone()));
    match kind {
        K::CreateTask => Command::CreateTask,
        K::EditScope => Command::EditScope(SourceScope::new()),
        K::StartTask => Command::StartTask(preview()),
        K::AcceptInitialConsent => Command::AcceptInitialConsent(receipt()),
        K::RecordDiscoveryTab => Command::RecordDiscoveryTab {
            discovery_tab_id: bip_types::identity::TabId::new("discovery_tab"),
            browser_session_id: BrowserSessionId::new("browser_session_1")
                .unwrap_or_else(|_| unreachable!()),
        },
        K::ApproveAction => Command::ApproveAction {
            approval: receipt(),
            still_current: true,
            expires_at_monotonic_ms: 2_000,
            expires_at_utc_ms: 2_000,
            browser_session_id: BrowserSessionId::new("browser_session_1")
                .unwrap_or_else(|_| unreachable!()),
        },
        K::DenyAction => Command::DenyAction {
            approval: receipt(),
        },
        K::PauseTask => Command::PauseTask {
            cause: PauseCause::User,
        },
        K::TakeOver => Command::TakeOver,
        K::PauseSettled => Command::PauseSettled,
        K::ResumeTask => Command::ResumeTask,
        K::CancelTask => Command::CancelTask,
        K::CancelSettled => Command::CancelSettled,
        K::ExecutorStarted => Command::ExecutorStarted,
        K::SetPlan => Command::SetPlan(plan_draft()),
        K::AdvanceStep => Command::AdvanceStep {
            plan_step_id: step_id
                .unwrap_or_else(|| task_engine::ids::PlanStepId::new("step_absent")),
            to: StepState::Running,
        },
        K::RequestApproval => Command::RequestApproval { action_id },
        K::RequestUserInput => Command::RequestUserInput,
        K::SupplyUserInput => Command::SupplyUserInput,
        K::RequestPermission => Command::RequestPermission(permission_request()),
        K::RecordPermissionResult => Command::RecordPermissionResult(granted(fixture)),
        K::RequestHandover => Command::RequestHandover {
            handover_id: probe_handover(),
        },
        K::RequestFieldValues | K::SupplyFieldValues => field_value_command(kind),
        K::CompleteHandover => Command::CompleteHandover(resumed(fixture)),
        K::ExpireHandover => Command::ExpireHandover {
            handover_id: open_handover(fixture),
        },
        K::ProposeAction => Command::ProposeAction(Box::new(proposal("action_key_probe"))),
        K::RecordPolicyDecision => Command::RecordPolicyDecision {
            action_id,
            decision: Box::new(authorize()),
            dispatch_id: None,
        },
        K::DispatchAction => Command::DispatchAction {
            action_id,
            dispatch_id: DispatchId::new("dispatch_probe"),
        },
        K::RecordActionOutcome => Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(verified_outcome()),
        },
        K::RequestModelTurn
        | K::RequestModelAttempt
        | K::RecordModelTurn
        | K::RecordModelTurnGap
        | K::RecordContextEviction => model_command(kind),
        K::RecordToolJobOutcome => probe_tool_job(action_id.clone()),
        K::ResultCandidateReady
        | K::CompleteResultValidated
        | K::PartialResultValidated
        | K::ResumeForCorrection
        | K::FollowUp
        | K::FailTask
        | K::CorrectFact
        | K::ExcludeSource
        | K::RequestArtifact
        | K::AcceptArtifact
        | K::ExportArtifact => result_command(kind),
    }
}
