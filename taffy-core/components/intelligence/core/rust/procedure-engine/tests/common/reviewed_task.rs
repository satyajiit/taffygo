// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One reviewed `BuildSourceTable` task, drivable by either path.
//!
//! The fixture is deliberately the *reviewed* workflow's own: the same seed,
//! the same single consented source, the same provider route and the same
//! allowlist. An oracle that compared the two paths on a task tailored to the
//! procedure would be comparing them on a task the hard-coded half was never
//! written for, which is the one comparison that could pass while the claim
//! being tested is false.

#![allow(dead_code)]

use bip_types::identity::{
    ActionId, ApprovalReceiptReference, DispatchId, FrameId, MonotonicMillis, PageEpoch, ProfileId,
    TabId, TaskId,
};
use bip_types::{ActionResultCode, Sensitivity};
use procedure_engine::builtin::build_source_table;
use procedure_engine::record::Procedure;
use task_engine::action::ActionState;
use task_engine::{
    ActionOutcome, Authorization, BrowserSessionId, BudgetDefaults, CapabilityId, Command,
    CommandEnvelope, ConsentedSource, ControlMode, Denial, IdempotencyKey, ManualClock, Milestone,
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence, PolicyVersion,
    ProposalDecision, Reducer, ScopePreview, SequentialIds, SourceId, SourceScope, TaskBudgets,
    TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, WorkflowDigest, WorkflowError,
};

/// The origin the fixture task's one consented source is on.
pub const FIXTURE_ORIGIN: &str = "https://example.test";

/// A local, deterministic stand-in for the browser's digest adapter.
///
/// It is not SHA-256 and does not need to be: what the oracle asserts is that
/// two paths hand the *same bytes* to the same adapter, and a real hash would
/// hide a difference behind an avalanche just as a fake one does.
pub struct Digest;

impl WorkflowDigest for Digest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], WorkflowError> {
        let mut output = [0_u8; 32];
        let output_len = output.len();
        for (index, byte) in input.iter().enumerate() {
            if let Some(slot) = output.get_mut(index % output_len) {
                *slot = slot.wrapping_mul(31).wrapping_add(*byte);
            }
        }
        Ok(output)
    }
}

fn source_id() -> SourceId {
    SourceId::from_bytes([3; 16])
}

fn source() -> ConsentedSource {
    ConsentedSource {
        source_id: source_id(),
        tab_id: TabId::new("tab-live"),
        normalized_origin: FIXTURE_ORIGIN.to_owned(),
        canonical_locator: None,
    }
}

pub fn seed() -> TaskSeed {
    TaskSeed {
        task_id: TaskId::new("task-source-table"),
        workspace_id: None,
        browser_profile_id: ProfileId::new("profile-1"),
        kind: TaskKind::Research,
        user_goal: "Show the structure of this source".to_owned(),
        control_mode: ControlMode::Shared,
        snapshot: TaskSnapshot {
            template_id: TaskTemplateId::BuildSourceTable,
            assistant_config_version: 1,
            skill_version_id: None,
            builtin_skill: None,
            tool_allowlist: vec![task_engine::REVIEWED_OBSERVATION_TOOL.to_owned()],
            capability_policy_version: PolicyVersion(1),
            provider_route: Some(
                task_engine::ProviderRouteId::new(task_engine::REVIEWED_NO_MODEL_ROUTE_ID)
                    .unwrap_or_else(|_| unreachable!()),
            ),
            consented_sources: Vec::new(),
            source_discovery_enabled: false,
            remaining_new_source_cap: 0,
            discovery_tab_id: None,
            library_refresh: None,
            browser_session_id: BrowserSessionId::new("browser-session-1")
                .unwrap_or_else(|_| unreachable!()),
            milestone: Milestone::M3,
        },
        budgets: TaskBudgets::none(),
        deadline: None,
        deadline_utc: None,
        predecessor_task_id: None,
    }
}

pub fn preview() -> ScopePreview {
    ScopePreview {
        scope: SourceScope::new().include(source_id()),
        sources: vec![source()],
        source_discovery_enabled: false,
        new_source_cap: 0,
        provider_route: Some(
            task_engine::ProviderRouteId::new(task_engine::REVIEWED_NO_MODEL_ROUTE_ID)
                .unwrap_or_else(|_| unreachable!()),
        ),
        budgets: TaskBudgets::none().with(task_engine::BudgetKind::MaxSources, 1),
    }
}

/// The shipped procedure, minted for the origin this task consented to.
pub fn procedure_for_task() -> Procedure {
    let Ok(origin) = policy_engine::origin::normalize_serialization(FIXTURE_ORIGIN) else {
        unreachable!("the fixture origin is an origin")
    };
    let Ok(procedure) = build_source_table(&origin) else {
        unreachable!("the built-in is well formed")
    };
    procedure
}

pub struct Driver {
    pub reducer: Reducer<ManualClock, SequentialIds>,
    next_key: u64,
}

impl Driver {
    pub fn new() -> Self {
        Self::from_seed(seed())
    }

