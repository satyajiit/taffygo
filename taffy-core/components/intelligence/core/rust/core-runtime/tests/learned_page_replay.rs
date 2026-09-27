// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The production walk resolves a saved step against current page evidence.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use bip_types::action::PostconditionKind;
use bip_types::identity::{
    ApprovalReceiptReference, DispatchId, FrameId, MonotonicMillis, PageEpoch, ProfileId,
    SkillVersionId, TabId, TaskId,
};
use bip_types::snapshot::{ContentTrust, NodeState, SemanticRole, Sensitivity};
use bip_types::ActionResultCode;
use kernel_test_support::ReferenceDigest;
use loop_kernel::context::arena::{ArenaNode, DestinationClass, PageArena};
use loop_kernel::state::LoopState;
use loop_kernel::walk::{advance_with_procedure, PlannedCommand};
use procedure_engine::{PhraseId, Procedure, ProcedureStep, StepArgument, StepValue};
use task_engine::action::{ActionIntent, ActionOutcome, BrowserIntent};
use task_engine::{
    Authorization, BrowserSessionId, BudgetDefaults, CapabilityId, Command, CommandEnvelope,
    ConsentedSource, ControlMode, IdempotencyKey, ManualClock, Milestone, ObservationCompleteness,
    ObservationGraphSummary, PageObservationEvidence, PolicyVersion, ProposalDecision,
    ProviderRouteId, Reducer, ScopePreview, SequentialIds, SourceId, SourceScope, TaskBudgets,
    TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, REVIEWED_NO_MODEL_ROUTE_ID,
};

type Task = Reducer<ManualClock, SequentialIds>;

fn source() -> ConsentedSource {
    ConsentedSource {
        source_id: SourceId::from_bytes([7; 16]),
        tab_id: TabId::new("tab-live"),
        normalized_origin: "https://example.test".to_owned(),
        canonical_locator: None,
    }
}

fn task() -> Task {
    let route = ProviderRouteId::new(REVIEWED_NO_MODEL_ROUTE_ID).unwrap();
    let mut task = Reducer::create(
        TaskSeed {
            task_id: TaskId::new("task-replay"),
            workspace_id: None,
            browser_profile_id: ProfileId::new("profile-1"),
            kind: TaskKind::Errand,
            user_goal: "Open the next page".to_owned(),
            control_mode: ControlMode::Assistant,
            snapshot: TaskSnapshot {
                template_id: TaskTemplateId::WebErrand,
                assistant_config_version: 1,
                skill_version_id: Some(SkillVersionId("saved-flow-v1".to_owned())),
                builtin_skill: None,
                tool_allowlist: vec![
                    "browser.dom.read".to_owned(),
                    "browser.link.open".to_owned(),
                ],
                capability_policy_version: PolicyVersion(1),
                provider_route: Some(route.clone()),
                consented_sources: Vec::new(),
                source_discovery_enabled: false,
                remaining_new_source_cap: 0,
                discovery_tab_id: None,
                library_refresh: None,
                browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
                milestone: Milestone::M8,
            },
            budgets: TaskBudgets::none(),
            deadline: None,
            deadline_utc: None,
            predecessor_task_id: None,
        },
        BudgetDefaults::uniform(16),
        ManualClock::at(1_000),
        SequentialIds::new(),
        IdempotencyKey::new("create"),
        TraceId::new("trace-create"),
    );
    apply(
        &mut task,
        Command::StartTask(ScopePreview {
            scope: SourceScope::new().include(source().source_id),
            sources: vec![source()],
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route: Some(route),
            budgets: TaskBudgets::none()
                .with(task_engine::BudgetKind::MaxSources, 1)
                .with(task_engine::BudgetKind::MaxModelRequests, 0),
        }),
    );
    apply(
        &mut task,
        Command::AcceptInitialConsent(ApprovalReceiptReference::new("consent")),
    );
    task
}

fn apply(task: &mut Task, command: Command) {
    let revision = task.task().revision();
    task.apply(CommandEnvelope::new(
        IdempotencyKey::new(format!("test-{revision}")),
        revision,
        TraceId::new(format!("test-{revision}")),
        command,
    ))
    .unwrap();
}

