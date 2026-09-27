// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Shared fixtures: a reducer driven into any of the thirteen durable states.
//!
//! Everything here is deterministic. The clock never moves on its own, the
//! identifier source counts, and no test observes anything a run of the same
//! script would not observe again.

#![allow(dead_code)]

pub mod agent;
pub mod command;
pub mod errand;
mod states;
pub mod value_sheet;

pub use self::states::*;

use bip_types::identity::{
    ActionId, ApprovalReceiptReference, ContentDigest, DigestAlgorithm, DispatchId, FrameId,
    PageEpoch, ProfileId, TabId, TaskId,
};
use bip_types::{ActionResultCode, Sensitivity};
use task_engine::action::{ActionIntent, ActionOutcome, ActionProposal, BrowserIntent};
use task_engine::authority::{
    Authorization, CapabilityId, ControlMode, PolicyVersion, ProposalDecision,
};
use task_engine::budget::{BudgetDefaults, BudgetKind, TaskBudgets};
use task_engine::command::{Command, CommandEnvelope};
use task_engine::ids::{ArtifactId, IdempotencyKey, SequentialIds};
use task_engine::permission::{
    PermissionDecision, PermissionRequest, PermissionRequestId, PermissionResult,
    PlatformPermission,
};
use task_engine::plan::{PlanDraft, StepDraft, StepKind};
use task_engine::reducer::{Reducer, TaskSeed};
use task_engine::task::{
    BrowserSessionId, GapReason, ScopePreview, SourceScope, TaskKind, TaskResult, TaskSnapshot,
    TaskState, TaskTemplateId, UnmetRequirement,
};
use task_engine::tool::Milestone;
use task_engine::{
    FactId, ManualClock, ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
    SourceId, TraceId,
};

/// The clock and identifier source every fixture uses.
pub type TestReducer = Reducer<ManualClock, SequentialIds>;

/// The read-oriented tool every fixture proposes.
pub const READ_TOOL: &str = "browser.dom.read";

/// A reducer plus the identifiers a test needs to talk about it.
pub struct Fixture {
    pub reducer: TestReducer,
    pub action_id: Option<ActionId>,
    pub artifact_id: ArtifactId,
    keys: u64,
}

impl Fixture {
    /// The next unused idempotency key.
    pub fn next_key(&mut self) -> IdempotencyKey {
        let issued = self.keys;
        self.keys = self.keys.saturating_add(1);
        IdempotencyKey::new(format!("key_{issued}"))
    }

    /// Wraps `command` in an envelope written against the current revision.
    pub fn envelope(&mut self, command: Command) -> CommandEnvelope {
        let revision = self.reducer.task().revision();
        let key = self.next_key();
        CommandEnvelope::new(key, revision, TraceId::new("trace_test"), command)
    }

    /// Applies `command` against the fixture's current revision and hands back
    /// what the reducer answered.
    ///
    /// One borrow rather than two, so a test can read the effects, the events
    /// and the refusal — which is most of what a test about a transition is
    /// actually for, and what [`Fixture::must_apply`] throws away.
    pub fn apply(
        &mut self,
        command: Command,
    ) -> Result<task_engine::Accepted, task_engine::Refusal> {
        let envelope = self.envelope(command);
        self.reducer.apply(envelope)
    }

    /// Applies `command`, and asserts when the script that builds a fixture is
    /// itself wrong.
    pub fn must_apply(&mut self, command: Command) {
        let envelope = self.envelope(command);
        let kind = envelope.kind();
        let result = self.reducer.apply(envelope);
        assert!(
            result.is_ok(),
            "fixture step {} refused: {result:?}",
            kind.label()
        );
    }

    /// Where the task is.
    pub fn state(&self) -> TaskState {
        self.reducer.task().state()
    }
}

/// The defaults every fixture runs under. Small numbers, so a budget test can
/// reach a limit without a long script.
pub fn defaults() -> BudgetDefaults {
    BudgetDefaults::new(|kind| match kind {
        BudgetKind::MaxSources => 4,
        BudgetKind::MaxWorkingTabs => 2,
        BudgetKind::MaxRetriesPerStep => 1,
        _ => 64,
    })
}

