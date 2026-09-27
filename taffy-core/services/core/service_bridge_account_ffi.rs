// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Account-only inbound CXX records kept out of the shared state bridge.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeAccountOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeAccountCommand {
        operation: BridgeAccountOperation,
        kind: u8,
        flow_id: String,
        auth_method: u8,
        redirect_binding_id: String,
        scopes: Vec<u8>,
        issued_at_monotonic_ms: u64,
        email: String,
        has_account_subject: bool,
        account_subject: String,
        returned_state: String,
        auth_callback_status: u8,
        has_authorization_code_handle: bool,
        authorization_code_handle: String,
        auth_credential_status: u8,
        has_credential_handle: bool,
        credential_handle: String,
    }

    struct BridgeAccountCompletion {
        operation: BridgeAccountOperation,
        effect_id: String,
        status: u8,
        kind: u8,
        operation_kind: u8,
        flow_id: String,
        entropy: Vec<u8>,
        purpose: u8,
        secret_handle: String,
        deleted: bool,
        opened: bool,
        accepted: bool,
        session_handle: String,
        account_subject: String,
        expires_at_monotonic_ms: u64,
        rotation: u64,
        auth_method: u8,
        // Same absence discipline as the validation result below: cxx has no
        // optional, so a flag stands beside an empty string and both halves
        // are checked on the far side.
        has_email: bool,
        email: String,
        has_display_name: bool,
        display_name: String,
    }

    struct BridgeAccountTokenValidationRequest {
        operation_id: String,
        service_generation: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
        operation_kind: u8,
        expected_auth_method: u8,
        has_expected_account_subject: bool,
        expected_account_subject: String,
        target_rotation: u64,
        response_body: Vec<u8>,
    }

    struct BridgeAccountTokenValidationResult {
        status: u8,
        operation_id: String,
        operation_kind: u8,
        auth_method: u8,
        account_subject: String,
        expires_in_seconds: u64,
        target_rotation: u64,
        access_token: Vec<u8>,
        refresh_token: Vec<u8>,
        // cxx has no optional, so absence is a flag beside an empty string.
        // Both halves are checked on the far side: an address present with no
        // text is a defect rather than an empty address.
        has_email: bool,
        email: String,
        has_display_name: bool,
        display_name: String,
    }

    extern "Rust" {
        fn ValidateAccountTokenResponse(
            request: BridgeAccountTokenValidationRequest,
            expected_generation: u64,
            now_monotonic_ms: u64,
        ) -> BridgeAccountTokenValidationResult;
    }
}

#[allow(non_snake_case)]
fn ValidateAccountTokenResponse(
    request: ffi::BridgeAccountTokenValidationRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
) -> ffi::BridgeAccountTokenValidationResult {
    let operation_kind =
        core_runtime::wire::AccountNetworkOperation::from_wire(u32::from(request.operation_kind));
    let auth_method =
        core_runtime::wire::AccountAuthMethod::from_wire(u32::from(request.expected_auth_method));
    let Some((operation_kind, auth_method)) = operation_kind.zip(auth_method) else {
        return invalid_validation_result(request);
    };
    let result = core_runtime::validate_account_token_response(
        core_runtime::wire::AccountTokenValidationRequest {
            operation: core_runtime::wire::OperationEnvelope {
                operation_id: request.operation_id,
                service_generation: request.service_generation,
                task_revision: 0,
                deadline_monotonic_ms: request.deadline_monotonic_ms,
                idempotency_key: request.idempotency_key,
            },
            operation_kind,
            expected_auth_method: auth_method,
            expected_account_subject: request
                .has_expected_account_subject
                .then_some(request.expected_account_subject),
            target_rotation: request.target_rotation,
            response_body: request.response_body,
        },
        expected_generation,
        now_monotonic_ms,
    );
    ffi::BridgeAccountTokenValidationResult {
        status: result.status as u8,
        operation_id: result.operation_id,
        operation_kind: result.operation_kind as u8,
        auth_method: result.auth_method as u8,
        account_subject: result.account_subject,
        expires_in_seconds: result.expires_in_seconds,
        target_rotation: result.target_rotation,
        access_token: result.access_token,
        refresh_token: result.refresh_token,
        has_email: result.email.is_some(),
        email: result.email.unwrap_or_default(),
        has_display_name: result.display_name.is_some(),
        display_name: result.display_name.unwrap_or_default(),
    }
}

fn invalid_validation_result(
    mut request: ffi::BridgeAccountTokenValidationRequest,
) -> ffi::BridgeAccountTokenValidationResult {
    request.response_body.fill(0);
    ffi::BridgeAccountTokenValidationResult {
        status: core_runtime::wire::AccountTokenValidationStatus::InvalidResponse as u8,
        operation_id: request.operation_id,
        operation_kind: request.operation_kind,
        auth_method: request.expected_auth_method,
        account_subject: String::new(),
        expires_in_seconds: 0,
        target_rotation: request.target_rotation,
        access_token: Vec::new(),
        refresh_token: Vec::new(),
        has_email: false,
        email: String::new(),
        has_display_name: false,
        display_name: String::new(),
    }
}
