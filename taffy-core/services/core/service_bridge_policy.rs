// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed projection for the browser-fact/Rust-policy round trip.

mod refusal;

use core_runtime::wire;

use crate::service_bridge_policy_ffi::ffi;
use crate::service_bridge_runtime::ServiceBridge;
use refusal::{
    empty_approval, empty_direct_observation_effect, empty_grant, empty_origin, invalid_result,
};

#[allow(non_snake_case)]
pub(crate) fn EvaluatePolicy(
    bridge: &mut ServiceBridge,
    request: ffi::BridgePolicyRequest,
) -> ffi::BridgePolicyResult {
    let operation_id = request.operation.operation_id.clone();
    let Some(runtime) = bridge.runtime.as_mut() else {
        return invalid_result(operation_id);
    };
    let Some(request) = request_to_wire(request) else {
        return invalid_result(operation_id);
    };
    // The label is read out before the runtime borrow ends, and recorded
    // after it, so the browser can name the clause that refused rather than
    // reading one word for fourteen of them.
    let (result, label) = runtime.evaluate_policy_labeled(&request);
    let projected = result_from_wire(result);
    if !label.is_empty() {
        crate::service_bridge_runtime::note_refusal(bridge, label);
    }
    projected
}

fn request_to_wire(value: ffi::BridgePolicyRequest) -> Option<wire::PolicyEvaluationRequest> {
    let mut data_classes = Vec::with_capacity(value.data_classes.len());
    for sensitivity in value.data_classes {
        data_classes.push(wire::BipSensitivity::from_wire(u32::from(sensitivity))?);
    }
    Some(wire::PolicyEvaluationRequest {
        operation: operation_to_wire(value.operation),
        now_monotonic_ms: value.now_monotonic_ms,
        now_utc_ms: value.now_utc_ms,
        action_id: value.action_id,
        task_id: value.task_id,
        principal: principal_to_wire(value.principal)?,
        action_class: wire::PolicyActionClass::from_wire(u32::from(value.action_class))?,
        operation_kind: wire::TaskActionOperationKind::from_wire(u32::from(value.operation_kind))?,
        canonical_intent_digest: value.canonical_intent_digest,
        proposal_digest: value.proposal_digest,
        scope: scope_to_wire(value.scope)?,
        data_classes,
        context_risk: wire::PolicyRiskClass::from_wire(u32::from(value.context_risk))?,
        expires_at_monotonic_ms: value.expires_at_monotonic_ms,
        actor_lease: wire::ActorLeaseFact {
            lease_id: value.actor_lease.lease_id,
            service_generation: value.actor_lease.service_generation,
            task_id: value.actor_lease.task_id,
            profile_id: value.actor_lease.profile_id,
            tab_id: value.actor_lease.tab_id,
            control_mode: wire::TaskControlMode::from_wire(u32::from(
                value.actor_lease.control_mode,
            ))?,
            expires_at_monotonic_ms: value.actor_lease.expires_at_monotonic_ms,
            authority_subject: authority_subject_to_wire(value.actor_lease.authority_subject)?,
        },
        approval: value.has_approval.then(|| approval_to_wire(value.approval)),
        context: wire::PolicyEvaluationContext::from_wire(u32::from(value.context))?,
        authority_subject: authority_subject_to_wire(value.authority_subject)?,
        policy_version: value.policy_version,
        discovery: value
            .has_discovery
            .then(|| discovery_to_wire(value.discovery)),
    })
}

fn discovery_to_wire(value: ffi::BridgeTaskDiscoveryAuthority) -> wire::TaskDiscoveryAuthorityFact {
    wire::TaskDiscoveryAuthorityFact {
        discovery_tab_id: value.discovery_tab_id,
        browser_session_id: value.browser_session_id,
        remaining_new_source_cap: value.remaining_new_source_cap,
    }
}

fn authority_subject_to_wire(value: ffi::BridgeAuthoritySubject) -> Option<wire::AuthoritySubject> {
    Some(wire::AuthoritySubject {
        kind: wire::AuthoritySubjectKind::from_wire(u32::from(value.kind))?,
        authority_subject_id: value.authority_subject_id,
    })
}

fn operation_to_wire(value: ffi::BridgePolicyOperation) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}

fn principal_to_wire(value: ffi::BridgePrincipal) -> Option<wire::PolicyPrincipal> {
    Some(wire::PolicyPrincipal {
        kind: wire::PolicyPrincipalKind::from_wire(u32::from(value.kind))?,
        skill_version_id: value.has_skill_version_id.then_some(value.skill_version_id),
    })
}