    pub fn from_seed(seed: TaskSeed) -> Self {
        Self {
            reducer: Reducer::create(
                seed,
                BudgetDefaults::uniform(8),
                ManualClock::at(1_000),
                SequentialIds::new(),
                IdempotencyKey::new("create"),
                TraceId::new("trace-create"),
            ),
            next_key: 0,
        }
    }

    pub fn apply(&mut self, command: Command) -> task_engine::Accepted {
        let issued = self.next_key;
        self.next_key = self.next_key.saturating_add(1);
        let envelope = CommandEnvelope::new(
            IdempotencyKey::new(format!("command-{issued}")),
            self.reducer.task().revision(),
            TraceId::new(format!("trace-{issued}")),
            command,
        );
        self.reducer
            .apply(envelope)
            .unwrap_or_else(|refusal| unreachable!("command refused: {refusal:?}"))
    }

    /// Consent, so the task reaches `QUEUED` with its one source in scope.
    pub fn start(&mut self) {
        self.apply(Command::StartTask(preview()));
        self.apply(Command::AcceptInitialConsent(ApprovalReceiptReference(
            "consent-1".to_owned(),
        )));
    }

    /// Commits whatever the reviewed workflow says is next.
    pub fn apply_reviewed(&mut self) -> task_engine::Accepted {
        let command = self
            .reducer
            .next_reviewed_command(&Digest)
            .unwrap_or_else(|error| unreachable!("reviewed workflow failed: {error:?}"))
            .unwrap_or_else(|| unreachable!("the reviewed workflow is waiting"));
        self.apply(command)
    }

    /// The one action still in flight.
    ///
    /// Selected by state rather than by position, because a task that retries
    /// holds more than one action record and every earlier one is terminal.
    /// Taking the first would hand a settled attempt back and drive the
    /// retry cases against an action nobody is waiting on.
    fn pending_action(&self) -> ActionId {
        self.reducer
            .actions()
            .find(|action| {
                matches!(
                    action.state(),
                    ActionState::Proposed
                        | ActionState::WaitingApproval
                        | ActionState::Authorized
                        | ActionState::Dispatching
                        | ActionState::Verifying
                )
            })
            .map_or_else(
                || unreachable!("a proposal minted an action"),
                |action| action.action_id().clone(),
            )
    }

    /// Policy authorizes the one proposed action and the broker takes it.
    pub fn dispatch_pending_proposal(&mut self) -> ActionId {
        let action_id = self.pending_action();
        self.apply(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new("capability-1"),
            })),
            dispatch_id: Some(DispatchId::new("dispatch-1")),
        });
        action_id
    }

    /// The same, named for what the caller is about to do with it.
    pub fn authorize_pending_proposal(&mut self) -> ActionId {
        self.dispatch_pending_proposal()
    }

    /// The dispatched action comes back having not touched the page.
    ///
    /// `StaleGraph` is `NotPerformed`, is not a cancellation and needs no
    /// decision from the person, so the attempt lands in `ActionState::Failed`
    /// — the one terminal state that leaves a retry open. This is what lets a
    /// test reach the second attempt, and the attempt after the ceiling.
    pub fn fail_dispatched_action(&mut self, action_id: ActionId) -> task_engine::Accepted {
        self.apply(Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(ActionOutcome {
                code: ActionResultCode::StaleGraph,
                dispatch_id: Some(DispatchId::new("dispatch-1")),
                observed_at: MonotonicMillis(2_000),
                observation: None,
                discovered_source: None,
            }),
        })
    }

    /// Policy refuses the one proposed action.
    pub fn deny_pending_proposal(&mut self) -> ActionId {
        let action_id = self.pending_action();
        self.apply(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Deny(Denial::new(
                ActionResultCode::DeniedByPolicy,
            ))),
            dispatch_id: None,
        });
        action_id
    }
}

pub fn evidence(completeness: ObservationCompleteness) -> PageObservationEvidence {
    let incomplete = completeness == ObservationCompleteness::Incomplete;
    PageObservationEvidence {
        service_generation: 4,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new("tab-live"),
        frame_id: FrameId("frame-main".to_owned()),
        page_epoch: PageEpoch("epoch-current".to_owned()),
        graph_revision: 9,
        normalized_origin: FIXTURE_ORIGIN.to_owned(),
        private_profile: false,
        completeness,
        graph: ObservationGraphSummary {
            node_count: 4,
            relationship_count: 3,
            named_node_count: 2,
            text_run_count: 6,
            text_byte_count: 128,
        },
        total_bytes: 256,
        truncated: incomplete,
        may_change_answer: incomplete,
        redacted_field_count: 1,
        suppressed_secret_value_count: 1,
        sensitive_zone_count: 1,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::Credential,
    }
}

pub fn complete_observation(
    driver: &mut Driver,
    action_id: ActionId,
    completeness: ObservationCompleteness,
) -> task_engine::Accepted {
    driver.apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(DispatchId::new("dispatch-1")),
            observed_at: MonotonicMillis(2_000),
            observation: Some(evidence(completeness)),
            discovered_source: None,
        }),
    })
}
