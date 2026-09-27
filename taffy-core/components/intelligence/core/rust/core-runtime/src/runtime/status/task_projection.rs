// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exhaustive projection of canonical reducer facts into Core API task state.

use core_api_types::{
    ActionApprovalView, CoreFailure, CoreFailureCode, TaskActivityKind as CoreTaskActivityKind,
    TaskActivityView, TaskArtifactKind as CoreTaskArtifactKind, TaskArtifactView,
    TaskControlKind as CoreTaskControlKind, TaskPhase, TaskTemplateId as CoreTaskTemplateId,
    TaskViewState, MAX_PROGRESS_BASIS_POINTS, MAX_TASK_ACTIVITY, MAX_TASK_ARTIFACTS,
    MAX_TASK_GOAL_BYTES,
};
use task_engine::{
    ActionClass, ExecutionPhase, FailureReason, ModelAttemptKind, PauseCause, PlanStatus, TaskState,
};

use crate::ports::TaskViewFacts;

use super::CoreStatusProjectionGap;

pub(super) fn project_task_view(
    facts: TaskViewFacts,
) -> Result<TaskViewState, CoreStatusProjectionGap> {
    if facts.goal.len() > MAX_TASK_GOAL_BYTES {
        return Err(CoreStatusProjectionGap::GoalTooLarge {
            task_id: facts.task_id,
        });
    }
    if facts.artifacts.len() > MAX_TASK_ARTIFACTS {
        return Err(CoreStatusProjectionGap::TooManyTaskArtifacts {
            task_id: facts.task_id,
        });
    }
    if facts.activity.len() > MAX_TASK_ACTIVITY {
        return Err(CoreStatusProjectionGap::TooManyActivitySteps {
            task_id: facts.task_id,
        });
    }
    let progress_basis_points = project_progress(&facts)?;
    let phase = project_phase(&facts);
    let outcome_unknown = has_outcome_unknown(&facts);
    let failure = facts.terminal_failure.map(project_failure).or_else(|| {
        outcome_unknown.then(|| CoreFailure {
            code: CoreFailureCode::OutcomeUnknown,
            retryable: false,
            message_key: Some("task.failure.outcome_unknown".to_owned()),
        })
    });
    let status_message_key = Some(project_status_message(&facts).to_owned());
    // Form values are confirmed on the browser-owned sheet before they enter
    // the vault. Projecting the same proposal onto the generic approval API
    // would create a second, value-blind way to approve it. Browser bindings
    // still carry the pending identity so the browser can bind its private
    // preapproval to the reducer transition.
    let pending_action = facts
        .pending_action
        .filter(|action| {
            !matches!(
                action.action_class,
                ActionClass::FillField
                    | ActionClass::SelectOption
                    | ActionClass::ToggleControl
                    | ActionClass::SubmitForm
            )
        })
        .map(|action| ActionApprovalView {
            action_id: action.action_id,
            host: None,
            item_count: action.item_count,
            summary_message_key: action_summary_message(action.action_class).to_owned(),
        });
    Ok(TaskViewState {
        task_id: facts.task_id,
        revision: facts.revision,
        phase,
        progress_basis_points,
        status_message_key,
        failure,
        goal: facts.goal,
        template_id: project_template_id(facts.template_id),
        workspace_id: facts.workspace_id,
        pending_action,
        pending_ask_prompt: None,
        pending_field_value_request: facts.pending_field_values,
        allowed_controls: facts
            .allowed_controls
            .into_iter()
            .map(project_control)
            .collect(),
        artifacts: facts
            .artifacts
            .into_iter()
            .map(|artifact| TaskArtifactView {
                artifact_id: artifact.artifact_id,
                kind: project_artifact_kind(artifact.kind),
                workspace_revision: artifact.workspace_revision,
                accepted: artifact.accepted,
            })
            .collect(),
        activity: facts
            .activity
            .into_iter()
            .map(|step| TaskActivityView {
                sequence: step.sequence,
                kind: project_activity_kind(step.kind),
                host: step.host,
                count: step.count,
                at_epoch_ms: step.at_epoch_ms,
            })
            .collect(),
    })
}

