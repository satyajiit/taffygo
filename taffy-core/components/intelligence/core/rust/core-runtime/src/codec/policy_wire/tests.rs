// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_service_types as wire;

use super::evaluate_policy_request;
use crate::TaskId;

use self::fixtures::{discovery_request, open_discovery_task, open_task, request, runtime};

mod fixtures;

#[test]
fn an_unknown_task_is_an_invalid_request_and_never_a_grant() {
    let mut runtime = runtime();
    let result = evaluate_policy_request(runtime.core_mut(), &request());
    assert_eq!(result.operation_id, "policy-1");
    assert_eq!(result.status, wire::PolicyEvaluationStatus::InvalidRequest);
    assert!(result.minted_grant.is_none());
    assert!(result.direct_observation_effect.is_none());
    assert!(result.has_valid_presence());
}

#[test]
fn an_unbounded_operation_identity_is_not_reflected() {
    let mut runtime = runtime();
    let mut request = request();
    request.operation.operation_id = "x".repeat(wire::MAX_OPERATION_ID_BYTES + 1);
    let result = evaluate_policy_request(runtime.core_mut(), &request);
    assert!(result.operation_id.is_empty());
    assert_eq!(result.status, wire::PolicyEvaluationStatus::InvalidRequest);
    assert!(result.minted_grant.is_none());
}

#[test]
fn complete_browser_facts_reach_the_real_policy_and_mint_one_bound_grant() {
    let mut runtime = runtime();
    open_task(&mut runtime);
    let mut request = request();
    let Some(task) = runtime.core().task(&TaskId::new("task-1")) else {
        unreachable!();
    };
    request.operation.task_revision = task.revision();
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::Granted);
    assert!(result.minted_grant.as_ref().is_some_and(|grant| {
        grant.task_id == request.task_id
            && grant.action_id == request.action_id
            && grant.actor_lease_id == request.actor_lease.lease_id
            && grant.idempotency_key == request.operation.idempotency_key
            && grant.authority_subject == request.authority_subject
            && grant.operation_kind == request.operation_kind
            && grant.canonical_intent_digest == request.canonical_intent_digest
    }));
    assert!(result.direct_observation_effect.is_none());
    assert!(result.has_valid_presence());
}

#[test]
fn exact_navigation_operation_digest_and_address_survive_policy_evaluation() {
    let mut runtime = runtime();
    open_task(&mut runtime);
    let mut request = request();
    let Some(task) = runtime.core().task(&TaskId::new("task-1")) else {
        unreachable!();
    };
    request.operation.task_revision = task.revision();
    request.action_class = wire::PolicyActionClass::OpenLink;
    request.operation_kind = wire::TaskActionOperationKind::Navigate;
    request.canonical_intent_digest = [0xa5; 32];
    request.scope.destination_scope = Some(request.scope.origin.clone());
    request.scope.destination_address =
        Some("https://example.test/orders?token=a%2Fb#receipt".to_owned());

    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::Granted);
    let grant = result.minted_grant.unwrap_or_else(|| unreachable!());
    assert_eq!(grant.operation_kind, request.operation_kind);
    assert_eq!(
        grant.canonical_intent_digest,
        request.canonical_intent_digest
    );
    assert_eq!(
        grant.scope.destination_address,
        request.scope.destination_address
    );
}

#[test]
fn an_operation_cannot_borrow_a_less_consequential_action_class() {
    let mut runtime = runtime();
    open_task(&mut runtime);
    let mut request = request();
    let Some(task) = runtime.core().task(&TaskId::new("task-1")) else {
        unreachable!();
    };
    request.operation.task_revision = task.revision();
    request.operation_kind = wire::TaskActionOperationKind::FormSubmit;

    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::Denied);
    assert!(result.minted_grant.is_none());
}

fn direct_request() -> wire::PolicyEvaluationRequest {
    let mut request = request();
    let subject = wire::AuthoritySubject {
        kind: wire::AuthoritySubjectKind::DirectUserIntent,
        authority_subject_id: "direct-intent-selected-page-1".to_owned(),
    };
    request.context = wire::PolicyEvaluationContext::DirectUserObservation;
    request.authority_subject = subject.clone();
    request.actor_lease.authority_subject = subject;
    request.task_id.clear();
    request.actor_lease.task_id.clear();
    request.action_id.clear();
    request.operation.task_revision = 0;
    request.scope.required_graph_revision = 0;
    request.data_classes = vec![wire::BipSensitivity::NotSensitive];
    request.actor_lease.control_mode = wire::TaskControlMode::User;
    request
}

