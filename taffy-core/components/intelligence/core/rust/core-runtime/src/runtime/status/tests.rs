// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod account;
mod recency_order;
mod running_notices;
mod task_end;

use core_api_types::{AuthPhase, AuthProvider, TaskPhase, TaskTemplateId as CoreTaskTemplateId};
use task_engine::{
    ActionClass, FailureReason, PlanStatus, PlatformPermission, TaskState, TaskTemplateId,
};

use crate::account::{AccountAuthMethod, AccountSession, AccountSubjectId, SessionHandle};
use crate::contract::Deadline;
use crate::ports::{PendingActionFacts, PendingPermissionFacts, PlanProgressFacts, TaskViewFacts};
use loop_kernel::ports::{AcceptedTaskConsentFacts, CommittedActionApprovalFacts};

use super::browser_bindings::project_task_browser_bindings;
use super::task_projection::{action_summary_message, project_task_view, project_template_id};
use crate::adapters::account::ProductionAccount;
use crate::ports::AccountPort;

use super::project_account_view;

fn task_facts(state: TaskState) -> TaskViewFacts {
    TaskViewFacts {
        task_id: "task-1".to_owned(),
        revision: 7,
        state,
        execution_phase: None,
        goal: "Compare the selected evidence".to_owned(),
        template_id: TaskTemplateId::CompareProducts,
        workspace_id: None,
        plan_progress: None,
        terminal_failure: (state == TaskState::Failed).then_some(FailureReason::SourcesUnavailable),
        pause_cause: None,
        model_attempt: None,
        last_move_refused: false,
        reply_being_reasked: false,
        pending_action: None,
        pending_permission: None,
        accepted_consent: None,
        committed_action_approvals: Vec::new(),
        outcome_unknown_actions: 0,
        artifacts: Vec::new(),
        activity: Vec::new(),
        allowed_controls: Vec::new(),
        pending_handover: None,
        pending_field_values: None,
    }
}

#[test]
fn browser_binding_preserves_the_reducers_exact_control_order() {
    let mut facts = task_facts(TaskState::Running);
    facts.allowed_controls = vec![
        task_engine::TaskControlKind::Pause,
        task_engine::TaskControlKind::TakeOver,
        task_engine::TaskControlKind::Stop,
    ];

    let binding = project_task_browser_bindings(&facts);

    assert_eq!(binding.allowed_controls, facts.allowed_controls);
}

#[test]
fn settling_paused_and_terminal_bindings_withhold_durable_browser_authority() {
    for state in [
        TaskState::Pausing,
        TaskState::Paused,
        TaskState::Cancelling,
        TaskState::Cancelled,
        TaskState::Completed,
        TaskState::Partial,
        TaskState::Failed,
    ] {
        let mut facts = task_facts(state);
        facts.accepted_consent = Some(AcceptedTaskConsentFacts {
            accepted_revision: 2,
            browser_session_id: "browser-session-1".to_owned(),
            receipt_id: "consent-1".to_owned(),
            sources: Vec::new(),
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route_id: Some("no_model_required".to_owned()),
        });
        facts.committed_action_approvals = vec![CommittedActionApprovalFacts {
            action_id: "action-1".to_owned(),
            committed_revision: 6,
            receipt_id: "approval-1".to_owned(),
            proposal_digest: "ab".repeat(32),
            expires_at_monotonic_ms: 1_000,
            expires_at_utc_ms: 2_000,
            browser_session_id: "browser-session-1".to_owned(),
        }];

        let binding = project_task_browser_bindings(&facts);

        assert!(binding.accepted_consent.is_none(), "{}", state.label());
        assert!(binding.committed_approvals.is_empty(), "{}", state.label());
    }
}

