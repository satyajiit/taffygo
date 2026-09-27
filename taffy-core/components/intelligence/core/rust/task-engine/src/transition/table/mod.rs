// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The table: every durable state against every command.
//!
//! # The match here is the totality proof
//!
//! [`disposition`] is one exhaustive match over [`CommandKind`], and it is the
//! only thing in this module that knows every command exists. A command added
//! without a cell is a compile error here rather than a pair with no answer.
//! Every arm is one call, so this function is the *index* of the grid and
//! nothing else — the cell itself lives in the family module the arm names.
//!
//! # Why two family modules and not one file
//!
//! [`control`] holds the commands that decide whether a task runs at all:
//! consent, holding, resuming, stopping, settling, waiting for a person, and
//! handing the page to one.
//! [`work`] holds the commands a running task issues: plans, proposals, model
//! turns, results and artifacts. That is the seam section 9.2's diagram
//! already draws, and it is the one split that leaves each half readable
//! without the other.
//!
//! The routing costs nothing in safety. A cell function answers for exactly
//! the commands its arm names, so there is no arm anywhere that could hand a
//! command the wrong family's answer — which is the failure a table split into
//! two independently exhaustive matches would have made possible.

mod control;
mod work;

use super::Disposition;
use crate::command::CommandKind;
use crate::task::TaskState;

/// What the reducer does with `command` when the task is in `state`.
///
/// Total over both enumerations. There is no pair without an answer, and the
/// answer is the same one the reducer acts on.
pub const fn disposition(state: TaskState, command: CommandKind) -> Disposition {
    match command {
        CommandKind::CreateTask => control::create_task(state),
        CommandKind::EditScope => control::edit_scope(state),
        CommandKind::StartTask => control::start_task(state),
        CommandKind::AcceptInitialConsent => control::accept_initial_consent(state),
        CommandKind::RecordDiscoveryTab => control::record_discovery_tab(state),
        CommandKind::ApproveAction => control::approve_action(state),
        CommandKind::DenyAction => control::deny_action(state),
        CommandKind::PauseTask => control::pause_task(state),
        CommandKind::TakeOver => control::take_over(state),
        CommandKind::PauseSettled => control::pause_settled(state),
        CommandKind::ResumeTask => control::resume_task(state),
        CommandKind::CancelTask => control::cancel_task(state),
        CommandKind::CancelSettled => control::cancel_settled(state),
        CommandKind::ExecutorStarted => control::executor_started(state),
        CommandKind::SetPlan => work::set_plan(state),
        CommandKind::AdvanceStep => work::advance_step(state),
        CommandKind::RequestApproval => control::request_approval(state),
        CommandKind::RequestUserInput => control::request_user_input(state),
        CommandKind::SupplyUserInput => control::supply_user_input(state),
        CommandKind::RequestPermission => control::request_permission(state),
        CommandKind::RecordPermissionResult => control::record_permission_result(state),
        CommandKind::RequestHandover => control::request_handover(state),
        CommandKind::RequestFieldValues => control::request_field_values(state),
        CommandKind::SupplyFieldValues => control::supply_field_values(state),
        CommandKind::CompleteHandover => control::complete_handover(state),
        CommandKind::ExpireHandover => control::expire_handover(state),
        CommandKind::ProposeAction => work::propose_action(state),
        CommandKind::RecordPolicyDecision => work::record_policy_decision(state),
        CommandKind::DispatchAction => work::dispatch_action(state),
        CommandKind::RecordActionOutcome => work::record_action_outcome(state),
        CommandKind::RecordToolJobOutcome => work::record_tool_job_outcome(state),
        CommandKind::RequestModelTurn => work::request_model_turn(state),
        CommandKind::RequestModelAttempt => work::request_model_attempt(state),
        // One cell for both. A reply and the fact that there will not be one
        // are the same question of the task: is a call still outstanding, and
        // is this the call it is holding.
        CommandKind::RecordModelTurn | CommandKind::RecordModelTurnGap => {
            work::record_model_turn(state)
        }
        CommandKind::RecordContextEviction => work::record_context_eviction(state),
        CommandKind::ResultCandidateReady => work::result_candidate_ready(state),
        CommandKind::CompleteResultValidated => work::complete_result_validated(state),
        CommandKind::PartialResultValidated => work::partial_result_validated(state),
        CommandKind::ResumeForCorrection => work::resume_for_correction(state),
        CommandKind::FollowUp => control::follow_up(state),
        CommandKind::FailTask => work::fail_task(state),
        CommandKind::CorrectFact => work::correct_fact(state),
        CommandKind::ExcludeSource => work::exclude_source(state),
        CommandKind::RequestArtifact => work::request_artifact(state),
        CommandKind::AcceptArtifact => work::accept_artifact(state),
        CommandKind::ExportArtifact => work::export_artifact(state),
    }
}
