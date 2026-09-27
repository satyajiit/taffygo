// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The paid-turn gate, driven with the production reducer and loop walk.

#![allow(clippy::expect_used, clippy::panic, clippy::unwrap_used)]

#[path = "pre_model_observation/moved_within_its_site.rs"]
mod moved_within_its_site;

use bip_types::identity::{
    ApprovalReceiptReference, DispatchId, FrameId, MonotonicMillis, PageEpoch, ProfileId, TabId,
    TaskId,
};
use bip_types::{ActionResultCode, Sensitivity};
use core_runtime::context::PageArena;
use kernel_test_support::ReferenceDigest;
use loop_kernel::state::LoopState;
use loop_kernel::walk;
use task_engine::action::{ActionIntent, ActionOutcome, BrowserIntent};
use task_engine::{
    Authorization, BrowserSessionId, BudgetDefaults, BudgetKind, Command, CommandEnvelope,
    ControlMode, IdempotencyKey, ManualClock, Milestone, ObservationCompleteness,
    ObservationGraphSummary, PageObservationEvidence, PolicyVersion, ProposalDecision,
    ProviderRouteId, Reducer, ScopePreview, SequentialIds, SourceId, SourceScope, TaskBudgets,
    TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, REVIEWED_OBSERVATION_TOOL,
};

type Task = Reducer<ManualClock, SequentialIds>;

struct Driver {
    task: Task,
    key: u64,
}

impl Driver {
    fn running() -> Self {
        let source_id = SourceId::from_bytes([7; 16]);
        let source = task_engine::ConsentedSource {
            source_id,
            tab_id: TabId::new("tab-live"),
            normalized_origin: "https://example.test".to_owned(),
            canonical_locator: None,
        };
        let budgets = TaskBudgets::none()
            .with(BudgetKind::MaxSources, 1)
            .with(BudgetKind::MaxModelRequests, 4);
        let seed = TaskSeed {
            task_id: TaskId::new("task-observe"),
            workspace_id: None,
            browser_profile_id: ProfileId::new("profile-1"),
            kind: TaskKind::Research,
            user_goal: "read this page".to_owned(),
            control_mode: ControlMode::Assistant,
            snapshot: TaskSnapshot {
                template_id: TaskTemplateId::BuildSourceTable,
                assistant_config_version: 1,
                skill_version_id: None,
                builtin_skill: None,
                tool_allowlist: vec![REVIEWED_OBSERVATION_TOOL.to_owned()],
                capability_policy_version: PolicyVersion(1),
                provider_route: ProviderRouteId::new("direct_user_key").ok(),
                consented_sources: Vec::new(),
                source_discovery_enabled: false,
                remaining_new_source_cap: 0,
                discovery_tab_id: None,
                library_refresh: None,
                browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
                milestone: Milestone::M3,
            },
            budgets: budgets.clone(),
            deadline: None,
            deadline_utc: None,
            predecessor_task_id: None,
        };
        let task = Reducer::create(
            seed,
            BudgetDefaults::new(|_| 8),
            ManualClock::at(1_000),
            SequentialIds::new(),
            IdempotencyKey::new("create"),
            TraceId::new("trace-create"),
        );
        let mut driver = Self { task, key: 0 };
        driver.apply(Command::StartTask(ScopePreview {
            scope: SourceScope::new().include(source_id),
            sources: vec![source],
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route: ProviderRouteId::new("direct_user_key").ok(),
            budgets,
        }));
        driver.apply(Command::AcceptInitialConsent(
            ApprovalReceiptReference::new("consent-1"),
        ));
        driver.apply(Command::ExecutorStarted);
        driver
    }

