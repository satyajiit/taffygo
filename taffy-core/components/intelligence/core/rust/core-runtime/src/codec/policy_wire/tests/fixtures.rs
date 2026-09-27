// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::rc::Rc;

use bip_types::identity::TabId;
use core_service_types as wire;
use task_engine::{BrowserSessionId, Command, CommandEnvelope, IdempotencyKey, TraceId};

use crate::account::crypto::ReferenceSha256;
use crate::contract::{Completion, Deadline, EffectRequest, OperationId, ServiceGeneration};
use crate::runtime::{BeginSubmit, CommitOutcome, OpenTaskCommitOutcome};
use crate::TaskId;
use crate::{create_profile_service_runtime, decode_start_task, ProfileRuntimeConfiguration};

fn task_subject() -> wire::AuthoritySubject {
    wire::AuthoritySubject {
        kind: wire::AuthoritySubjectKind::Task,
        authority_subject_id: "task-1".to_owned(),
    }
}

pub(super) fn request() -> wire::PolicyEvaluationRequest {
    wire::PolicyEvaluationRequest {
        operation: wire::OperationEnvelope {
            operation_id: "policy-1".to_owned(),
            service_generation: 1,
            task_revision: 0,
            deadline_monotonic_ms: 100,
            idempotency_key: "observe-once".to_owned(),
        },
        now_monotonic_ms: 10,
        now_utc_ms: 1_000,
        action_id: "action-1".to_owned(),
        task_id: "task-1".to_owned(),
        principal: wire::PolicyPrincipal {
            kind: wire::PolicyPrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: wire::PolicyActionClass::ObservePage,
        proposal_digest: "ab".repeat(32),
        scope: wire::PolicyCapabilityScope {
            profile_id: "profile-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            frame_id: "frame-1".to_owned(),
            page_epoch: "epoch-1".to_owned(),
            origin: wire::PolicyOrigin {
                kind: wire::PolicyOriginKind::Tuple,
                serialization: Some("https://example.test".to_owned()),
                opaque_id: None,
            },
            node_id: None,
            destination_scope: None,
            required_graph_revision: 1,
            allowed_redirects: Vec::new(),
            destination_address: None,
        },
        data_classes: Vec::new(),
        context_risk: wire::PolicyRiskClass::LocalRead,
        expires_at_monotonic_ms: 80,
        actor_lease: wire::ActorLeaseFact {
            lease_id: "lease-1".to_owned(),
            service_generation: 1,
            task_id: "task-1".to_owned(),
            profile_id: "profile-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            control_mode: wire::TaskControlMode::Assistant,
            expires_at_monotonic_ms: 90,
            authority_subject: task_subject(),
        },
        approval: None,
        context: wire::PolicyEvaluationContext::Task,
        authority_subject: task_subject(),
        policy_version: 1,
        operation_kind: wire::TaskActionOperationKind::DomRead,
        canonical_intent_digest: [9; 32],
        discovery: None,
    }
}

pub(super) fn runtime() -> crate::composition::profile::ProfileServiceRuntime {
    create_profile_service_runtime(
        ProfileRuntimeConfiguration {
            generation: ServiceGeneration::INITIAL,
            generation_capability_entropy: core::array::from_fn(|index| {
                u8::try_from(index).unwrap_or_default()
            }),
            initial_utc_millis: 0,
            private_profile: false,
            browser_profile_id: "profile-1".to_owned(),
            browser_session_id: "browser-session-1".to_owned(),
            available_account_methods: Vec::new(),
            skills: Vec::new(),
            recall: Vec::new(),
            assistant_configuration: None,
        },
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|error| panic!("profile runtime must build: {error:?}"))
}

pub(super) fn open_task(runtime: &mut crate::composition::profile::ProfileServiceRuntime) {
    let operation = wire::OperationEnvelope {
        operation_id: "open-task".to_owned(),
        service_generation: 1,
        task_revision: 0,
        deadline_monotonic_ms: 100,
        idempotency_key: "create-task".to_owned(),
    };
    let start = wire::StartTaskCommand {
        task_id: "task-1".to_owned(),
        workspace_id: None,
        browser_profile_id: "profile-1".to_owned(),
        browser_session_id: "browser-session-1".to_owned(),
        kind: wire::TaskKind::Research,
        goal: "research".to_owned(),
        control_mode: wire::TaskControlMode::Assistant,
        provider_route_id: Some("no_model_required".to_owned()),
        assistant_config_version: 1,
        policy_version: 1,
        skill_version_id: None,
        builtin_skill: None,
        tool_allowlist: vec!["browser.dom.read".to_owned()],
        milestone: wire::TaskMilestone::M7,
        budgets: vec![
            wire::TaskBudget {
                kind: wire::TaskBudgetKind::MaxSources,
                limit: 1,
            },
            wire::TaskBudget {
                kind: wire::TaskBudgetKind::MaxModelRequests,
                limit: 0,
            },
            wire::TaskBudget {
                kind: wire::TaskBudgetKind::MaxInputUnits,
                limit: 0,
            },
            wire::TaskBudget {
                kind: wire::TaskBudgetKind::MaxOutputUnits,
                limit: 0,
            },
            wire::TaskBudget {
                kind: wire::TaskBudgetKind::MaxCostUnits,
                limit: 0,
            },
            wire::TaskBudget {
                kind: wire::TaskBudgetKind::MaxRetriesPerStep,
                limit: crate::codec::start_shape::NO_MODEL_MAX_RETRIES_PER_STEP,
            },
        ],
        has_task_deadline: false,
        task_deadline_monotonic_ms: 0,
        task_deadline_utc_ms: 0,
        predecessor_task_id: None,
        trace_id: "trace-1".to_owned(),
        task_id_seed: core::array::from_fn(|index| {
            u8::try_from(index.saturating_add(1)).unwrap_or_default()
        }),
        template_id: wire::TaskTemplateId::BuildSourceTable,
        consent_preview: wire::TaskConsentPreview {
            sources: vec![wire::TaskConsentSource {
                source_id: "00112233445566778899aabbccddeeff".to_owned(),
                tab_id: "tab-1".to_owned(),
                normalized_origin: "https://example.test".to_owned(),
                canonical_locator: None,
            }],
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route: wire::TaskProviderRoute::NoModelRequired,
        },
        initial_consent_receipt_id: "initial-consent-1".to_owned(),
        library_refresh: None,
    };
    commit_open_task(runtime, operation, &start);
}

fn commit_open_task(
    runtime: &mut crate::composition::profile::ProfileServiceRuntime,
    operation: wire::OperationEnvelope,
    start: &wire::StartTaskCommand,
) {
    let load = decode_start_task(start, &operation).unwrap_or_else(|_| unreachable!());
    let begin = runtime.core_mut().begin_open_task(
        load,
        OperationId::new(operation.operation_id).unwrap_or_else(|_| unreachable!()),
        Deadline::from_millis(operation.deadline_monotonic_ms),
        1,
        1,
    );
    let commit = begin
        .expect("the complete browser facts fixture must open its task")
        .commit;
    let revision = match &commit.body {
        EffectRequest::Storage(effect) => effect.resulting_revision(),
        _ => unreachable!(),
    };
    let completion = commit.with_body(Completion::StorageCommitted {
        committed_revision: revision,
    });
    assert!(matches!(
        runtime
            .core_mut()
            .complete_open_task(&TaskId::new("task-1"), completion, 2),
        Ok(OpenTaskCommitOutcome::Opened(_))
    ));
}

/// The six budgets the errand shape states, as the command factory states them.
fn errand_budgets(max_sources: u64) -> Vec<wire::TaskBudget> {
    use crate::codec::start_shape::{ERRAND_MAX_MODEL_REQUESTS, MAX_RETRIES_PER_STEP};
    [
        (wire::TaskBudgetKind::MaxSources, max_sources),
        (
            wire::TaskBudgetKind::MaxModelRequests,
            ERRAND_MAX_MODEL_REQUESTS,
        ),
        (wire::TaskBudgetKind::MaxInputUnits, 0),
        (wire::TaskBudgetKind::MaxOutputUnits, 0),
        (wire::TaskBudgetKind::MaxCostUnits, 0),
        (
            wire::TaskBudgetKind::MaxRetriesPerStep,
            MAX_RETRIES_PER_STEP,
        ),
    ]
    .map(|(kind, limit)| wire::TaskBudget { kind, limit })
    .to_vec()
}

pub(super) fn open_discovery_task(
    runtime: &mut crate::composition::profile::ProfileServiceRuntime,
) {
    let operation = wire::OperationEnvelope {
        operation_id: "open-discovery-task".to_owned(),
        service_generation: 1,
        task_revision: 0,
        deadline_monotonic_ms: 100,
        idempotency_key: "create-discovery-task".to_owned(),
    };
    let start = wire::StartTaskCommand {
        task_id: "task-1".to_owned(),
        workspace_id: None,
        browser_profile_id: "profile-1".to_owned(),
        browser_session_id: "browser-session-1".to_owned(),
        kind: wire::TaskKind::Research,
        goal: "find and complete the errand".to_owned(),
        control_mode: wire::TaskControlMode::Assistant,
        provider_route_id: Some("direct_user_key".to_owned()),
        assistant_config_version: 1,
        policy_version: 1,
        skill_version_id: None,
        builtin_skill: None,
        tool_allowlist: vec!["browser.dom.read".to_owned(), "browser.search".to_owned()],
        milestone: wire::TaskMilestone::M7,
        budgets: errand_budgets(4),
        has_task_deadline: false,
        task_deadline_monotonic_ms: 0,
        task_deadline_utc_ms: 0,
        predecessor_task_id: None,
        trace_id: "trace-discovery".to_owned(),
        task_id_seed: core::array::from_fn(|index| {
            u8::try_from(index.saturating_add(1)).unwrap_or_default()
        }),
        template_id: wire::TaskTemplateId::WebErrand,
        consent_preview: wire::TaskConsentPreview {
            sources: Vec::new(),
            source_discovery_enabled: true,
            new_source_cap: 4,
            provider_route: wire::TaskProviderRoute::DirectUserKey,
        },
        initial_consent_receipt_id: "initial-consent-discovery".to_owned(),
        library_refresh: None,
    };
    commit_open_task(runtime, operation, &start);

    let task_id = TaskId::new("task-1");
    let task_revision = runtime.core().task(&task_id).map_or_else(
        || unreachable!(),
        loop_kernel::ports::TaskEnginePort::revision,
    );
    let record = CommandEnvelope::new(
        IdempotencyKey::new("record-discovery-tab"),
        task_revision,
        TraceId::new("trace-record-discovery-tab"),
        Command::RecordDiscoveryTab {
            discovery_tab_id: TabId::new("discovery-tab-1"),
            browser_session_id: BrowserSessionId::new("browser-session-1")
                .unwrap_or_else(|_| unreachable!()),
        },
    );
    let commit = match runtime.core_mut().begin_submit(
        &task_id,
        record,
        OperationId::new("record-discovery-tab").unwrap_or_else(|_| unreachable!()),
        Deadline::from_millis(100),
        3,
        3,
    ) {
        Ok(BeginSubmit::AwaitingCommit(effect)) => *effect,
        other => unreachable!("the discovery tab transition must stage: {other:?}"),
    };
    let revision = match &commit.body {
        EffectRequest::Storage(effect) => effect.resulting_revision(),
        _ => unreachable!(),
    };
    let completion = commit.with_body(Completion::StorageCommitted {
        committed_revision: revision,
    });
    assert!(matches!(
        runtime.core_mut().complete_commit(&task_id, completion, 4),
        Ok(CommitOutcome::Committed(_))
    ));
}

pub(super) fn discovery_request(
    runtime: &crate::composition::profile::ProfileServiceRuntime,
) -> wire::PolicyEvaluationRequest {
    let mut request = request();
    request.operation.task_revision = runtime.core().task(&TaskId::new("task-1")).map_or_else(
        || unreachable!(),
        loop_kernel::ports::TaskEnginePort::revision,
    );
    request.context = wire::PolicyEvaluationContext::TaskDiscovery;
    request.action_class = wire::PolicyActionClass::OpenLink;
    request.operation_kind = wire::TaskActionOperationKind::Search;
    request.context_risk = wire::PolicyRiskClass::ReversibleDisclosure;
    request.data_classes = vec![wire::BipSensitivity::NotSensitive];
    request.scope.tab_id = "discovery-tab-1".to_owned();
    request.scope.origin = wire::PolicyOrigin {
        kind: wire::PolicyOriginKind::Opaque,
        serialization: None,
        opaque_id: Some("opaque-discovery-document".to_owned()),
    };
    request.scope.destination_scope = Some(wire::PolicyOrigin {
        kind: wire::PolicyOriginKind::Tuple,
        serialization: Some("https://search.example".to_owned()),
        opaque_id: None,
    });
    request.scope.destination_address = Some("https://search.example/?q=bounded".to_owned());
    request.scope.required_graph_revision = 0;
    request.actor_lease.tab_id = request.scope.tab_id.clone();
    request.discovery = Some(wire::TaskDiscoveryAuthorityFact {
        discovery_tab_id: request.scope.tab_id.clone(),
        browser_session_id: "browser-session-1".to_owned(),
        remaining_new_source_cap: 4,
    });
    request
}