#[test]
fn direct_user_observation_is_taskless_and_emits_one_typed_effect() {
    let mut runtime = runtime();
    let request = direct_request();
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::Granted);
    let grant = result.minted_grant.unwrap_or_else(|| unreachable!());
    assert!(grant.task_id.is_empty());
    assert!(grant.action_id.is_empty());
    assert_eq!(grant.authority_subject, request.authority_subject);
    assert_eq!(grant.data_classes, request.data_classes);
    let effect = result
        .direct_observation_effect
        .unwrap_or_else(|| unreachable!());
    assert_eq!(effect.kind, wire::EffectKind::PageObservation);
    assert_eq!(effect.operation, request.operation);
    let body = effect.page_observation.unwrap_or_else(|| unreachable!());
    assert!(body.task_id.is_empty());
    assert!(body.action_id.is_empty());
    assert_eq!(body.authority_subject, request.authority_subject);
    assert_eq!(body.max_nodes as usize, wire::MAX_DIRECT_OBSERVATION_NODES);
    assert_eq!(
        body.max_text_bytes as usize,
        wire::MAX_DIRECT_OBSERVATION_TEXT_BYTES
    );
    assert_eq!(
        body.max_bytes as usize,
        wire::MAX_DIRECT_OBSERVATION_TOTAL_BYTES
    );
    assert_eq!(
        body.max_frames as usize,
        wire::MAX_DIRECT_OBSERVATION_FRAMES
    );
    assert_eq!(
        body.deadline_ms as usize,
        wire::MAX_DIRECT_OBSERVATION_DEADLINE_MS
    );
}

#[test]
fn a_direct_identifier_in_the_task_subject_is_rejected() {
    let mut runtime = runtime();
    let mut request = request();
    request.authority_subject.authority_subject_id = "direct-intent-forged-task".to_owned();
    request.actor_lease.authority_subject = request.authority_subject.clone();
    request.task_id = request.authority_subject.authority_subject_id.clone();
    request.actor_lease.task_id = request.task_id.clone();
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::InvalidRequest);
    assert!(result.minted_grant.is_none());
}

#[test]
fn a_task_identifier_in_the_direct_subject_is_rejected() {
    let mut runtime = runtime();
    let mut request = direct_request();
    request.authority_subject.authority_subject_id = "task-1".to_owned();
    request.actor_lease.authority_subject = request.authority_subject.clone();
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::InvalidRequest);
    assert!(result.minted_grant.is_none());
    assert!(result.direct_observation_effect.is_none());
}

#[test]
fn the_durable_zero_source_fact_mints_one_exact_discovery_grant() {
    let mut runtime = runtime();
    open_discovery_task(&mut runtime);
    let request = discovery_request(&runtime);
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::Granted);
    let grant = result.minted_grant.unwrap_or_else(|| unreachable!());
    assert_eq!(grant.discovery, request.discovery);
    assert_eq!(grant.scope.tab_id, "discovery-tab-1");
    assert_eq!(grant.scope.origin, request.scope.origin);
    assert_eq!(
        grant.scope.destination_scope,
        request.scope.destination_scope
    );
    assert_eq!(
        grant.scope.destination_address,
        request.scope.destination_address
    );
    assert_eq!(grant.operation_kind, wire::TaskActionOperationKind::Search);
    assert!(result.direct_observation_effect.is_none());
}

