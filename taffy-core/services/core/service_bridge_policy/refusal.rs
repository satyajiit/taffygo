// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The zero-valued policy records, and the refusal built out of them.
//!
//! cxx has no optional, so an absent grant, scope, origin or approval is a
//! record whose fields carry nothing beside a flag that says so. They are
//! gathered here rather than beside the projections that fill them because
//! they answer a different question: not what a verdict says, but what the
//! bridge sends when there is no verdict to say it with.

use core_runtime::wire;

use crate::service_bridge_policy_ffi::ffi;

pub(super) fn invalid_result(operation_id: String) -> ffi::BridgePolicyResult {
    ffi::BridgePolicyResult {
        operation_id,
        status: wire::PolicyEvaluationStatus::InvalidRequest as u8,
        has_denial_code: false,
        denial_code: 0,
        has_minted_grant: false,
        minted_grant: empty_grant(),
        has_direct_observation_effect: false,
        direct_observation_effect: empty_direct_observation_effect(),
    }
}

pub(super) fn empty_grant() -> ffi::BridgeMintedGrant {
    ffi::BridgeMintedGrant {
        capability_id: String::new(),
        service_generation: 0,
        policy_version: 0,
        actor_lease_id: String::new(),
        task_id: String::new(),
        action_id: String::new(),
        action_class: 0,
        operation_kind: 0,
        canonical_intent_digest: [0; 32],
        principal: ffi::BridgePrincipal {
            kind: 0,
            has_skill_version_id: false,
            skill_version_id: String::new(),
        },
        proposal_digest: String::new(),
        idempotency_key: String::new(),
        scope: empty_scope(),
        data_classes: Vec::new(),
        effective_risk: 0,
        has_approval: false,
        approval: empty_approval(),
        issued_at_monotonic_ms: 0,
        expires_at_monotonic_ms: 0,
        authority_subject: empty_authority_subject(),
        has_discovery: false,
        discovery: ffi::BridgeTaskDiscoveryAuthority {
            discovery_tab_id: String::new(),
            browser_session_id: String::new(),
            remaining_new_source_cap: 0,
        },
    }
}

pub(super) fn empty_direct_observation_effect() -> ffi::BridgeDirectObservationEffect {
    ffi::BridgeDirectObservationEffect {
        operation: ffi::BridgePolicyOperation {
            operation_id: String::new(),
            service_generation: 0,
            task_revision: 0,
            deadline_monotonic_ms: 0,
            idempotency_key: String::new(),
        },
        effect_id: String::new(),
        tab_id: String::new(),
        frame_id: String::new(),
        page_epoch: String::new(),
        scope: 0,
        max_bytes: 0,
        task_id: String::new(),
        action_id: String::new(),
        capability_id: String::new(),
        proposal_digest: String::new(),
        idempotency_key: String::new(),
        authority_subject: empty_authority_subject(),
        max_nodes: 0,
        max_text_bytes: 0,
        max_frames: 0,
        deadline_ms: 0,
        expected_graph_revision: 0,
    }
}

pub(super) fn empty_authority_subject() -> ffi::BridgeAuthoritySubject {
    ffi::BridgeAuthoritySubject {
        kind: 0,
        authority_subject_id: String::new(),
    }
}

pub(super) fn empty_scope() -> ffi::BridgePolicyScope {
    ffi::BridgePolicyScope {
        profile_id: String::new(),
        tab_id: String::new(),
        frame_id: String::new(),
        page_epoch: String::new(),
        origin: empty_origin(),
        has_node_id: false,
        node_id: String::new(),
        has_destination_scope: false,
        destination_scope: empty_origin(),
        has_destination_address: false,
        destination_address: String::new(),
        required_graph_revision: 0,
        allowed_redirects: Vec::new(),
    }
}

pub(super) fn empty_origin() -> ffi::BridgeOrigin {
    ffi::BridgeOrigin {
        kind: 0,
        has_serialization: false,
        serialization: String::new(),
        has_opaque_id: false,
        opaque_id: String::new(),
    }
}

pub(super) fn empty_approval() -> ffi::BridgeApprovalFact {
    ffi::BridgeApprovalFact {
        receipt_reference: String::new(),
        proposal_digest: String::new(),
        service_generation: 0,
        expires_at_monotonic_ms: 0,
        expires_at_utc_ms: 0,
        browser_session_id: String::new(),
    }
}