fn origin_to_wire(value: ffi::BridgeOrigin) -> Option<wire::PolicyOrigin> {
    Some(wire::PolicyOrigin {
        kind: wire::PolicyOriginKind::from_wire(u32::from(value.kind))?,
        serialization: value.has_serialization.then_some(value.serialization),
        opaque_id: value.has_opaque_id.then_some(value.opaque_id),
    })
}

fn scope_to_wire(value: ffi::BridgePolicyScope) -> Option<wire::PolicyCapabilityScope> {
    if value.has_destination_address != !value.destination_address.is_empty() {
        return None;
    }
    let mut redirects = Vec::with_capacity(value.allowed_redirects.len());
    for redirect in value.allowed_redirects {
        redirects.push(origin_to_wire(redirect)?);
    }
    Some(wire::PolicyCapabilityScope {
        profile_id: value.profile_id,
        tab_id: value.tab_id,
        frame_id: value.frame_id,
        page_epoch: value.page_epoch,
        origin: origin_to_wire(value.origin)?,
        node_id: value.has_node_id.then_some(value.node_id),
        destination_scope: if value.has_destination_scope {
            Some(origin_to_wire(value.destination_scope)?)
        } else {
            None
        },
        destination_address: value
            .has_destination_address
            .then_some(value.destination_address),
        required_graph_revision: value.required_graph_revision,
        allowed_redirects: redirects,
    })
}

fn approval_to_wire(value: ffi::BridgeApprovalFact) -> wire::PolicyApprovalFact {
    wire::PolicyApprovalFact {
        receipt_reference: value.receipt_reference,
        proposal_digest: value.proposal_digest,
        service_generation: value.service_generation,
        expires_at_monotonic_ms: value.expires_at_monotonic_ms,
        expires_at_utc_ms: value.expires_at_utc_ms,
        browser_session_id: value.browser_session_id,
    }
}

fn result_from_wire(value: wire::PolicyEvaluationResult) -> ffi::BridgePolicyResult {
    let operation_id = value.operation_id.clone();
    let has_minted_grant = value.minted_grant.is_some();
    let has_direct_observation_effect = value.direct_observation_effect.is_some();
    let direct_observation_effect = match value.direct_observation_effect {
        Some(effect) => {
            let Some(projected) = direct_observation_effect_from_wire(effect) else {
                return invalid_result(operation_id);
            };
            projected
        }
        None => empty_direct_observation_effect(),
    };
    ffi::BridgePolicyResult {
        operation_id: value.operation_id,
        status: value.status as u8,
        has_denial_code: value.denial.is_some(),
        denial_code: value.denial.map_or(0, |denial| denial.code as u8),
        has_minted_grant,
        minted_grant: value
            .minted_grant
            .map(grant_from_wire)
            .unwrap_or_else(empty_grant),
        has_direct_observation_effect,
        direct_observation_effect,
    }
}

fn grant_from_wire(value: wire::MintedCapabilityGrant) -> ffi::BridgeMintedGrant {
    let has_approval = value.approval.is_some();
    let has_discovery = value.discovery.is_some();
    ffi::BridgeMintedGrant {
        capability_id: value.capability_id,
        service_generation: value.service_generation,
        policy_version: value.policy_version,
        actor_lease_id: value.actor_lease_id,
        task_id: value.task_id,
        action_id: value.action_id,
        action_class: value.action_class as u8,
        operation_kind: value.operation_kind as u8,
        canonical_intent_digest: value.canonical_intent_digest,
        principal: principal_from_wire(value.principal),
        proposal_digest: value.proposal_digest,
        idempotency_key: value.idempotency_key,
        scope: scope_from_wire(value.scope),
        data_classes: value
            .data_classes
            .into_iter()
            .map(|item| item as u8)
            .collect(),
        effective_risk: value.effective_risk as u8,
        has_approval,
        approval: value
            .approval
            .map(approval_from_wire)
            .unwrap_or_else(empty_approval),
        issued_at_monotonic_ms: value.issued_at_monotonic_ms,
        expires_at_monotonic_ms: value.expires_at_monotonic_ms,
        authority_subject: authority_subject_from_wire(value.authority_subject),
        has_discovery,
        discovery: value
            .discovery
            .map(discovery_from_wire)
            .unwrap_or_else(empty_discovery),
    }
}

fn discovery_from_wire(
    value: wire::TaskDiscoveryAuthorityFact,
) -> ffi::BridgeTaskDiscoveryAuthority {
    ffi::BridgeTaskDiscoveryAuthority {
        discovery_tab_id: value.discovery_tab_id,
        browser_session_id: value.browser_session_id,
        remaining_new_source_cap: value.remaining_new_source_cap,
    }
}