/// The second move the fact admits: a typed navigate to an https address
/// (decision 0106 section 2). The core reads the same durable fact for it and
/// mints the grant with the address's own origin as the destination.
#[test]
fn a_typed_navigate_from_the_discovery_tab_mints_under_the_same_fact() {
    let mut runtime = runtime();
    open_discovery_task(&mut runtime);
    let mut request = discovery_request(&runtime);
    request.operation_kind = wire::TaskActionOperationKind::Navigate;
    request.scope.destination_scope = Some(wire::PolicyOrigin {
        kind: wire::PolicyOriginKind::Tuple,
        serialization: Some("https://destination.example".to_owned()),
        opaque_id: None,
    });
    request.scope.destination_address = Some("https://destination.example/start".to_owned());
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::Granted);
    let grant = result
        .minted_grant
        .unwrap_or_else(|| unreachable!("the bounded discovery navigate must mint"));
    assert_eq!(grant.discovery, request.discovery);
    assert_eq!(
        grant.operation_kind,
        wire::TaskActionOperationKind::Navigate
    );
    assert_eq!(
        grant.scope.destination_scope,
        request.scope.destination_scope
    );

    // The address a model types is an origin and nothing else - "go to
    // myaadhaar.uidai.gov.in", not a path on it. Every vector above this line
    // carries a path or a trailing slash, which is why a phone kept recording
    // Deny(Unsupported) for a navigate this suite said was granted.
    let mut bare_origin = discovery_request(&runtime);
    bare_origin.operation_kind = wire::TaskActionOperationKind::Navigate;
    bare_origin.scope.destination_scope = Some(wire::PolicyOrigin {
        kind: wire::PolicyOriginKind::Tuple,
        serialization: Some("https://destination.example".to_owned()),
        opaque_id: None,
    });
    bare_origin.scope.destination_address = Some("https://destination.example".to_owned());
    let bare = runtime.evaluate_policy(&bare_origin);
    assert_eq!(bare.status, wire::PolicyEvaluationStatus::Granted);

    let mut tab_open = discovery_request(&runtime);
    tab_open.operation_kind = wire::TaskActionOperationKind::TabsOpen;
    tab_open.action_class = wire::PolicyActionClass::CreateTaskTab;
    assert_eq!(
        runtime.evaluate_policy(&tab_open).status,
        wire::PolicyEvaluationStatus::InvalidRequest
    );
}

#[test]
fn discovery_wire_facts_must_equal_the_durable_session_tab_and_cap() {
    let mut runtime = runtime();
    open_discovery_task(&mut runtime);
    let assert_invalid = |runtime: &mut crate::composition::profile::ProfileServiceRuntime,
                          request: wire::PolicyEvaluationRequest| {
        let result = runtime.evaluate_policy(&request);
        assert_eq!(result.status, wire::PolicyEvaluationStatus::InvalidRequest);
        assert!(result.minted_grant.is_none());
        assert!(result.direct_observation_effect.is_none());
    };

    let mut wrong_tab = discovery_request(&runtime);
    wrong_tab
        .discovery
        .as_mut()
        .unwrap_or_else(|| unreachable!())
        .discovery_tab_id = "different-tab".to_owned();
    assert_invalid(&mut runtime, wrong_tab);

    let mut wrong_session = discovery_request(&runtime);
    wrong_session
        .discovery
        .as_mut()
        .unwrap_or_else(|| unreachable!())
        .browser_session_id = "different-session".to_owned();
    assert_invalid(&mut runtime, wrong_session);

    for cap in [0, 3, 5, 9] {
        let mut wrong_cap = discovery_request(&runtime);
        wrong_cap
            .discovery
            .as_mut()
            .unwrap_or_else(|| unreachable!())
            .remaining_new_source_cap = cap;
        assert_invalid(&mut runtime, wrong_cap);
    }

    let mut ordinary_task = discovery_request(&runtime);
    ordinary_task.context = wire::PolicyEvaluationContext::Task;
    assert_invalid(&mut runtime, ordinary_task);

    let mut tuple_current_origin = discovery_request(&runtime);
    tuple_current_origin.scope.origin = wire::PolicyOrigin {
        kind: wire::PolicyOriginKind::Tuple,
        serialization: Some("https://search.example".to_owned()),
        opaque_id: None,
    };
    assert_invalid(&mut runtime, tuple_current_origin);
}

#[test]
fn an_ordinary_selected_page_task_cannot_fabricate_discovery_entitlement() {
    let mut runtime = runtime();
    open_task(&mut runtime);
    let mut request = request();
    let task = runtime
        .core()
        .task(&TaskId::new("task-1"))
        .unwrap_or_else(|| unreachable!());
    request.operation.task_revision = task.revision();
    request.context = wire::PolicyEvaluationContext::TaskDiscovery;
    request.discovery = Some(wire::TaskDiscoveryAuthorityFact {
        discovery_tab_id: "tab-1".to_owned(),
        browser_session_id: "browser-session-1".to_owned(),
        remaining_new_source_cap: 1,
    });
    let result = runtime.evaluate_policy(&request);
    assert_eq!(result.status, wire::PolicyEvaluationStatus::InvalidRequest);
    assert!(result.minted_grant.is_none());
}
