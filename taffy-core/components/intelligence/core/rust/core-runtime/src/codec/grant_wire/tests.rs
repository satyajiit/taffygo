// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ActionId, ContentDigest, DigestAlgorithm, FrameId, GraphRevision, MonotonicMillis, PageEpoch,
    ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::{Sensitivity, SensitivitySet};
use policy_engine::origin::normalize_serialization;
use policy_engine::{
    ActionClass, ActionOperationKind, ActorLeaseId, AllowedRedirects, AuthoritySubject,
    CapabilityId, CapabilityScope, DirectUserIntentId, GrantIdempotencyKey, MintedGrant,
    PolicyVersion, RiskClass,
};

use super::{GrantWireError, MintedGrantWire};

fn grant() -> MintedGrant {
    MintedGrant {
        capability_id: CapabilityId::new("capability-1"),
        service_generation: 7,
        policy_version: PolicyVersion(12),
        actor_lease_id: ActorLeaseId::new("lease-1"),
        authority_subject: AuthoritySubject::Task(TaskId::new("task-1")),
        action_id: ActionId::new("action-1"),
        action_class: ActionClass::ObservePage,
        operation_kind: ActionOperationKind::DomRead,
        canonical_intent_digest: [7; 32],
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        proposal_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "ab".repeat(32),
        },
        idempotency_key: GrantIdempotencyKey::new("effect-1").unwrap_or_else(|_| unreachable!()),
        scope: CapabilityScope {
            profile_id: ProfileId::new("profile-1"),
            tab_id: TabId::new("tab-1"),
            frame_id: FrameId::new("frame-1"),
            page_epoch: PageEpoch::new("epoch-1"),
            origin: normalize_serialization("https://example.test")
                .unwrap_or_else(|_| unreachable!()),
            node_id: None,
            destination_scope: None,
            destination_address: None,
            required_graph_revision: GraphRevision(2),
            allowed_redirects: AllowedRedirects::none(),
        },
        data_classes: SensitivitySet::of(Sensitivity::Personal),
        effective_risk: RiskClass::LocalRead,
        approval: None,
        discovery: None,
        issued_at: MonotonicMillis(100),
        expires_at: MonotonicMillis(200),
    }
}

#[test]
fn generated_grant_keeps_every_authority_binding() {
    let source = grant();
    let projected = source.to_wire().unwrap_or_else(|_| unreachable!());
    assert_eq!(projected.task_id, "task-1");
    assert_eq!(
        projected.authority_subject.kind,
        core_service_types::AuthoritySubjectKind::Task
    );
    assert_eq!(projected.authority_subject.authority_subject_id, "task-1");
    assert_eq!(projected.action_id, source.action_id.0);
    assert_eq!(
        projected.principal.kind,
        core_service_types::PolicyPrincipalKind::Assistant
    );
    assert_eq!(
        projected.action_class,
        core_service_types::PolicyActionClass::ObservePage
    );
    assert_eq!(projected.idempotency_key, source.idempotency_key.as_str());
    assert_eq!(projected.proposal_digest, source.proposal_digest.value);
    assert_eq!(projected.scope.tab_id, source.scope.tab_id.0);
    assert_eq!(projected.scope.destination_address, None);
    assert_eq!(
        projected.operation_kind,
        core_service_types::TaskActionOperationKind::DomRead
    );
    assert_eq!(
        projected.canonical_intent_digest,
        source.canonical_intent_digest
    );
}

#[test]
fn navigation_keeps_the_exact_destination_address() {
    let mut source = grant();
    source.action_class = ActionClass::OpenLink;
    source.operation_kind = ActionOperationKind::Navigate;
    source.scope.destination_address =
        Some("https://example.test/path?query=exact#fragment".to_owned());

    let projected = source.to_wire().unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.operation_kind,
        core_service_types::TaskActionOperationKind::Navigate
    );
    assert_eq!(
        projected.scope.destination_address.as_deref(),
        Some("https://example.test/path?query=exact#fragment")
    );
}