    fn running_comparison() -> Self {
        let first_id = SourceId::from_bytes([7; 16]);
        let second_id = SourceId::from_bytes([8; 16]);
        let sources = vec![
            task_engine::ConsentedSource {
                source_id: first_id,
                tab_id: TabId::new("tab-live"),
                normalized_origin: "https://example.test".to_owned(),
                canonical_locator: None,
            },
            task_engine::ConsentedSource {
                source_id: second_id,
                tab_id: TabId::new("tab-second"),
                normalized_origin: "https://second.test".to_owned(),
                canonical_locator: None,
            },
        ];
        let budgets = TaskBudgets::none()
            .with(BudgetKind::MaxSources, 2)
            .with(BudgetKind::MaxModelRequests, 4);
        let seed = TaskSeed {
            task_id: TaskId::new("task-compare"),
            workspace_id: None,
            browser_profile_id: ProfileId::new("profile-1"),
            kind: TaskKind::Research,
            user_goal: "compare these pages".to_owned(),
            control_mode: ControlMode::Assistant,
            snapshot: TaskSnapshot {
                template_id: TaskTemplateId::CompareProducts,
                assistant_config_version: 1,
                skill_version_id: None,
                builtin_skill: None,
                tool_allowlist: vec![REVIEWED_OBSERVATION_TOOL.to_owned()],
                capability_policy_version: PolicyVersion(1),
                provider_route: ProviderRouteId::new("direct_user_key").ok(),
                consented_sources: Vec::new(),
                source_discovery_enabled: false,
                remaining_new_source_cap: 0,
                discovery_tab_id: None,
                library_refresh: None,
                browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
                milestone: Milestone::M3,
            },
            budgets: budgets.clone(),
            deadline: None,
            deadline_utc: None,
            predecessor_task_id: None,
        };
        let task = Reducer::create(
            seed,
            BudgetDefaults::new(|_| 8),
            ManualClock::at(1_000),
            SequentialIds::new(),
            IdempotencyKey::new("create"),
            TraceId::new("trace-create"),
        );
        let mut driver = Self { task, key: 0 };
        driver.apply(Command::StartTask(ScopePreview {
            scope: SourceScope::new().include(first_id).include(second_id),
            sources,
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route: ProviderRouteId::new("direct_user_key").ok(),
            budgets,
        }));
        driver.apply(Command::AcceptInitialConsent(
            ApprovalReceiptReference::new("consent-1"),
        ));
        driver.apply(Command::ExecutorStarted);
        driver
    }

    fn apply(&mut self, command: Command) -> task_engine::Accepted {
        let key = self.key;
        self.key = self.key.saturating_add(1);
        self.task
            .apply(CommandEnvelope::new(
                IdempotencyKey::new(format!("command-{key}")),
                self.task.task().revision(),
                TraceId::new(format!("trace-{key}")),
                command,
            ))
            .unwrap()
    }

    fn advance(&self, state: &mut LoopState) -> Option<walk::PlannedCommand> {
        walk::advance(&self.task, state, &ReferenceDigest, 1, 2_000).unwrap()
    }

    fn propose_observation(
        &mut self,
        state: &mut LoopState,
    ) -> (bip_types::identity::ActionId, String, TabId) {
        let planned = self.advance(state).expect("the read is planned");
        let Command::ProposeAction(proposal) = &planned.envelope.command else {
            panic!("a paid call must be replaced by the page read")
        };
        let ActionIntent::Browser(BrowserIntent::DomRead { tab, target: None }) = proposal.intent()
        else {
            panic!("the prerequisite must be an exact whole-document read")
        };
        let tab = tab.clone();
        let key = proposal.idempotency_key.as_str().to_owned();
        let accepted = self.task.apply(planned.envelope).unwrap();
        assert!(matches!(
            accepted.effects.as_slice(),
            [task_engine::Effect::AskPolicy { .. }]
        ));
        let action_id = self
            .task
            .actions()
            .find(|action| action.proposal().idempotency_key.as_str() == key)
            .expect("the proposal minted an action")
            .action_id()
            .clone();
        (action_id, key, tab)
    }

    fn verify_observation(&mut self, action_id: bip_types::identity::ActionId) {
        self.verify_observation_with(action_id, evidence(), "1");
    }

    fn verify_observation_with(
        &mut self,
        action_id: bip_types::identity::ActionId,
        evidence: PageObservationEvidence,
        suffix: &str,
    ) {
        let policy = self.apply(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: task_engine::CapabilityId::new(format!("capability-{suffix}")),
            })),
            dispatch_id: Some(DispatchId::new(format!("dispatch-{suffix}"))),
        });
        assert!(matches!(
            policy.effects.as_slice(),
            [task_engine::Effect::DispatchAction { .. }]
        ));
        self.apply(Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(ActionOutcome {
                code: ActionResultCode::Verified,
                dispatch_id: Some(DispatchId::new(format!("dispatch-{suffix}"))),
                observed_at: MonotonicMillis(2_000),
                observation: Some(evidence),
                discovered_source: None,
            }),
        });
    }
}

fn evidence() -> PageObservationEvidence {
    evidence_for("tab-live", "https://example.test", "epoch-1")
}

fn evidence_for(tab_id: &str, origin: &str, epoch: &str) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new(tab_id),
        frame_id: FrameId("frame-main".to_owned()),
        page_epoch: PageEpoch(epoch.to_owned()),
        graph_revision: 9,
        normalized_origin: origin.to_owned(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 1,
            text_byte_count: 4,
        },
        total_bytes: 32,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    }
}

