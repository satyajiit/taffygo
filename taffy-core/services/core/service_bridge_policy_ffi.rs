// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Policy-only CXX records kept outside the task/runtime bridge surface.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgePolicyOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgePrincipal {
        kind: u8,
        has_skill_version_id: bool,
        skill_version_id: String,
    }

    struct BridgeOrigin {
        kind: u8,
        has_serialization: bool,
        serialization: String,
        has_opaque_id: bool,
        opaque_id: String,
    }

    struct BridgePolicyScope {
        profile_id: String,
        tab_id: String,
        frame_id: String,
        page_epoch: String,
        origin: BridgeOrigin,
        has_node_id: bool,
        node_id: String,
        has_destination_scope: bool,
        destination_scope: BridgeOrigin,
        has_destination_address: bool,
        destination_address: String,
        required_graph_revision: u64,
        allowed_redirects: Vec<BridgeOrigin>,
    }

    struct BridgeApprovalFact {
        receipt_reference: String,
        proposal_digest: String,
        service_generation: u64,
        expires_at_monotonic_ms: u64,
        expires_at_utc_ms: u64,
        browser_session_id: String,
    }

    struct BridgeAuthoritySubject {
        kind: u8,
        authority_subject_id: String,
    }

    struct BridgeActorLeaseFact {
        lease_id: String,
        service_generation: u64,
        task_id: String,
        authority_subject: BridgeAuthoritySubject,
        profile_id: String,
        tab_id: String,
        control_mode: u8,
        expires_at_monotonic_ms: u64,
    }

    struct BridgeTaskDiscoveryAuthority {
        discovery_tab_id: String,
        browser_session_id: String,
        remaining_new_source_cap: u32,
    }

    struct BridgePolicyRequest {
        operation: BridgePolicyOperation,
        now_monotonic_ms: u64,
        now_utc_ms: u64,
        action_id: String,
        task_id: String,
        principal: BridgePrincipal,
        action_class: u8,
        operation_kind: u8,
        canonical_intent_digest: [u8; 32],
        proposal_digest: String,
        scope: BridgePolicyScope,
        data_classes: Vec<u8>,
        context_risk: u8,
        expires_at_monotonic_ms: u64,
        actor_lease: BridgeActorLeaseFact,
        has_approval: bool,
        approval: BridgeApprovalFact,
        context: u8,
        authority_subject: BridgeAuthoritySubject,
        policy_version: u32,
        has_discovery: bool,
        discovery: BridgeTaskDiscoveryAuthority,
    }

    struct BridgeMintedGrant {
        capability_id: String,
        service_generation: u64,
        policy_version: u32,
        actor_lease_id: String,
        task_id: String,
        action_id: String,
        action_class: u8,
        operation_kind: u8,
        canonical_intent_digest: [u8; 32],
        principal: BridgePrincipal,
        proposal_digest: String,
        idempotency_key: String,
        scope: BridgePolicyScope,
        data_classes: Vec<u8>,
        effective_risk: u8,
        has_approval: bool,
        approval: BridgeApprovalFact,
        issued_at_monotonic_ms: u64,
        expires_at_monotonic_ms: u64,
        authority_subject: BridgeAuthoritySubject,
        has_discovery: bool,
        discovery: BridgeTaskDiscoveryAuthority,
    }

    struct BridgeDirectObservationEffect {
        operation: BridgePolicyOperation,
        effect_id: String,
        tab_id: String,
        frame_id: String,
        page_epoch: String,
        scope: u8,
        max_bytes: u32,
        task_id: String,
        action_id: String,
        capability_id: String,
        proposal_digest: String,
        idempotency_key: String,
        authority_subject: BridgeAuthoritySubject,
        max_nodes: u32,
        max_text_bytes: u32,
        max_frames: u32,
        deadline_ms: u32,
        expected_graph_revision: u64,
    }

    struct BridgePolicyResult {
        operation_id: String,
        status: u8,
        /// Present exactly for a denied status: the closed action result
        /// code the proposal is refused with.
        has_denial_code: bool,
        denial_code: u8,
        has_minted_grant: bool,
        minted_grant: BridgeMintedGrant,
        has_direct_observation_effect: bool,
        direct_observation_effect: BridgeDirectObservationEffect,
    }
}
