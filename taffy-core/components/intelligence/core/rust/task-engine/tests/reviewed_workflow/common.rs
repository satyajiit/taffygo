// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#![allow(dead_code)]

use bip_types::identity::{
    ApprovalReceiptReference, DispatchId, FrameId, MonotonicMillis, PageEpoch, ProfileId, TabId,
    TaskId,
};
use bip_types::{ActionResultCode, Sensitivity};
use task_engine::{
    ActionOutcome, Authorization, BrowserSessionId, BudgetDefaults, Command, CommandEnvelope,
    ConsentedSource, ControlMode, Effect, IdempotencyKey, ManualClock, Milestone,
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence, PolicyVersion,
    ProposalDecision, Reducer, ScopePreview, SequentialIds, SourceId, SourceScope, TaskBudgets,
    TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, WorkflowDigest, WorkflowError,
};

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
        normalized_origin: "https://example.test".to_owned(),
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
            tool_allowlist: vec!["browser.dom.read".to_owned()],
            capability_policy_version: PolicyVersion(1),
            provider_route: Some(
                task_engine::ProviderRouteId::new("no_model_required")
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

pub fn defaults() -> BudgetDefaults {
    BudgetDefaults::uniform(8)
}

pub fn preview() -> ScopePreview {
    ScopePreview {
        scope: SourceScope::new().include(source_id()),
        sources: vec![source()],
        source_discovery_enabled: false,
        new_source_cap: 0,
        provider_route: Some(
            task_engine::ProviderRouteId::new("no_model_required")
                .unwrap_or_else(|_| unreachable!()),
        ),
        budgets: TaskBudgets::none().with(task_engine::BudgetKind::MaxSources, 1),
    }
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
                defaults(),
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
            .unwrap_or_else(|refusal| unreachable!("reviewed command refused: {refusal:?}"))
    }

    pub fn apply_next(&mut self) -> task_engine::Accepted {
        let command = self
            .reducer
            .next_reviewed_command(&Digest)
            .unwrap_or_else(|error| unreachable!("reviewed workflow failed: {error:?}"))
            .unwrap_or_else(|| unreachable!("workflow is waiting"));
        self.apply(command)
    }

    pub fn enter_observation_dispatch(&mut self) -> bip_types::identity::ActionId {
        self.apply(Command::StartTask(preview()));
        self.apply(Command::AcceptInitialConsent(ApprovalReceiptReference(
            "consent-1".to_owned(),
        )));
        self.apply_next();
        self.apply_next();
        self.apply_next();
        let proposal = self.apply_next();
        assert!(matches!(
            proposal.effects.as_slice(),
            [Effect::AskPolicy { .. }]
        ));
        let action_id = self.reducer.actions().next().map_or_else(
            || unreachable!("proposal minted an action"),
            |action| action.action_id().clone(),
        );
        let policy = self.apply(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: task_engine::CapabilityId::new("capability-1"),
            })),
            dispatch_id: Some(DispatchId::new("dispatch-1")),
        });
        assert!(matches!(
            policy.effects.as_slice(),
            [Effect::DispatchAction { .. }]
        ));
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
        normalized_origin: "https://example.test".to_owned(),
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
    action_id: bip_types::identity::ActionId,
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