/// The nine steps, one for one (decision 0148).
///
/// Exhaustive, like every other projection here: a kind added to the reducer
/// is a kind the contract has to learn, and a compile error is the only way
/// that stays true.
const fn project_activity_kind(kind: task_engine::TaskActivityKind) -> CoreTaskActivityKind {
    match kind {
        task_engine::TaskActivityKind::OpenedPage => CoreTaskActivityKind::OpenedPage,
        task_engine::TaskActivityKind::ReadPage => CoreTaskActivityKind::ReadPage,
        task_engine::TaskActivityKind::PageUnavailable => CoreTaskActivityKind::PageUnavailable,
        task_engine::TaskActivityKind::MoveRefused => CoreTaskActivityKind::MoveRefused,
        task_engine::TaskActivityKind::AskedYou => CoreTaskActivityKind::AskedYou,
        task_engine::TaskActivityKind::YouAnswered => CoreTaskActivityKind::YouAnswered,
        task_engine::TaskActivityKind::HandedBack => CoreTaskActivityKind::HandedBack,
        task_engine::TaskActivityKind::YouTookOver => CoreTaskActivityKind::YouTookOver,
        task_engine::TaskActivityKind::BuiltOutput => CoreTaskActivityKind::BuiltOutput,
    }
}

const fn project_artifact_kind(kind: task_engine::ArtifactKind) -> CoreTaskArtifactKind {
    match kind {
        task_engine::ArtifactKind::Markdown => CoreTaskArtifactKind::Markdown,
        task_engine::ArtifactKind::Csv => CoreTaskArtifactKind::Csv,
        task_engine::ArtifactKind::Xlsx => CoreTaskArtifactKind::Xlsx,
        task_engine::ArtifactKind::Pdf => CoreTaskArtifactKind::Pdf,
        task_engine::ArtifactKind::Docx => CoreTaskArtifactKind::Docx,
        task_engine::ArtifactKind::Pptx => CoreTaskArtifactKind::Pptx,
        task_engine::ArtifactKind::WaveAudio => CoreTaskArtifactKind::WaveAudio,
        task_engine::ArtifactKind::FrameArchive => CoreTaskArtifactKind::FrameArchive,
    }
}

const fn project_control(control: task_engine::TaskControlKind) -> CoreTaskControlKind {
    match control {
        task_engine::TaskControlKind::Pause => CoreTaskControlKind::Pause,
        task_engine::TaskControlKind::Resume => CoreTaskControlKind::Resume,
        task_engine::TaskControlKind::TakeOver => CoreTaskControlKind::TakeOver,
        task_engine::TaskControlKind::Stop => CoreTaskControlKind::Stop,
    }
}

pub(super) const fn project_template_id(
    template_id: task_engine::TaskTemplateId,
) -> CoreTaskTemplateId {
    match template_id {
        task_engine::TaskTemplateId::CompareProducts => CoreTaskTemplateId::CompareProducts,
        task_engine::TaskTemplateId::SummarizeEvidence => CoreTaskTemplateId::SummarizeEvidence,
        task_engine::TaskTemplateId::BuildSourceTable => CoreTaskTemplateId::BuildSourceTable,
        task_engine::TaskTemplateId::WebErrand => CoreTaskTemplateId::WebErrand,
    }
}