#[test]
fn destination_presence_must_match_the_operation() {
    let mut missing = grant();
    missing.action_class = ActionClass::OpenLink;
    missing.operation_kind = ActionOperationKind::Navigate;
    assert_eq!(missing.to_wire(), Err(GrantWireError::InvalidScope));

    let mut invented = grant();
    invented.scope.destination_address = Some("https://example.test/path".to_owned());
    assert_eq!(invented.to_wire(), Err(GrantWireError::InvalidScope));

    let mut mismatched = grant();
    mismatched.operation_kind = ActionOperationKind::FormSubmit;
    assert_eq!(mismatched.to_wire(), Err(GrantWireError::InvalidOperation));
}

#[test]
fn exact_observation_node_shapes_survive_only_when_well_formed() {
    let mut form = grant();
    form.operation_kind = ActionOperationKind::FormInspect;
    form.scope.node_id = Some(SemanticNodeId::new("form-1"));
    assert!(form.to_wire().is_ok());

    let mut missing_form = form.clone();
    missing_form.scope.node_id = None;
    assert_eq!(missing_form.to_wire(), Err(GrantWireError::InvalidScope));

    let mut selection = grant();
    selection.operation_kind = ActionOperationKind::SelectionRead;
    assert!(selection.to_wire().is_ok());

    selection.scope.node_id = Some(SemanticNodeId::new("invented-selection"));
    assert_eq!(selection.to_wire(), Err(GrantWireError::InvalidScope));
}

#[test]
fn direct_user_intent_never_enters_the_legacy_task_slot() {
    let mut source = grant();
    source.authority_subject = AuthoritySubject::DirectUserIntent(
        DirectUserIntentId::new("direct-intent-selected-page-1").unwrap_or_else(|_| unreachable!()),
    );
    source.action_id = ActionId::new(String::new());
    source.data_classes = SensitivitySet::of(Sensitivity::NotSensitive);

    let projected = source.to_wire().unwrap_or_else(|_| unreachable!());
    assert!(projected.task_id.is_empty());
    assert!(projected.action_id.is_empty());
    assert_eq!(
        projected.authority_subject.kind,
        core_service_types::AuthoritySubjectKind::DirectUserIntent
    );
    assert_eq!(
        projected.authority_subject.authority_subject_id,
        "direct-intent-selected-page-1"
    );
    assert_eq!(
        projected.data_classes,
        vec![core_service_types::BipSensitivity::NotSensitive]
    );
}

#[test]
fn sensitivity_projection_keeps_explicit_bottom_and_canonical_members() {
    let mut source = grant();
    source.data_classes = SensitivitySet::EMPTY;
    let bottom = source.to_wire().unwrap_or_else(|_| unreachable!());
    assert_eq!(
        bottom.data_classes,
        vec![core_service_types::BipSensitivity::NotSensitive]
    );

    source.data_classes = SensitivitySet::of(Sensitivity::Personal).with(Sensitivity::Health);
    let sensitive = source.to_wire().unwrap_or_else(|_| unreachable!());
    assert_eq!(
        sensitive.data_classes,
        vec![
            core_service_types::BipSensitivity::Personal,
            core_service_types::BipSensitivity::Health,
        ]
    );
}

#[test]
fn direct_namespace_in_a_task_subject_is_rejected() {
    let mut source = grant();
    source.authority_subject = AuthoritySubject::Task(TaskId::new("direct-intent-forged"));
    assert_eq!(source.to_wire(), Err(GrantWireError::InvalidIdentity));
}

#[test]
fn malformed_principal_binding_never_reaches_the_browser_ledger() {
    let mut source = grant();
    source.principal.kind = PrincipalKind::Skill;
    assert_eq!(source.to_wire(), Err(GrantWireError::InvalidPrincipal));
}