#[test]
fn active_binding_keeps_durable_browser_authority() {
    let mut facts = task_facts(TaskState::Running);
    facts.accepted_consent = Some(AcceptedTaskConsentFacts {
        accepted_revision: 2,
        browser_session_id: "browser-session-1".to_owned(),
        receipt_id: "consent-1".to_owned(),
        sources: Vec::new(),
        source_discovery_enabled: false,
        new_source_cap: 0,
        provider_route_id: Some("no_model_required".to_owned()),
    });
    facts.committed_action_approvals = vec![CommittedActionApprovalFacts {
        action_id: "action-1".to_owned(),
        committed_revision: 6,
        receipt_id: "approval-1".to_owned(),
        proposal_digest: "ab".repeat(32),
        expires_at_monotonic_ms: 1_000,
        expires_at_utc_ms: 2_000,
        browser_session_id: "browser-session-1".to_owned(),
    }];

    let binding = project_task_browser_bindings(&facts);

    assert!(binding.accepted_consent.is_some());
    assert_eq!(binding.committed_approvals.len(), 1);
}

#[test]
fn empty_account_protocol_projects_signed_out_without_defaults() {
    let view = project_account_view(&ProductionAccount::new());
    assert_eq!(view.phase, AuthPhase::SignedOut);
    assert!(view.account.is_none());
}

#[test]
fn durable_account_method_projects_signed_in_without_profile_fabrication() {
    let mut protocol = ProductionAccount::new();
    protocol.restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    }));

    let view = project_account_view(&protocol);
    assert_eq!(view.phase, AuthPhase::SignedIn);
    let account = view.account.unwrap_or_else(|| unreachable!());
    assert_eq!(account.account_id, "subject-1");
    assert_eq!(account.method, AuthProvider::Github);
    assert!(account.display_name.is_none());
    assert!(account.email.is_none());
}

#[test]
fn every_durable_task_state_has_one_closed_ui_phase() {
    let expected = [
        TaskPhase::Idle,
        TaskPhase::WaitingForUser,
        TaskPhase::Planning,
        TaskPhase::Running,
        TaskPhase::WaitingForUser,
        TaskPhase::Paused,
        TaskPhase::Paused,
        TaskPhase::Cancelled,
        TaskPhase::Running,
        TaskPhase::Cancelled,
        TaskPhase::Completed,
        TaskPhase::Partial,
        TaskPhase::Failed,
    ];
    assert_eq!(TaskState::ALL.len(), expected.len());
    for (state, phase) in TaskState::ALL.iter().zip(expected) {
        let projected = project_task_view(task_facts(*state)).unwrap_or_else(|_| unreachable!());
        assert_eq!(projected.phase, phase, "{}", state.label());
        assert_eq!(projected.template_id, CoreTaskTemplateId::CompareProducts);
    }
}

#[test]
fn task_view_preserves_the_reducers_exact_control_order() {
    let mut facts = task_facts(TaskState::Running);
    facts.allowed_controls = vec![
        task_engine::TaskControlKind::Pause,
        task_engine::TaskControlKind::TakeOver,
        task_engine::TaskControlKind::Stop,
    ];

    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());

    assert_eq!(
        projected.allowed_controls,
        [
            core_api_types::TaskControlKind::Pause,
            core_api_types::TaskControlKind::TakeOver,
            core_api_types::TaskControlKind::Stop,
        ]
    );
}

#[test]
fn every_reducer_template_maps_to_the_same_closed_core_api_member() {
    let expected = [
        CoreTaskTemplateId::CompareProducts,
        CoreTaskTemplateId::SummarizeEvidence,
        CoreTaskTemplateId::BuildSourceTable,
        CoreTaskTemplateId::WebErrand,
    ];
    assert_eq!(TaskTemplateId::ALL.len(), expected.len());
    for (template, projected) in TaskTemplateId::ALL.iter().zip(expected) {
        assert_eq!(project_template_id(*template), projected);
    }
}

#[test]
fn plan_progress_counts_every_final_step_state_without_guessing() {
    let mut facts = task_facts(TaskState::Running);
    facts.plan_progress = Some(PlanProgressFacts {
        status: PlanStatus::Active,
        total_steps: 4,
        finished_steps: 3,
    });
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(projected.progress_basis_points, 7_500);
}

