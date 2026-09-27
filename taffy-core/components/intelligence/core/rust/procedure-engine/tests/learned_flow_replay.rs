// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Full recorded sequence through normal reducer proposals and person handback.

#![allow(clippy::expect_used, clippy::panic)]

#[path = "common/reviewed_task.rs"]
mod reviewed_task;

use bip_types::action::PostconditionKind as Post;
use bip_types::identity::{
    ApprovalReceiptReference, DispatchId, GraphRevision, MonotonicMillis, NodeHandle, Origin,
    OriginKind, SemanticNodeId, SkillVersionId,
};
use bip_types::snapshot::{NodeState, SemanticRole};
use procedure_engine::*;
use reviewed_task::{evidence, seed, Digest, Driver, FIXTURE_ORIGIN};
use task_engine::action::{ActionIntent, BrowserIntent, ObservedNodeHandle};
use task_engine::handover::{HandoverCompletion, PersonInput};
use task_engine::{
    ActionOutcome, ActorLeaseId, Command, Milestone, ObservationCompleteness, TaskState,
    TaskTemplateId,
};

fn flow() -> Procedure {
    recorded_flow(false)
}

fn recorded_flow(include_list: bool) -> Procedure {
    use procedure_engine::{ArgumentDescriptor as Arg, RecordedValue as Value};

    let mut entries = vec![
        LedgerEntry::Described(
            StepDescriptor::new("browser.navigate", Post::CommittedNavigation).taking(vec![
                Arg::new(
                    0,
                    Value::PublicAddress(format!("{FIXTURE_ORIGIN}/documents")),
                ),
            ]),
        ),
        LedgerEntry::Described(StepDescriptor::new("browser.dom.read", Post::NoMutation)),
        LedgerEntry::Described(
            StepDescriptor::new("user.handover", Post::NoMutation)
                .taking(vec![Arg::new(0, Value::Choice { index: 1 })]),
        ),
        LedgerEntry::Described(
            StepDescriptor::new("browser.download.from_link", Post::BrowserFlowStarted).taking(
                vec![Arg::new(
                    0,
                    Value::SemanticTarget {
                        role: SemanticRole::Link,
                        phrase: PhraseId::Download,
                    },
                )],
            ),
        ),
    ];
    if include_list {
        entries.push(LedgerEntry::Described(StepDescriptor::new(
            "browser.download.list",
            Post::NoMutation,
        )));
    }
    let count = entries.len();
    let recording = Recording::new(
        policy_engine::origin::normalize_serialization(FIXTURE_ORIGIN).expect("origin"),
        vec![MatchClause::RolePresent(SemanticRole::Document)],
        entries,
        count,
    );
    let mut procedure = record_procedure(
        ProcedureId::new("learned.document").expect("id"),
        &recording,
        Milestone::M8,
    )
    .expect("recorded steps")
    .from_task("finished-task")
    .expect("complete recording");
    procedure = decode(&encode(&procedure).expect("stored definition")).expect("restored draft");
    procedure.status = transition(
        procedure.status,
        ProcedureStatus::Active,
        LifecycleActor::Person,
    )
    .expect("person accepted");
    procedure
}

fn driver() -> Driver {
    let mut seed = seed();
    seed.kind = task_engine::TaskKind::Errand;
    seed.control_mode = task_engine::ControlMode::Assistant;
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.milestone = Milestone::M8;
    seed.snapshot.skill_version_id = Some(SkillVersionId("learned.document@1".to_owned()));
    seed.snapshot.tool_allowlist = [
        "browser.navigate",
        "browser.dom.read",
        "user.handover",
        "browser.download.from_link",
        "browser.download.list",
    ]
    .into_iter()
    .map(str::to_owned)
    .collect();
    let mut driver = Driver::from_seed(seed);
    let mut preview = reviewed_task::preview();
    preview.budgets = preview
        .budgets
        .with(task_engine::BudgetKind::MaxModelRequests, 0);
    driver.apply(Command::StartTask(preview));
    driver.apply(Command::AcceptInitialConsent(ApprovalReceiptReference(
        "consent-1".to_owned(),
    )));
    driver
}

fn next(driver: &Driver, procedure: &Procedure, page: Option<ReplayPage<'_>>) -> Command {
    next_procedure_command_with_page(
        &driver.reducer,
        procedure,
        ObservedFields::none(),
        page,
        &Digest,
    )
    .expect("replay decided")
    .expect("next command")
}