/// The task every fixture starts from.
pub fn seed() -> TaskSeed {
    TaskSeed {
        task_id: TaskId::new("task_1"),
        workspace_id: None,
        browser_profile_id: ProfileId::new("profile_1"),
        kind: TaskKind::Research,
        user_goal: "Compare two laptops".to_owned(),
        control_mode: ControlMode::Assistant,
        snapshot: TaskSnapshot {
            template_id: TaskTemplateId::CompareProducts,
            assistant_config_version: 1,
            skill_version_id: None,
            builtin_skill: None,
            tool_allowlist: Vec::new(),
            capability_policy_version: PolicyVersion(1),
            provider_route: None,
            consented_sources: Vec::new(),
            source_discovery_enabled: false,
            remaining_new_source_cap: 0,
            discovery_tab_id: None,
            library_refresh: None,
            browser_session_id: BrowserSessionId::new("browser_session_1")
                .unwrap_or_else(|_| unreachable!()),
            milestone: Milestone::M3,
        },
        budgets: TaskBudgets::none(),
        deadline: None,
        deadline_utc: None,
        predecessor_task_id: None,
    }
}

/// A fresh reducer holding a task in `DRAFT`.
pub fn draft() -> Fixture {
    draft_from(seed())
}

/// A draft over a caller-shaped seed, for the fixtures that need to change one
/// snapshot field before anything is applied.
pub fn draft_from(seed: TaskSeed) -> Fixture {
    draft_from_within(seed, defaults())
}

/// A draft over a caller-shaped seed and caller-shaped policy defaults.
///
/// [`defaults`] is deliberately generous so a budget test can reach a limit in
/// a short script. The product's factory is `BudgetDefaults::uniform(0)`, so a
/// test asking what the *product* does with an unstated limit has to say so.
pub fn draft_from_within(seed: TaskSeed, defaults: BudgetDefaults) -> Fixture {
    let reducer = Reducer::create(
        seed,
        defaults,
        ManualClock::at(1_000),
        SequentialIds::new(),
        IdempotencyKey::new("key_create"),
        TraceId::new("trace_test"),
    );
    Fixture {
        reducer,
        action_id: None,
        artifact_id: ArtifactId::new("artifact_0"),
        keys: 0,
    }
}

/// The preview a task consents to.
pub fn preview() -> ScopePreview {
    let source = source_id(1);
    ScopePreview {
        scope: SourceScope::new().include(source),
        sources: vec![task_engine::ConsentedSource {
            source_id: source,
            tab_id: TabId::new("tab_1"),
            normalized_origin: "https://example.test".to_owned(),
            canonical_locator: None,
        }],
        source_discovery_enabled: false,
        new_source_cap: 0,
        provider_route: None,
        budgets: TaskBudgets::none().with(BudgetKind::MaxSources, 1),
    }
}

/// A well-formed two-step plan.
pub fn plan_draft() -> PlanDraft {
    PlanDraft {
        summary: "Read both pages and compare".to_owned(),
        steps: vec![
            StepDraft {
                kind: StepKind::Observe,
                description: "Read the first page".to_owned(),
                dependencies: vec![],
            },
            StepDraft {
                kind: StepKind::Extract,
                description: "Pull the values out".to_owned(),
                dependencies: vec![0],
            },
        ],
    }
}

/// A proposal for the read-oriented tool, keyed by `key`.
pub fn proposal(key: &str) -> ActionProposal {
    proposal_for(
        BrowserIntent::DomRead {
            tab: TabId::new("tab_1"),
            target: None,
        },
        key,
    )
}

/// A proposal for one typed browser operation, keyed by `key`.
pub fn proposal_for(intent: BrowserIntent, key: &str) -> ActionProposal {
    ActionProposal::new(
        ActionIntent::Browser(intent),
        None,
        IdempotencyKey::new(key),
        false,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "0".repeat(64),
        },
    )
}

