// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Core Service account-effect projections for the CXX boundary.

use core_runtime::wire;

use crate::ffi;

pub(super) fn effect_to_bridge(effect: wire::EffectEnvelope) -> Option<ffi::BridgeAccountEffect> {
    let mut out = empty_bridge_effect(&effect);
    match effect.kind {
        wire::EffectKind::SecureStore => project_secure(effect.secure_store?, &mut out),
        wire::EffectKind::OpenAuthSurface => project_surface(effect.auth_surface?, &mut out),
        wire::EffectKind::NetworkRequest => project_network(effect.network_request?, &mut out),
        _ => return None,
    }
    Some(out)
}

fn empty_bridge_effect(effect: &wire::EffectEnvelope) -> ffi::BridgeAccountEffect {
    ffi::BridgeAccountEffect {
        operation: ffi::BridgeOperation {
            operation_id: effect.operation.operation_id.clone(),
            service_generation: effect.operation.service_generation,
            task_revision: effect.operation.task_revision,
            deadline_monotonic_ms: effect.operation.deadline_monotonic_ms,
            idempotency_key: effect.operation.idempotency_key.clone(),
        },
        effect_id: effect.effect_id.clone(),
        kind: effect.kind as u8,
        retry_class: effect.retry_class as u8,
        operation_kind: 0,
        flow_id: String::new(),
        auth_method: 0,
        scopes: Vec::new(),
        byte_count: 0,
        purpose: 0,
        material: Vec::new(),
        secret_handle: String::new(),
        email: String::new(),
        pkce_verifier_handle: String::new(),
        redirect_binding_id: String::new(),
        pkce_challenge: String::new(),
        state: String::new(),
        authorization_code_handle: String::new(),
        credential_handle: String::new(),
        raw_nonce_handle: String::new(),
        hashed_nonce: String::new(),
        session_handle: String::new(),
        account_subject: String::new(),
        expected_rotation: 0,
        max_response_bytes: 0,
    }
}

fn project_secure(effect: wire::SecureStoreEffect, out: &mut ffi::BridgeAccountEffect) {
    out.operation_kind = effect.operation_kind as u8;
    if let Some(body) = effect.generate_entropy {
        out.flow_id = body.flow_id;
        out.byte_count = body.byte_count;
    } else if let Some(body) = effect.write_transient {
        out.flow_id = body.flow_id;
        out.purpose = body.purpose as u8;
        out.material = body.material;
    } else if let Some(body) = effect.delete_handle {
        out.secret_handle = body.secret_handle;
    }
}

fn project_surface(effect: wire::AuthSurfaceEffect, out: &mut ffi::BridgeAccountEffect) {
    out.operation_kind = effect.operation_kind as u8;
    if let Some(body) = effect.oauth {
        out.flow_id = body.flow_id;
        out.auth_method = body.auth_method as u8;
        out.redirect_binding_id = body.redirect_binding_id;
        out.pkce_challenge = body.pkce_challenge;
        out.pkce_verifier_handle = body.pkce_verifier_handle;
        out.state = body.state;
        out.scopes = body.scopes.into_iter().map(|scope| scope as u8).collect();
    } else if let Some(body) = effect.native_credential {
        out.flow_id = body.flow_id;
        out.auth_method = body.auth_method as u8;
        out.raw_nonce_handle = body.raw_nonce_handle;
        out.hashed_nonce = body.hashed_nonce;
    }
}

fn project_network(effect: wire::NetworkRequestEffect, out: &mut ffi::BridgeAccountEffect) {
    out.operation_kind = effect.operation_kind as u8;
    out.max_response_bytes = effect.max_response_bytes;
    if let Some(body) = effect.exchange_authorization_code {
        out.flow_id = body.flow_id;
        out.auth_method = body.auth_method as u8;
        out.authorization_code_handle = body.authorization_code_handle;
        out.pkce_verifier_handle = body.pkce_verifier_handle;
        out.redirect_binding_id = body.redirect_binding_id;
    } else if let Some(body) = effect.exchange_native_credential {
        out.flow_id = body.flow_id;
        out.auth_method = body.auth_method as u8;
        out.credential_handle = body.credential_handle;
        out.raw_nonce_handle = body.raw_nonce_handle;
    } else if let Some(body) = effect.request_email_link {
        out.flow_id = body.flow_id;
        out.email = body.email;
        out.pkce_verifier_handle = body.pkce_verifier_handle;
        out.redirect_binding_id = body.redirect_binding_id;
        out.pkce_challenge = body.pkce_challenge;
        out.state = body.state;
    } else if let Some(body) = effect.refresh_session {
        out.session_handle = body.session_handle;
        out.account_subject = body.expected_account_subject;
        out.auth_method = body.expected_auth_method as u8;
        out.expected_rotation = body.expected_rotation;
    } else if let Some(body) = effect.revoke_session {
        out.session_handle = body.session_handle;
    }
}