#[test]
fn every_durable_plan_status_has_a_deterministic_task_detail() {
    let expected = [
        "task.planning",
        "task.running",
        "task.planning",
        "task.completing",
        "task.plan_abandoned",
    ];
    assert_eq!(PlanStatus::ALL.len(), expected.len());
    for (status, message_key) in PlanStatus::ALL.iter().zip(expected) {
        let mut facts = task_facts(TaskState::Running);
        facts.plan_progress = Some(PlanProgressFacts {
            status: *status,
            total_steps: 2,
            finished_steps: 1,
        });
        let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
        assert_eq!(projected.status_message_key.as_deref(), Some(message_key));
    }
}

#[test]
fn every_action_class_has_a_bounded_content_free_summary_key() {
    let actions = [
        ActionClass::ObservePage,
        ActionClass::ScrollIntoView,
        ActionClass::OpenLink,
        ActionClass::CreateTaskTab,
        ActionClass::SyntheticClick,
        ActionClass::MoveFocus,
        ActionClass::FillField,
        ActionClass::SelectOption,
        ActionClass::ToggleControl,
        ActionClass::SubmitForm,
        ActionClass::StartDownload,
        ActionClass::UploadFile,
        ActionClass::SendMessage,
        ActionClass::Purchase,
        ActionClass::ExtractCredential,
        ActionClass::BypassAccessControl,
    ];
    for action in actions {
        let key = action_summary_message(action);
        assert!(key.starts_with("action."));
        assert!(key.len() <= core_api_types::MAX_MESSAGE_KEY_BYTES);
    }
}

#[test]
fn pending_action_projection_contains_no_authority_or_fabricated_host() {
    let mut facts = task_facts(TaskState::AwaitingConsent);
    facts.pending_action = Some(PendingActionFacts {
        action_id: "action-1".to_owned(),
        action_class: ActionClass::StartDownload,
        item_count: 1,
        proposal_digest: "ab".repeat(32),
    });
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    let action = projected.pending_action.unwrap_or_else(|| unreachable!());
    assert_eq!(action.action_id, "action-1");
    assert!(action.host.is_none());
    assert_eq!(action.summary_message_key, "action.start_download");
}

#[test]
fn form_actions_never_reach_the_value_blind_generic_approval_surface() {
    for action_class in [
        ActionClass::FillField,
        ActionClass::SelectOption,
        ActionClass::ToggleControl,
        ActionClass::SubmitForm,
    ] {
        let mut facts = task_facts(TaskState::AwaitingConsent);
        facts.pending_action = Some(PendingActionFacts {
            action_id: "action-1".to_owned(),
            action_class,
            item_count: 1,
            proposal_digest: "ab".repeat(32),
        });
        let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
        assert!(
            projected.pending_action.is_none(),
            "{}",
            action_class.label()
        );
    }
}

#[test]
fn waiting_on_the_person_names_a_handover_and_not_an_approval() {
    let mut facts = task_facts(TaskState::WaitingUser);
    facts.pending_handover = Some("turn-1-handover-1".to_owned());
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.status_message_key.as_deref(),
        Some("task.waiting_for_handover")
    );
}

#[test]
fn waiting_without_a_handover_keeps_the_generic_input_line() {
    let projected =
        project_task_view(task_facts(TaskState::WaitingUser)).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.status_message_key.as_deref(),
        Some("task.waiting_for_input")
    );
}

#[test]
fn waiting_on_a_permission_does_not_share_the_ask_line() {
    let mut facts = task_facts(TaskState::WaitingUser);
    facts.pending_permission = Some(PendingPermissionFacts {
        request_id: "request-1".to_owned(),
        permission: PlatformPermission::Camera,
        deadline_monotonic_ms: 1,
        deadline_utc_ms: 1,
        browser_session_id: "session-1".to_owned(),
    });
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.status_message_key.as_deref(),
        Some("task.waiting_for_permission")
    );
}