fn procedure() -> Procedure {
    let origin = policy_engine::origin::normalize_serialization("https://example.test").unwrap();
    let mut procedure = procedure_engine::build_source_table(&origin).unwrap();
    procedure.steps =
        vec![
            ProcedureStep::new("browser.link.open", PostconditionKind::CommittedNavigation).taking(
                vec![StepArgument::new(
                    "node",
                    StepValue::SemanticTarget {
                        role: SemanticRole::Link,
                        phrase: PhraseId::Continue,
                    },
                )],
            ),
        ];
    procedure
}

fn next(task: &Task, state: &mut LoopState, procedure: &Procedure) -> Option<PlannedCommand> {
    advance_with_procedure(
        task,
        Some(procedure),
        None,
        state,
        &ReferenceDigest,
        1,
        1_000,
    )
    .unwrap()
}

fn observation() -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: source().tab_id,
        frame_id: FrameId::new("frame-live"),
        page_epoch: PageEpoch::new("fresh-page"),
        graph_revision: 4,
        normalized_origin: source().normalized_origin,
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 0,
            text_byte_count: 0,
        },
        total_bytes: 128,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    }
}

fn page() -> PageArena {
    let mut page = PageArena::new();
    assert!(page.push(ArenaNode {
        node_id: "current-continue".to_owned(),
        role: SemanticRole::Link,
        sensitivity: Sensitivity::NotSensitive,
        name: Some("Continue".to_owned()),
        name_withheld: false,
        actions: Vec::new(),
        states: vec![NodeState::Visible, NodeState::Enabled],
        destination: DestinationClass::from_bits(1),
        content_trust: ContentTrust::FirstPartyDocument,
        content_signals: Vec::new(),
        text: Vec::new(),
        text_withheld: false,
        declared_text_runs: 0,
        declared_text_bytes: 0,
        container: None,
    }));
    page
}

#[test]
fn saved_step_uses_a_fresh_complete_read_and_never_a_saved_node_number() {
    let mut task = task();
    let procedure = procedure();
    let mut state = LoopState::default();
    let mut planned = next(&task, &mut state, &procedure).unwrap();
    for _ in 0..8 {
        if matches!(planned.envelope.command, Command::ProposeAction(_)) {
            break;
        }
        task.apply(planned.envelope).unwrap();
        planned = next(&task, &mut state, &procedure).unwrap();
    }
    let Command::ProposeAction(proposal) = &planned.envelope.command else {
        panic!("the step must observe first");
    };
    assert!(matches!(
        proposal.intent(),
        ActionIntent::Browser(BrowserIntent::DomRead { target: None, .. })
    ));
    assert!(
        proposal.plan_step_id.is_none(),
        "a preparation read cannot finish the saved step"
    );
    task.apply(planned.envelope).unwrap();
    let action = task.actions().next().unwrap().action_id().clone();
    apply(
        &mut task,
        Command::RecordPolicyDecision {
            action_id: action.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new("read-cap"),
            })),
            dispatch_id: Some(DispatchId::new("read-dispatch")),
        },
    );
    state
        .page
        .replace_source(source().source_id, &observation(), page())
        .unwrap();
    assert!(
        next(&task, &mut state, &procedure).is_none(),
        "page bytes alone cannot cross the commit barrier"
    );
    apply(
        &mut task,
        Command::RecordActionOutcome {
            action_id: action,
            outcome: Box::new(ActionOutcome {
                code: ActionResultCode::Verified,
                dispatch_id: Some(DispatchId::new("read-dispatch")),
                observed_at: MonotonicMillis(2_000),
                observation: Some(observation()),
                discovered_source: None,
            }),
        },
    );
    let planned = next(&task, &mut state, &procedure).unwrap();
    let Command::ProposeAction(proposal) = planned.envelope.command else {
        panic!("the verified page must enable the saved link step");
    };
    let ActionIntent::Browser(BrowserIntent::LinkOpen { target }) = proposal.intent() else {
        panic!("the saved verb remains an ordinary browser link action");
    };
    assert_eq!(target.node_id().as_str(), "current-continue");
    assert_eq!(target.page_epoch().as_str(), "fresh-page");
    assert_eq!(target.graph_revision().0, 4);
    assert_eq!(state.page.preview().handles.issued(), 1);
}