fn adopt(state: &mut LoopState, evidence: &PageObservationEvidence) {
    adopt_source(state, SourceId::from_bytes([7; 16]), evidence);
}

fn adopt_source(state: &mut LoopState, source_id: SourceId, evidence: &PageObservationEvidence) {
    state
        .page
        .replace_source(source_id, evidence, PageArena::new())
        .expect("canonical fixture identity");
}

#[test]
fn first_start_waits_for_verified_adoption_and_makes_only_one_paid_call() {
    let mut driver = Driver::running();
    let mut state = LoopState::default();
    let (action_id, _, _) = driver.propose_observation(&mut state);
    assert!(
        driver.advance(&mut state).is_none(),
        "policy is still pending"
    );

    driver.verify_observation(action_id);
    adopt(&mut state, &evidence());
    let model = driver
        .advance(&mut state)
        .expect("verified page opens the turn");
    assert!(matches!(
        model.envelope.command,
        Command::RequestModelTurn { .. }
    ));
    let accepted = driver.task.apply(model.envelope).unwrap();
    assert!(matches!(
        accepted.effects.as_slice(),
        [task_engine::Effect::CallModel { .. }]
    ));
    assert!(
        driver.advance(&mut state).is_none(),
        "the paid call is in flight"
    );
    assert_eq!(driver.task.turns_started(), 1);
}

#[test]
fn restore_and_stale_source_reobserve_without_reusing_the_journal_identity() {
    let mut driver = Driver::running();
    let mut live = LoopState::default();
    let (action_id, first_key, _) = driver.propose_observation(&mut live);
    driver.verify_observation(action_id);
    adopt(&mut live, &evidence());
    assert!(matches!(
        driver.advance(&mut live).map(|plan| plan.envelope.command),
        Some(Command::RequestModelTurn { .. })
    ));

    let mut restored = LoopState::default();
    let restored_read = driver.advance(&mut restored).expect("restore reobserves");
    let Command::ProposeAction(restored_proposal) = restored_read.envelope.command else {
        panic!("the restored empty arena cannot call the model")
    };
    assert_ne!(first_key, restored_proposal.idempotency_key.as_str());

    let mut stale = LoopState::default();
    let mut wrong = evidence();
    wrong.tab_id = TabId::new("tab-stale");
    adopt(&mut stale, &wrong);
    assert!(matches!(
        driver.advance(&mut stale).map(|plan| plan.envelope.command),
        Some(Command::ProposeAction(_))
    ));
}

#[test]
fn comparison_reads_overlap_and_reverse_completion_still_opens_only_one_paid_turn() {
    let mut driver = Driver::running_comparison();
    let mut state = LoopState::default();

    let (first_action, _, first_tab) = driver.propose_observation(&mut state);
    assert_eq!(first_tab, TabId::new("tab-live"));
    // The second tab is proposed while the first still awaits policy.
    let (second_action, _, second_tab) = driver.propose_observation(&mut state);
    assert_eq!(second_tab, TabId::new("tab-second"));
    assert!(driver.advance(&mut state).is_none());
    let second_evidence = evidence_for("tab-second", "https://second.test", "epoch-2");
    driver.verify_observation_with(second_action, second_evidence.clone(), "second");
    adopt_source(&mut state, SourceId::from_bytes([8; 16]), &second_evidence);
    assert!(
        driver.advance(&mut state).is_none(),
        "the first read is pending"
    );

    let first_evidence = evidence();
    // Receiving bytes ahead of their terminal receipt cannot unblock a turn.
    adopt_source(&mut state, SourceId::from_bytes([7; 16]), &first_evidence);
    assert!(driver.advance(&mut state).is_none());
    driver.verify_observation_with(first_action, first_evidence.clone(), "first");
    let sources = driver.task.task().consented_sources();
    let observations = state.page.matching_observations(sources);
    let source_order: Vec<_> = observations.iter().map(|live| live.source_id).collect();
    assert_eq!(
        source_order,
        sources
            .iter()
            .map(|source| source.source_id)
            .collect::<Vec<_>>()
    );

    let model = driver
        .advance(&mut state)
        .expect("both verified sources open the paid turn");
    assert!(matches!(
        model.envelope.command,
        Command::RequestModelTurn { .. }
    ));
    let accepted = driver.task.apply(model.envelope).unwrap();
    assert!(matches!(
        accepted.effects.as_slice(),
        [task_engine::Effect::CallModel { .. }]
    ));
    assert_eq!(driver.task.turns_started(), 1);
}