fn apply_next(driver: &mut Driver, procedure: &Procedure, page: Option<ReplayPage<'_>>) -> Command {
    let command = next(driver, procedure, page);
    driver.apply(command.clone());
    command
}

fn complete(driver: &mut Driver, observed: bool) {
    let action_id = driver.authorize_pending_proposal();
    complete_authorized(driver, action_id, observed);
}

fn complete_authorized(
    driver: &mut Driver,
    action_id: bip_types::identity::ActionId,
    observed: bool,
) {
    driver.apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: bip_types::ActionResultCode::Verified,
            dispatch_id: Some(DispatchId::new("dispatch-1")),
            observed_at: MonotonicMillis(2_000),
            observation: observed.then(|| evidence(ObservationCompleteness::Complete)),
            discovered_source: None,
        }),
    });
}

fn node(observation: &task_engine::PageObservationEvidence) -> ReplayNode<'static> {
    ReplayNode {
        handle: ObservedNodeHandle::from_node_handle(&NodeHandle {
            tab_id: observation.tab_id.clone(),
            frame_id: observation.frame_id.clone(),
            page_epoch: observation.page_epoch.clone(),
            graph_revision: GraphRevision(observation.graph_revision),
            node_id: SemanticNodeId::new("download-current"),
            expected_origin: Origin {
                kind: OriginKind::Tuple,
                serialization: Some(FIXTURE_ORIGIN.to_owned()),
                opaque_id: None,
            },
        })
        .expect("complete handle"),
        role: SemanticRole::Link,
        label: "Download",
        states: &[NodeState::Visible, NodeState::Enabled],
    }
}

fn awaiting_download(procedure: &Procedure) -> Driver {
    let mut driver = driver();
    for _ in 0..3 {
        apply_next(&mut driver, procedure, None);
    }
    assert_eq!(
        driver.reducer.plan().expect("saved plan").steps().len(),
        procedure.steps.len() + 1
    );
    let Command::ProposeAction(navigate) = apply_next(&mut driver, procedure, None) else {
        panic!("navigation")
    };
    assert!(matches!(
        navigate.intent(),
        ActionIntent::Browser(BrowserIntent::Navigate { .. })
    ));
    complete(&mut driver, false);
    for _ in 0..3 {
        apply_next(&mut driver, procedure, None);
    }
    complete(&mut driver, true);
    for _ in 0..2 {
        apply_next(&mut driver, procedure, None);
    }
    let Command::RequestHandover { handover_id } = apply_next(&mut driver, procedure, None) else {
        panic!("handover")
    };
    assert_eq!(driver.reducer.task().state(), TaskState::WaitingUser);
    assert_eq!(
        next_procedure_command_with_page(
            &driver.reducer,
            procedure,
            ObservedFields::none(),
            None,
            &Digest
        ),
        Ok(None)
    );
    driver.apply(Command::CompleteHandover(
        HandoverCompletion::new(
            handover_id,
            ActorLeaseId::new("lease-before"),
            ActorLeaseId::new("lease-after"),
            PersonInput::observed(3),
        )
        .expect("handback"),
    ));
    for _ in 0..2 {
        apply_next(&mut driver, procedure, None);
    }
    let Command::ProposeAction(refresh) = apply_next(&mut driver, procedure, None) else {
        panic!("fresh read")
    };
    assert!(matches!(
        refresh.intent(),
        ActionIntent::Browser(BrowserIntent::DomRead { target: None, .. })
    ));
    assert!(refresh.plan_step_id.is_none());
    driver
}

