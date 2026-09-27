// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Real production ports with explicit simulated browser acknowledgements.

use bip_types::identity::{
    ApprovalReceiptReference, ContentDigest, DigestAlgorithm, DispatchId, FrameId, MonotonicMillis,
    PageEpoch, ProfileId, TabId, TaskId,
};
use bip_types::snapshot::{ContentTrust, NodeState, SemanticRole, Sensitivity};
use task_engine::action::{ActionIntent, BrowserIntent, ObservedNodeHandle};
use task_engine::*;

use crate::contract::{Completion, Deadline, EffectRequest, OperationEnvelope, OperationId};
use crate::ports::{InitialConsentAdmission, TaskEngineLoad, TaskIdEntropy};
use crate::ProfileServiceRuntime;
use crate::{BeginSubmit, CommitOutcome};

pub(crate) fn task_id() -> TaskId {
    TaskId::new("recorded-task")
}

pub(crate) fn source() -> ConsentedSource {
    ConsentedSource {
        source_id: SourceId::from_bytes([3; 16]),
        tab_id: TabId::new("tab-1"),
        normalized_origin: "https://example.test".to_owned(),
        canonical_locator: None,
    }
}

pub(crate) fn load() -> TaskEngineLoad {
    let route = ProviderRouteId::new("direct_user_key").unwrap();
    let seed = TaskSeed {
        task_id: task_id(),
        workspace_id: None,
        browser_profile_id: ProfileId::new("profile-1"),
        kind: TaskKind::Errand,
        user_goal: "Download the public document".to_owned(),
        control_mode: ControlMode::Assistant,
        snapshot: TaskSnapshot {
            template_id: TaskTemplateId::WebErrand,
            assistant_config_version: 1,
            skill_version_id: None,
            builtin_skill: None,
            tool_allowlist: vec![
                "browser.navigate".to_owned(),
                "browser.dom.read".to_owned(),
                "browser.download.from_link".to_owned(),
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
    };
    TaskEngineLoad::fresh(
        seed,
        IdempotencyKey::new("create"),
        TraceId::new("create"),
        TaskIdEntropy::new(core::array::from_fn(|index| {
            u8::try_from(index + 1).unwrap()
        }))
        .unwrap(),
        InitialConsentAdmission {
            preview: ScopePreview {
                scope: SourceScope::new().include(source().source_id),
                sources: vec![source()],
                source_discovery_enabled: true,
                new_source_cap: 1,
                provider_route: Some(route),
                budgets: TaskBudgets::none()
                    .with(BudgetKind::MaxSources, 2)
                    .with(BudgetKind::MaxModelRequests, 64),
            },
            receipt: ApprovalReceiptReference::new("consent"),
            start_key: IdempotencyKey::new("start"),
            start_trace: TraceId::new("start"),
            consent_key: IdempotencyKey::new("consent"),
            consent_trace: TraceId::new("consent"),
        },
    )
}

pub(super) fn driver() -> ProfileServiceRuntime {
    driver_for(load())
}

pub(crate) fn driver_for(load: TaskEngineLoad) -> ProfileServiceRuntime {
    let mut runtime = super::super::built_runtime();
    let open = runtime
        .core_mut()
        .begin_open_task(
            load,
            OperationId::new("open").unwrap(),
            Deadline::from_millis(10_000),
            1,
            1_000,
        )
        .unwrap();
    let terminal = open.commit.with_body(Completion::StorageCommitted {
        committed_revision: open.commit.task_revision,
    });
    runtime
        .core_mut()
        .complete_open_task(&task_id(), terminal, 2)
        .unwrap();
    apply(&mut runtime, Command::ExecutorStarted);
    runtime
}

pub(crate) fn begin(
    runtime: &mut ProfileServiceRuntime,
    command: Command,
) -> OperationEnvelope<EffectRequest> {
    let revision = runtime.core().task(&task_id()).unwrap().revision();
    let result = runtime
        .core_mut()
        .begin_submit(
            &task_id(),
            CommandEnvelope::new(
                IdempotencyKey::new(format!("command-{revision}")),
                revision,
                TraceId::new(format!("trace-{revision}")),
                command,
            ),
            OperationId::new(format!("operation-{revision}")).unwrap(),
            Deadline::from_millis(10_000),
            1,
            1_000,
        )
        .unwrap();
    let BeginSubmit::AwaitingCommit(commit) = result else {
        panic!("new command requires durability")
    };
    *commit
}

pub(crate) fn commit(
    runtime: &mut ProfileServiceRuntime,
    pending: &OperationEnvelope<EffectRequest>,
) -> Accepted {
    let terminal = pending.with_body(Completion::StorageCommitted {
        committed_revision: pending.task_revision,
    });
    let CommitOutcome::Committed(accepted) = runtime
        .core_mut()
        .complete_commit(&task_id(), terminal, 2)
        .unwrap()
    else {
        panic!("exact storage ack")
    };
    accepted
}

pub(crate) fn apply(runtime: &mut ProfileServiceRuntime, command: Command) -> Accepted {
    let pending = begin(runtime, command);
    commit(runtime, &pending)
}

pub(crate) fn action(
    runtime: &mut ProfileServiceRuntime,
    intent: BrowserIntent,
    observation: Option<PageObservationEvidence>,
) {
    let revision = runtime.core().task(&task_id()).unwrap().revision();
    let proposal = ActionProposal::new(
        ActionIntent::Browser(intent),
        None,
        IdempotencyKey::new(format!("action-{revision}")),
        true,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a".repeat(64),
        },
    );
    let accepted = apply(runtime, Command::ProposeAction(Box::new(proposal)));
    let Effect::AskPolicy { action_id } = accepted.effects.first().unwrap() else {
        panic!("policy effect")
    };
    let dispatch = DispatchId::new(format!("dispatch-{revision}"));
    apply(
        runtime,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new(format!("cap-{revision}")),
            })),
            dispatch_id: Some(dispatch.clone()),
        },
    );
    apply(
        runtime,
        Command::RecordActionOutcome {
            action_id: action_id.clone(),
            outcome: Box::new(ActionOutcome {
                code: bip_types::ActionResultCode::Verified,
                dispatch_id: Some(dispatch),
                observed_at: MonotonicMillis(1),
                observation,
                discovered_source: None,
            }),
        },
    );
}

