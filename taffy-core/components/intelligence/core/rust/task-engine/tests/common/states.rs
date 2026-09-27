// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use task_engine::command::PauseCause;
use task_engine::task::ConsentStage;
use task_engine::transition::Guard;

/// A task in `RUNNING` with a plan and one authorized action.
pub fn running() -> Fixture {
    let mut fixture = draft();
    fixture.must_apply(Command::StartTask(preview()));
    fixture.must_apply(Command::AcceptInitialConsent(receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture.must_apply(Command::SetPlan(plan_draft()));
    fixture.must_apply(Command::ProposeAction(Box::new(proposal("action_key_0"))));
    let action_id = fixture.reducer.actions().next().map_or_else(
        || unreachable!("the proposal minted an action"),
        |action| action.action_id().clone(),
    );
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(authorize()),
        dispatch_id: None,
    });
    fixture.action_id = Some(action_id);
    fixture
}

/// A running task whose budgets are the ones the user consented to.
///
/// The budgets come through the preview rather than through the seed, because
/// that is where they come from in the product: consent is what fixes them,
/// and `StartTask` replaces whatever the draft was carrying.
pub fn running_within(budgets: TaskBudgets) -> Fixture {
    let mut fixture = draft();
    let mut consented = preview();
    // Every consent preview states the exact source ceiling independently of
    // the budget under test. Replacing the preview wholesale would make a
    // model-request-only fixture malformed before it reaches the behavior it
    // is meant to exercise.
    consented.budgets = budgets.with(BudgetKind::MaxSources, 1);
    fixture.must_apply(Command::StartTask(consented));
    fixture.must_apply(Command::AcceptInitialConsent(receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

/// A task in `state`.
///
/// `in_task_consent` picks which of the two ways into `AWAITING_CONSENT` is
/// used; it is ignored for every other state.
/// The fixture one cell of the table needs, chosen from the guards it names.
///
/// `WAITING_USER` is one state reached three ways — a value the task asked
/// for, a native permission prompt, and a handover — and the two exact waits
/// are only satisfiable from their own way in. A cell guarded on one of them,
/// handed a fixture that got there through `RequestUserInput`, would refuse
/// for a reason that says nothing about the cell under test.
pub fn for_cell(state: TaskState, guards: &[Guard]) -> Fixture {
    if state == TaskState::WaitingUser && guards.contains(&Guard::PendingPermissionMatches) {
        let mut fixture = running();
        fixture.must_apply(Command::RequestPermission(permission_request()));
        return fixture;
    }
    if state == TaskState::WaitingUser && guards.contains(&Guard::FieldValueRequestMatches) {
        let mut fixture = running();
        fixture.must_apply(Command::RequestFieldValues {
            request_id: task_engine::field_values::field_value_request_id_for_call(0, 0),
            tab_id: bip_types::identity::TabId::new("tab_probe"),
            node_id: bip_types::identity::SemanticNodeId::new("node_probe"),
            companion_node_ids: task_engine::FieldNodeIds::none(),
        });
        return fixture;
    }
    if state == TaskState::WaitingUser && guards.contains(&Guard::PendingHandoverMatches) {
        let mut fixture = running();
        fixture.must_apply(Command::RequestHandover {
            handover_id: task_engine::handover::handover_id_for_call(0, 0),
        });
        return fixture;
    }
    in_state(state, guards.contains(&Guard::InTaskApprovalPending))
}

pub fn in_state(state: TaskState, in_task_consent: bool) -> Fixture {
    match state {
        TaskState::Draft => draft(),
        TaskState::AwaitingConsent if in_task_consent => {
            let mut fixture = running();
            let action_id = fixture
                .action_id
                .clone()
                .unwrap_or_else(|| unreachable!("the running fixture has an action"));
            fixture.must_apply(Command::RequestApproval { action_id });
            assert_eq!(
                fixture.reducer.task().consent_stage(),
                Some(ConsentStage::InTask)
            );
            fixture
        }
        TaskState::AwaitingConsent => {
            let mut fixture = draft();
            fixture.must_apply(Command::StartTask(preview()));
            fixture
        }
        TaskState::Queued => {
            // The queue is reached twice: once from consent, and once from a
            // resume. The resume path is used here so a queued task carries the
            // actions a rebuild would find.
            let mut fixture = running();
            fixture.must_apply(Command::PauseTask {
                cause: PauseCause::User,
            });
            fixture.must_apply(Command::PauseSettled);
            fixture.must_apply(Command::ResumeTask);
            fixture
        }
        TaskState::Running => running(),
        TaskState::WaitingUser => {
            let mut fixture = running();
            fixture.must_apply(Command::RequestUserInput);
            fixture
        }
        TaskState::Pausing => {
            let mut fixture = running();
            fixture.must_apply(Command::PauseTask {
                cause: PauseCause::User,
            });
            fixture
        }
        TaskState::Paused => {
            let mut fixture = in_state(TaskState::Pausing, false);
            fixture.must_apply(Command::PauseSettled);
            fixture
        }
        TaskState::Cancelling => {
            let mut fixture = running();
            fixture.must_apply(Command::CancelTask);
            fixture
        }
        TaskState::Cancelled => {
            let mut fixture = in_state(TaskState::Cancelling, false);
            fixture.must_apply(Command::CancelSettled);
            fixture
        }
        TaskState::Completing => {
            let mut fixture = running();
            fixture.must_apply(Command::RequestArtifact {
                artifact_id: fixture.artifact_id.clone(),
                format: task_engine::ArtifactKind::Markdown,
                workspace_revision: 3,
            });
            fixture.must_apply(Command::ResultCandidateReady);
            fixture
        }
        TaskState::Completed => {
            let mut fixture = in_state(TaskState::Completing, false);
            fixture.must_apply(Command::CompleteResultValidated(complete_result()));
            fixture.must_apply(Command::AcceptArtifact {
                artifact_id: ArtifactId::new("artifact_0"),
            });
            fixture
        }
        TaskState::Partial => {
            let mut fixture = in_state(TaskState::Completing, false);
            fixture.must_apply(Command::PartialResultValidated(partial_result()));
            fixture.must_apply(Command::AcceptArtifact {
                artifact_id: ArtifactId::new("artifact_0"),
            });
            fixture
        }
        TaskState::Failed => {
            let mut fixture = running();
            fixture.must_apply(Command::FailTask {
                reason: task_engine::task::FailureReason::SourcesUnavailable,
            });
            fixture
        }
    }
}