/// A policy decision that grants authority.
pub fn authorize() -> ProposalDecision {
    ProposalDecision::Authorize(Authorization {
        capability_id: CapabilityId::new("cap_0"),
    })
}

/// The shape of one recorded model turn: counts and closed enumerations, and
/// nothing a sentence could travel in.
pub fn turn_digest() -> task_engine::TurnDigest {
    task_engine::TurnDigest {
        stop: task_engine::ModelStopReason::Complete,
        overflow: None,
        usage: task_engine::TurnUsage::default(),
        answer_segments: 1,
        tool_calls: 0,
        refused_tool_calls: 0,
        render: task_engine::RenderShape::empty([0_u8; 32]),
    }
}

/// A verified outcome.
pub fn verified_outcome() -> ActionOutcome {
    ActionOutcome {
        code: ActionResultCode::Verified,
        dispatch_id: Some(DispatchId::new("dispatch_0")),
        observed_at: bip_types::identity::MonotonicMillis(10),
        observation: Some(PageObservationEvidence {
            service_generation: 1,
            schema_version: "2.4".to_owned(),
            tab_id: TabId::new("tab_1"),
            frame_id: FrameId("frame_1".to_owned()),
            page_epoch: PageEpoch("epoch_1".to_owned()),
            graph_revision: 1,
            normalized_origin: "https://example.test".to_owned(),
            private_profile: false,
            completeness: ObservationCompleteness::Complete,
            graph: ObservationGraphSummary {
                node_count: 2,
                relationship_count: 1,
                named_node_count: 1,
                text_run_count: 2,
                text_byte_count: 24,
            },
            total_bytes: 64,
            truncated: false,
            may_change_answer: false,
            redacted_field_count: 0,
            suppressed_secret_value_count: 0,
            sensitive_zone_count: 0,
            policy_filtered_frame_count: 0,
            highest_sensitivity: Sensitivity::NotSensitive,
        }),
        discovered_source: None,
    }
}

/// A result with nothing left unmet.
pub fn complete_result() -> TaskResult {
    TaskResult {
        artifact_ids: vec![ArtifactId::new("artifact_0")],
        unmet: Vec::new(),
        fact_count: 2,
        source_count: 1,
    }
}

/// A result with a labelled gap.
pub fn partial_result() -> TaskResult {
    TaskResult {
        artifact_ids: vec![ArtifactId::new("artifact_0")],
        unmet: vec![UnmetRequirement {
            subject: "battery life".to_owned(),
            reason: GapReason::NotFoundInScope,
        }],
        fact_count: 1,
        source_count: 1,
    }
}

/// A receipt the consent surface recorded.
pub fn receipt() -> ApprovalReceiptReference {
    ApprovalReceiptReference("approval_0".to_owned())
}

/// One exact native permission request.
pub fn permission_request() -> PermissionRequest {
    PermissionRequest::new(
        PermissionRequestId::new("permission_0").unwrap_or_else(|_| unreachable!()),
        PlatformPermission::Notifications,
        10_000,
        10_000,
        BrowserSessionId::new("browser_session_1").unwrap_or_else(|_| unreachable!()),
    )
    .unwrap_or_else(|_| unreachable!())
}

/// The granted result for the exact native permission request.
pub fn permission_result() -> PermissionResult {
    PermissionResult::new(
        PermissionRequestId::new("permission_0").unwrap_or_else(|_| unreachable!()),
        PlatformPermission::Notifications,
        PermissionDecision::Granted,
    )
}

/// A stable source identifier.
pub fn source_id(seed: u8) -> SourceId {
    let mut bytes = [0_u8; 16];
    bytes[15] = seed;
    SourceId::from_bytes(bytes)
}

/// A stable fact identifier.
pub fn fact_id(seed: u8) -> FactId {
    let mut bytes = [0_u8; 16];
    bytes[15] = seed;
    FactId::from_bytes(bytes)
}
