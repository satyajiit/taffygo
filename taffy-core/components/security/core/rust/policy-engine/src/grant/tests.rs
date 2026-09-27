// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One complete observation request and one bounded discovery request, shared.

mod authority;
mod discovery;
mod operation_shape;

use super::{
    ActorLeaseFact, AuthoritySubject, GrantIdempotencyKey, GrantRequest, PolicyEvaluationContext,
    TaskDiscoveryAuthorityFact,
};
use crate::action_class::ActionClass;
use crate::action_operation::ActionOperationKind;
use crate::capability::CapabilityScope;
use crate::lease::{ActorLeaseId, ControlMode};
use crate::origin::{normalize_serialization, AllowedRedirects, NormalizedOrigin};
use crate::risk::RiskClass;
use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ActionId, ContentDigest, DigestAlgorithm, FrameId, GraphRevision, MonotonicMillis, PageEpoch,
    ProfileId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;

pub(super) fn request() -> GrantRequest {
    let origin = normalize_serialization("https://example.test").unwrap_or_else(|_| unreachable!());
    GrantRequest {
        context: PolicyEvaluationContext::Task,
        authority_subject: AuthoritySubject::Task(TaskId::new("task-a")),
        action_id: ActionId::new("action-a"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: ActionClass::ObservePage,
        operation_kind: ActionOperationKind::DomRead,
        canonical_intent_digest: [7; 32],
        proposal_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "ab".repeat(32),
        },
        scope: CapabilityScope {
            profile_id: ProfileId::new("profile-a"),
            tab_id: TabId::new("tab-a"),
            frame_id: FrameId::new("frame-a"),
            page_epoch: PageEpoch::new("epoch-a"),
            origin,
            node_id: None,
            destination_scope: None,
            destination_address: None,
            required_graph_revision: GraphRevision(4),
            allowed_redirects: AllowedRedirects::none(),
        },
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        service_generation: 7,
        idempotency_key: GrantIdempotencyKey::new("observe-once")
            .unwrap_or_else(|_| unreachable!()),
        expires_at: MonotonicMillis(200),
        now_utc_ms: 1_000,
        actor_lease: ActorLeaseFact {
            lease_id: ActorLeaseId::new("lease-browser"),
            service_generation: 7,
            authority_subject: AuthoritySubject::Task(TaskId::new("task-a")),
            profile_id: ProfileId::new("profile-a"),
            tab_id: TabId::new("tab-a"),
            control_mode: ControlMode::Assistant,
            expires_at: MonotonicMillis(300),
        },
        approval: None,
        discovery: None,
    }
}

pub(super) fn discovery_request() -> GrantRequest {
    let mut request = request();
    request.context = PolicyEvaluationContext::TaskDiscovery;
    request.action_class = ActionClass::OpenLink;
    request.operation_kind = ActionOperationKind::Search;
    request.context_risk = RiskClass::ReversibleDisclosure;
    request.scope.tab_id = TabId::new("discovery-tab");
    request.scope.origin = NormalizedOrigin::Opaque {
        opaque_id: "opaque-discovery-document".to_owned(),
    };
    request.scope.destination_scope =
        Some(normalize_serialization("https://search.example").unwrap_or_else(|_| unreachable!()));
    request.scope.destination_address = Some("https://search.example/?q=bounded".to_owned());
    request.scope.required_graph_revision = GraphRevision(0);
    request.actor_lease.tab_id = request.scope.tab_id.clone();
    request.discovery = Some(TaskDiscoveryAuthorityFact {
        discovery_tab_id: request.scope.tab_id.clone(),
        browser_session_id: "browser-session".to_owned(),
        remaining_new_source_cap: 4,
    });
    request
}