fn empty_discovery() -> ffi::BridgeTaskDiscoveryAuthority {
    ffi::BridgeTaskDiscoveryAuthority {
        discovery_tab_id: String::new(),
        browser_session_id: String::new(),
        remaining_new_source_cap: 0,
    }
}

fn authority_subject_from_wire(value: wire::AuthoritySubject) -> ffi::BridgeAuthoritySubject {
    ffi::BridgeAuthoritySubject {
        kind: value.kind as u8,
        authority_subject_id: value.authority_subject_id,
    }
}

fn direct_observation_effect_from_wire(
    value: wire::EffectEnvelope,
) -> Option<ffi::BridgeDirectObservationEffect> {
    if value.kind != wire::EffectKind::PageObservation
        || value.retry_class != wire::RetryClass::Idempotent
        || value.storage_commit.is_some()
        || value.model_request.is_some()
        || value.network_request.is_some()
        || value.browser_action.is_some()
        || value.tool_job.is_some()
        || value.secure_store.is_some()
        || value.auth_surface.is_some()
        || value.permission_request.is_some()
    {
        return None;
    }
    let body = value.page_observation?;
    Some(ffi::BridgeDirectObservationEffect {
        operation: operation_from_wire(value.operation),
        effect_id: value.effect_id,
        tab_id: body.tab_id,
        frame_id: body.frame_id,
        page_epoch: body.page_epoch,
        scope: body.scope as u8,
        max_bytes: body.max_bytes,
        task_id: body.task_id,
        action_id: body.action_id,
        capability_id: body.capability_id,
        proposal_digest: body.proposal_digest,
        idempotency_key: body.idempotency_key,
        authority_subject: authority_subject_from_wire(body.authority_subject),
        max_nodes: body.max_nodes,
        max_text_bytes: body.max_text_bytes,
        max_frames: body.max_frames,
        deadline_ms: body.deadline_ms,
        expected_graph_revision: body.expected_graph_revision,
    })
}

fn operation_from_wire(value: wire::OperationEnvelope) -> ffi::BridgePolicyOperation {
    ffi::BridgePolicyOperation {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}

fn principal_from_wire(value: wire::PolicyPrincipal) -> ffi::BridgePrincipal {
    let has_skill_version_id = value.skill_version_id.is_some();
    ffi::BridgePrincipal {
        kind: value.kind as u8,
        has_skill_version_id,
        skill_version_id: value.skill_version_id.unwrap_or_default(),
    }
}

fn origin_from_wire(value: wire::PolicyOrigin) -> ffi::BridgeOrigin {
    let has_serialization = value.serialization.is_some();
    let has_opaque_id = value.opaque_id.is_some();
    ffi::BridgeOrigin {
        kind: value.kind as u8,
        has_serialization,
        serialization: value.serialization.unwrap_or_default(),
        has_opaque_id,
        opaque_id: value.opaque_id.unwrap_or_default(),
    }
}

fn scope_from_wire(value: wire::PolicyCapabilityScope) -> ffi::BridgePolicyScope {
    let has_node_id = value.node_id.is_some();
    let has_destination_scope = value.destination_scope.is_some();
    let has_destination_address = value.destination_address.is_some();
    ffi::BridgePolicyScope {
        profile_id: value.profile_id,
        tab_id: value.tab_id,
        frame_id: value.frame_id,
        page_epoch: value.page_epoch,
        origin: origin_from_wire(value.origin),
        has_node_id,
        node_id: value.node_id.unwrap_or_default(),
        has_destination_scope,
        destination_scope: value
            .destination_scope
            .map(origin_from_wire)
            .unwrap_or_else(empty_origin),
        has_destination_address,
        destination_address: value.destination_address.unwrap_or_default(),
        required_graph_revision: value.required_graph_revision,
        allowed_redirects: value
            .allowed_redirects
            .into_iter()
            .map(origin_from_wire)
            .collect(),
    }
}

fn approval_from_wire(value: wire::PolicyApprovalFact) -> ffi::BridgeApprovalFact {
    ffi::BridgeApprovalFact {
        receipt_reference: value.receipt_reference,
        proposal_digest: value.proposal_digest,
        service_generation: value.service_generation,
        expires_at_monotonic_ms: value.expires_at_monotonic_ms,
        expires_at_utc_ms: value.expires_at_utc_ms,
        browser_session_id: value.browser_session_id,
    }
}