#[test]
fn saved_navigation_handover_and_download_finish_only_with_owned_completion() {
    let procedure = flow();
    let mut driver = awaiting_download(&procedure);
    let observation = evidence(ObservationCompleteness::Complete);
    let nodes = [node(&observation)];
    let session = driver.reducer.task().snapshot().browser_session_id.clone();
    let page = ReplayPage {
        evidence: &observation,
        nodes: &nodes,
        browser_session_id: Some(&session),
        completed_downloads: 0,
        pending_downloads: Some(1),
    };
    // Even an identical earlier graph receipt cannot settle this fresh read.
    assert_eq!(
        next_procedure_command_with_page(
            &driver.reducer,
            &procedure,
            ObservedFields::none(),
            Some(page),
            &Digest
        ),
        Ok(None)
    );
    complete(&mut driver, true);
    for invalid_nodes in [
        vec![nodes[0].clone(), nodes[0].clone()],
        vec![ReplayNode {
            states: &[NodeState::Visible],
            ..nodes[0].clone()
        }],
    ] {
        assert_eq!(
            next_procedure_command_with_page(
                &driver.reducer,
                &procedure,
                ObservedFields::none(),
                Some(ReplayPage {
                    nodes: &invalid_nodes,
                    ..page
                }),
                &Digest
            ),
            Err(ReplayRefusal::HandleBindingUnavailable)
        );
    }
    let Command::ProposeAction(download) = apply_next(&mut driver, &procedure, Some(page)) else {
        panic!("download")
    };
    assert!(matches!(
        download.intent(),
        ActionIntent::Browser(BrowserIntent::DownloadFromLink { .. })
    ));
    complete(&mut driver, false);
    for _ in 0..2 {
        apply_next(&mut driver, &procedure, Some(page));
    }
    for pending in [None, Some(0), Some(2)] {
        assert_eq!(
            next(
                &driver,
                &procedure,
                Some(ReplayPage {
                    pending_downloads: pending,
                    ..page
                })
            ),
            Command::FailTask {
                reason: task_engine::FailureReason::UnverifiableAction
            }
        );
    }
    let Command::ProposeAction(poll) = apply_next(&mut driver, &procedure, Some(page)) else {
        panic!("download status")
    };
    assert!(matches!(
        poll.intent(),
        ActionIntent::Browser(BrowserIntent::DownloadList { .. })
    ));
    complete(&mut driver, false);
    assert_ne!(driver.reducer.task().state(), TaskState::Completed);
    let completed = ReplayPage {
        completed_downloads: 1,
        pending_downloads: Some(0),
        ..page
    };
    for _ in 0..3 {
        apply_next(&mut driver, &procedure, Some(completed));
    }
    assert_eq!(driver.reducer.task().state(), TaskState::Completed);
}

#[test]
fn recorded_download_list_waits_for_a_new_verified_page_before_binding_its_session() {
    let procedure = recorded_flow(true);
    assert_eq!(
        procedure.steps.last().expect("recorded poll").verb,
        "browser.download.list"
    );
    let mut driver = awaiting_download(&procedure);
    let observation = evidence(ObservationCompleteness::Complete);
    let nodes = [node(&observation)];
    let session = driver.reducer.task().snapshot().browser_session_id.clone();
    let page = ReplayPage {
        evidence: &observation,
        nodes: &nodes,
        browser_session_id: Some(&session),
        completed_downloads: 1,
        pending_downloads: Some(0),
    };
    complete(&mut driver, true);
    let Command::ProposeAction(download) = apply_next(&mut driver, &procedure, Some(page)) else {
        panic!("download")
    };
    assert!(matches!(
        download.intent(),
        ActionIntent::Browser(BrowserIntent::DownloadFromLink { .. })
    ));
    complete(&mut driver, false);
    apply_next(&mut driver, &procedure, Some(page));
    // The runtime retires page observation when a learned step succeeds.
    // The next recorded list cannot bind a session from the retired page.
    apply_next(&mut driver, &procedure, None);
    let Command::ProposeAction(refresh) = apply_next(&mut driver, &procedure, None) else {
        panic!("the explicit recorded list needs a fresh page")
    };
    assert!(matches!(
        refresh.intent(),
        ActionIntent::Browser(BrowserIntent::DomRead { target: None, .. })
    ));
    assert!(refresh.plan_step_id.is_none());
    let refresh_action = driver.authorize_pending_proposal();
    assert_eq!(
        next_procedure_command_with_page(
            &driver.reducer,
            &procedure,
            ObservedFields::none(),
            Some(page),
            &Digest,
        ),
        Ok(None),
        "page bytes and an older receipt cannot settle the pending refresh"
    );
    complete_authorized(&mut driver, refresh_action, true);
    assert_eq!(
        next_procedure_command_with_page(
            &driver.reducer,
            &procedure,
            ObservedFields::none(),
            Some(ReplayPage {
                browser_session_id: None,
                ..page
            }),
            &Digest,
        ),
        Err(ReplayRefusal::HandleBindingUnavailable),
        "a fresh page still needs the current browser session"
    );
    let Command::ProposeAction(list) = apply_next(&mut driver, &procedure, Some(page)) else {
        panic!("verified session-bound download list")
    };
    assert!(
        matches!(list.intent(), ActionIntent::Browser(BrowserIntent::DownloadList { browser_session_id, .. }) if browser_session_id == &session)
    );
    complete(&mut driver, false);
    for _ in 0..5 {
        apply_next(&mut driver, &procedure, Some(page));
    }
    assert_eq!(driver.reducer.task().state(), TaskState::Completed);
}