pub(crate) fn install_page(
    runtime: &mut ProfileServiceRuntime,
) -> (PageObservationEvidence, ObservedNodeHandle) {
    install_page_for(
        runtime,
        &source(),
        "epoch-1",
        ObservationCompleteness::Complete,
    )
}

pub(crate) fn install_page_for(
    runtime: &mut ProfileServiceRuntime,
    source: &ConsentedSource,
    epoch: &str,
    completeness: ObservationCompleteness,
) -> (PageObservationEvidence, ObservedNodeHandle) {
    let mut arena = loop_kernel::context::PageArena::new();
    for (id, role, name) in [
        ("root", SemanticRole::Document, "Page"),
        ("download", SemanticRole::Link, "Download"),
    ] {
        assert!(arena.push(loop_kernel::context::ArenaNode {
            node_id: id.to_owned(),
            role,
            sensitivity: Sensitivity::NotSensitive,
            name: Some(name.to_owned()),
            name_withheld: false,
            actions: Vec::new(),
            states: vec![NodeState::Visible, NodeState::Enabled],
            destination: loop_kernel::context::arena::DestinationClass::default(),
            content_trust: ContentTrust::FirstPartyDocument,
            content_signals: Vec::new(),
            text: Vec::new(),
            text_withheld: false,
            declared_text_runs: 0,
            declared_text_bytes: 0,
            container: None,
        }));
    }
    let evidence = PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: source.tab_id.clone(),
        frame_id: FrameId::new("frame-1"),
        page_epoch: PageEpoch::new(epoch),
        graph_revision: 1,
        normalized_origin: source.normalized_origin.clone(),
        private_profile: false,
        completeness,
        graph: ObservationGraphSummary {
            node_count: 2,
            relationship_count: 0,
            named_node_count: 2,
            text_run_count: 0,
            text_byte_count: 0,
        },
        total_bytes: 128,
        truncated: completeness == ObservationCompleteness::Incomplete,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    };
    let target = ObservedNodeHandle::from_node_handle(
        &loop_kernel::context::PageIdentity::from_evidence(&evidence)
            .unwrap()
            .node_handle("download"),
    )
    .unwrap();
    runtime
        .core_mut()
        .loop_state_mut(task_id().as_str())
        .page
        .replace_source(source.source_id, &evidence, arena)
        .unwrap();
    (evidence, target)
}

pub(super) fn perform_flow(runtime: &mut ProfileServiceRuntime) {
    action(
        runtime,
        BrowserIntent::Navigate {
            tab: source().tab_id,
            address: "https://example.test/documents".to_owned(),
            new_tab: false,
        },
        None,
    );
    let (evidence, target) = install_page(runtime);
    action(
        runtime,
        BrowserIntent::DomRead {
            tab: source().tab_id,
            target: None,
        },
        Some(evidence),
    );
    action(
        runtime,
        BrowserIntent::DownloadFromLink {
            target,
            browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
        },
        None,
    );
}