fn project_progress(facts: &TaskViewFacts) -> Result<u32, CoreStatusProjectionGap> {
    let Some(progress) = facts.plan_progress else {
        return match facts.state {
            TaskState::Completed | TaskState::Partial => u32::try_from(MAX_PROGRESS_BASIS_POINTS)
                .map_err(|_| CoreStatusProjectionGap::InvalidProgress {
                    task_id: facts.task_id.clone(),
                }),
            TaskState::Draft
            | TaskState::AwaitingConsent
            | TaskState::Queued
            | TaskState::Running
            | TaskState::WaitingUser
            | TaskState::Pausing
            | TaskState::Paused
            | TaskState::Cancelling
            | TaskState::Completing
            | TaskState::Cancelled
            | TaskState::Failed => Ok(0),
        };
    };
    if progress.total_steps == 0 || progress.finished_steps > progress.total_steps {
        return Err(CoreStatusProjectionGap::InvalidProgress {
            task_id: facts.task_id.clone(),
        });
    }
    let invalid_progress = || CoreStatusProjectionGap::InvalidProgress {
        task_id: facts.task_id.clone(),
    };
    let ceiling = u64::try_from(MAX_PROGRESS_BASIS_POINTS).map_err(|_| invalid_progress())?;
    let scaled = u64::from(progress.finished_steps)
        .checked_mul(ceiling)
        .ok_or_else(invalid_progress)?
        / u64::from(progress.total_steps);
    u32::try_from(scaled).map_err(|_| CoreStatusProjectionGap::InvalidProgress {
        task_id: facts.task_id.clone(),
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PlanProjection {
    Planning,
    Executing,
    Completing,
    Abandoned,
}

const fn project_plan_status(status: PlanStatus) -> PlanProjection {
    match status {
        PlanStatus::Draft | PlanStatus::Superseded => PlanProjection::Planning,
        PlanStatus::Active => PlanProjection::Executing,
        PlanStatus::Completed => PlanProjection::Completing,
        PlanStatus::Abandoned => PlanProjection::Abandoned,
    }
}

const fn project_phase(facts: &TaskViewFacts) -> TaskPhase {
    if matches!(facts.state, TaskState::Pausing | TaskState::Paused) {
        return TaskPhase::Paused;
    }
    if has_outcome_unknown(facts) {
        return TaskPhase::OutcomeUnknown;
    }
    match facts.state {
        TaskState::Draft => TaskPhase::Idle,
        TaskState::AwaitingConsent | TaskState::WaitingUser => TaskPhase::WaitingForUser,
        TaskState::Queued => TaskPhase::Planning,
        TaskState::Running => {
            let plan_is_being_built = matches!(
                facts.plan_progress,
                Some(progress)
                    if matches!(project_plan_status(progress.status), PlanProjection::Planning)
            );
            if plan_is_being_built
                || matches!(facts.execution_phase, Some(ExecutionPhase::Planning))
            {
                TaskPhase::Planning
            } else {
                TaskPhase::Running
            }
        }
        TaskState::Cancelling | TaskState::Cancelled => TaskPhase::Cancelled,
        TaskState::Completing => TaskPhase::Running,
        TaskState::Completed => TaskPhase::Completed,
        TaskState::Partial => TaskPhase::Partial,
        TaskState::Failed => TaskPhase::Failed,
        TaskState::Pausing | TaskState::Paused => TaskPhase::Paused,
    }
}

const fn has_outcome_unknown(facts: &TaskViewFacts) -> bool {
    facts.outcome_unknown_actions > 0
        && !matches!(
            facts.state,
            TaskState::Completed | TaskState::Partial | TaskState::Cancelled
        )
}

pub(super) fn project_failure(reason: FailureReason) -> CoreFailure {
    let (code, retryable, message_key) = match reason {
        FailureReason::BudgetExhausted => (
            CoreFailureCode::BudgetExceeded,
            false,
            "task.failure.budget_exceeded",
        ),
        FailureReason::ProviderUnavailable => (
            CoreFailureCode::ProviderUnavailable,
            true,
            "task.failure.provider_unavailable",
        ),
        FailureReason::SourcesUnavailable => (
            CoreFailureCode::SourcesUnavailable,
            true,
            "task.failure.sources_unavailable",
        ),
        FailureReason::UnverifiableAction => (
            CoreFailureCode::UnverifiableAction,
            false,
            "task.failure.unverifiable_action",
        ),
        FailureReason::JournalUnusable => (
            CoreFailureCode::JournalUnusable,
            false,
            "task.failure.journal_unusable",
        ),
        FailureReason::DeadlineExceeded => (
            CoreFailureCode::DeadlineExceeded,
            false,
            "task.failure.deadline_exceeded",
        ),
        FailureReason::ProviderRefused => (
            CoreFailureCode::ProviderRefused,
            false,
            "task.failure.provider_refused",
        ),
        FailureReason::ProviderLimit => (
            CoreFailureCode::ProviderLimit,
            true,
            "task.failure.provider_limit",
        ),
        FailureReason::Offline => (CoreFailureCode::Offline, true, "task.failure.offline"),
        FailureReason::PolicyRefused => (
            CoreFailureCode::PolicyRefused,
            false,
            "task.failure.policy_refused",
        ),
    };
    CoreFailure {
        code,
        retryable,
        message_key: Some(message_key.to_owned()),
    }
}

const fn project_status_message(facts: &TaskViewFacts) -> &'static str {
    if facts.outcome_unknown_actions > 0
        && !matches!(
            facts.state,
            TaskState::Completed | TaskState::Partial | TaskState::Cancelled
        )
    {
        return "task.outcome_unknown";
    }
    match facts.state {
        TaskState::Draft => "task.draft",
        TaskState::AwaitingConsent if facts.pending_action.is_some() => {
            "task.awaiting_action_approval"
        }
        TaskState::AwaitingConsent => "task.awaiting_initial_consent",
        TaskState::Queued => "task.queued",
        TaskState::Running => match facts.plan_progress {
            Some(progress) => match project_plan_status(progress.status) {
                PlanProjection::Planning => "task.planning",
                PlanProjection::Completing => "task.completing",
                PlanProjection::Abandoned => "task.plan_abandoned",
                PlanProjection::Executing => project_execution_message(facts),
            },
            None => project_execution_message(facts),
        },
        TaskState::WaitingUser if facts.pending_handover.is_some() => "task.waiting_for_handover",
        TaskState::WaitingUser if facts.pending_permission.is_some() => {
            "task.waiting_for_permission"
        }
        // Ordered after the other two on purpose. A handover and a permission
        // are waits the person cannot answer from a sheet, so if a reducer ever
        // held one of those open beside a value request, saying "fill this in"
        // would point at the wrong door.
        TaskState::WaitingUser if facts.pending_field_values.is_some() => {
            "task.waiting_for_field_values"
        }
        TaskState::WaitingUser => "task.waiting_for_input",
        TaskState::Pausing => "task.pausing",
        // A pause the provider caused says so, so the person knows what a
        // resume would try again; a pause the person asked for keeps the
        // plain word.
        TaskState::Paused => match facts.pause_cause {
            Some(PauseCause::ProviderLimit) => "task.paused_provider_limit",
            // Not the limit key: a 5xx is the provider's capacity and says
            // nothing about the person's allowance, so naming it a limit sends
            // them to check a plan that is fine (decision 0219).
            Some(PauseCause::ProviderBusy) => "task.paused_provider_busy",
            Some(PauseCause::Offline) => "task.paused_offline",
            // Not "offline": the device may have a perfectly good network and
            // the answer may have been billed. The sentence says only what is
            // known, which is that nothing came back (decision 0217).
            Some(PauseCause::NoAnswer) => "task.paused_no_answer",
            Some(PauseCause::User | PauseCause::BackgroundRestricted) | None => "task.paused",
        },
        TaskState::Cancelling => "task.stopping",
        TaskState::Completing => "task.completing",
        TaskState::Cancelled => "task.stopped",
        TaskState::Completed => "task.completed",
        TaskState::Partial => "task.partly_done",
        TaskState::Failed => "task.failed",
    }
}

/// The line for a running task: a notice when there is one, else the phase.
///
/// A notice comes first because each names something the phase word would
/// hide behind "thinking": a paid retry of the same model, a switch to the
/// next one, a reply being asked for again because the last could not be
/// used, or a move the browser or policy refused. The refused move is said
/// only while the task is deciding what to do instead; once it is reading or
/// acting again the phase speaks, because the task has moved on.
const fn project_execution_message(facts: &TaskViewFacts) -> &'static str {
    if let Some(kind) = facts.model_attempt {
        return match kind {
            ModelAttemptKind::Retry => "task.retrying_provider",
            ModelAttemptKind::Failover => "task.switching_model",
        };
    }
    match facts.execution_phase {
        Some(ExecutionPhase::Inferencing) if facts.reply_being_reasked => "task.asking_again",
        Some(ExecutionPhase::Planning | ExecutionPhase::Inferencing) | None
            if facts.last_move_refused =>
        {
            "task.blocked_move"
        }
        Some(ExecutionPhase::Planning) => "task.planning",
        Some(ExecutionPhase::Observing) => "task.observing",
        Some(ExecutionPhase::Inferencing) => "task.thinking",
        Some(ExecutionPhase::Acting) => "task.acting",
        Some(ExecutionPhase::Verifying) => "task.verifying",
        None => "task.running",
    }
}

pub(super) const fn action_summary_message(action: ActionClass) -> &'static str {
    match action {
        ActionClass::ObservePage => "action.observe_page",
        ActionClass::ScrollIntoView => "action.scroll_into_view",
        ActionClass::OpenLink => "action.open_link",
        ActionClass::CreateTaskTab => "action.create_task_tab",
        ActionClass::SyntheticClick => "action.synthetic_click",
        ActionClass::MoveFocus => "action.move_focus",
        ActionClass::FillField => "action.fill_field",
        ActionClass::SelectOption => "action.select_option",
        ActionClass::ToggleControl => "action.toggle_control",
        ActionClass::SubmitForm => "action.submit_form",
        ActionClass::StartDownload => "action.start_download",
        ActionClass::UploadFile => "action.upload_file",
        ActionClass::SendMessage => "action.send_message",
        ActionClass::Purchase => "action.purchase",
        ActionClass::ExtractCredential => "action.extract_credential",
        ActionClass::BypassAccessControl => "action.bypass_access_control",
        ActionClass::ExecuteToolJob => "action.execute_tool_job",
        ActionClass::LibraryRead => "action.library_read",
        ActionClass::LibraryWrite => "action.library_write",
        ActionClass::MemoryRead => "action.memory_read",
        ActionClass::MemoryWrite => "action.memory_write",
        ActionClass::ControlTab => "action.control_tab",
        ActionClass::ProfileStoreRead => "action.profile_store_read",
    }
}
