// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Core Service account-completion projections for the CXX boundary.

use core_runtime::wire;

use crate::service_bridge_account_ffi::ffi as account_ffi;

pub(super) fn completion_to_wire(
    mut value: account_ffi::BridgeAccountCompletion,
) -> Option<wire::EffectResult> {
    let result = completion_to_wire_inner(&mut value);
    wipe_bridge_entropy(&mut value);
    result
}

fn completion_to_wire_inner(
    value: &mut account_ffi::BridgeAccountCompletion,
) -> Option<wire::EffectResult> {
    let status = wire::EffectStatus::from_wire(u32::from(value.status))?;
    let kind = wire::EffectKind::from_wire(u32::from(value.kind))?;
    let (secure_store, auth_surface, network) = match kind {
        wire::EffectKind::SecureStore => (Some(secure_result(value)?), None, None),
        wire::EffectKind::OpenAuthSurface => (None, Some(surface_result(value)?), None),
        wire::EffectKind::NetworkRequest => (None, None, Some(network_result(value)?)),
        _ => return None,
    };
    let mut result = wire::EffectResult {
        operation: wire::OperationEnvelope {
            operation_id: core::mem::take(&mut value.operation.operation_id),
            service_generation: value.operation.service_generation,
            task_revision: value.operation.task_revision,
            deadline_monotonic_ms: value.operation.deadline_monotonic_ms,
            idempotency_key: core::mem::take(&mut value.operation.idempotency_key),
        },
        effect_id: core::mem::take(&mut value.effect_id),
        status,
        kind,
        storage: None,
        observation: None,
        model: None,
        network,
        browser_action: None,
        tool: None,
        secure_store,
        auth_surface,
        permission: None,
        asset_delivery: None,
        catalog: None,
        provider_listing: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    };
    if result.has_valid_body() {
        Some(result)
    } else {
        wipe_generated_entropy(&mut result);
        None
    }
}

fn secure_result(
    value: &mut account_ffi::BridgeAccountCompletion,
) -> Option<wire::SecureStoreEffectResult> {
    let operation_kind = wire::SecureStoreOperation::from_wire(u32::from(value.operation_kind))?;
    let transient_write = if operation_kind == wire::SecureStoreOperation::WriteTransient {
        Some(wire::TransientSecretWriteResult {
            flow_id: value.flow_id.clone(),
            purpose: wire::SecretMaterialPurpose::from_wire(u32::from(value.purpose))?,
            secret_handle: value.secret_handle.clone(),
        })
    } else {
        None
    };
    Some(wire::SecureStoreEffectResult {
        operation_kind,
        generated_entropy: (operation_kind == wire::SecureStoreOperation::GenerateEntropy).then(
            || wire::GeneratedEntropyResult {
                flow_id: value.flow_id.clone(),
                entropy: core::mem::take(&mut value.entropy),
            },
        ),
        transient_write,
        deleted_handle: (operation_kind == wire::SecureStoreOperation::DeleteHandle).then(|| {
            wire::DeletedSecretHandleResult {
                secret_handle: value.secret_handle.clone(),
                deleted: value.deleted,
            }
        }),
    })
}

pub(super) fn wipe_generated_entropy(result: &mut wire::EffectResult) {
    if let Some(entropy) = result
        .secure_store
        .as_mut()
        .and_then(|store| store.generated_entropy.as_mut())
        .map(|generated| &mut generated.entropy)
    {
        entropy.fill(0);
        entropy.clear();
    }
}

pub(super) fn wipe_bridge_entropy(value: &mut account_ffi::BridgeAccountCompletion) {
    value.entropy.fill(0);
    value.entropy.clear();
}

fn surface_result(
    value: &account_ffi::BridgeAccountCompletion,
) -> Option<wire::AuthSurfaceEffectResult> {
    let operation_kind = wire::AuthSurfaceOperation::from_wire(u32::from(value.operation_kind))?;
    Some(wire::AuthSurfaceEffectResult {
        operation_kind,
        oauth: (operation_kind == wire::AuthSurfaceOperation::OpenOauth).then(|| {
            wire::OAuthSurfaceResult {
                flow_id: value.flow_id.clone(),
                opened: value.opened,
            }
        }),
        native_credential: (operation_kind == wire::AuthSurfaceOperation::RequestNativeCredential)
            .then(|| wire::NativeCredentialSurfaceResult {
                flow_id: value.flow_id.clone(),
                opened: value.opened,
            }),
    })
}

fn network_result(
    value: &account_ffi::BridgeAccountCompletion,
) -> Option<wire::NetworkEffectResult> {
    let operation_kind = wire::AccountNetworkOperation::from_wire(u32::from(value.operation_kind))?;
    Some(wire::NetworkEffectResult {
        operation_kind,
        authorization_code_session: (operation_kind
            == wire::AccountNetworkOperation::ExchangeAuthorizationCode)
            .then(|| session_receipt(value))
            .flatten(),
        native_credential_session: (operation_kind
            == wire::AccountNetworkOperation::ExchangeNativeCredential)
            .then(|| session_receipt(value))
            .flatten(),
        email_link: (operation_kind == wire::AccountNetworkOperation::RequestEmailLink).then(
            || wire::EmailLinkNetworkResult {
                flow_id: value.flow_id.clone(),
                accepted: value.accepted,
            },
        ),
        refreshed_session: (operation_kind == wire::AccountNetworkOperation::RefreshSession)
            .then(|| session_receipt(value))
            .flatten(),
        revoked_session: (operation_kind == wire::AccountNetworkOperation::RevokeSession).then(
            || wire::RevokedSessionResult {
                session_handle: value.session_handle.clone(),
                deleted: value.deleted,
            },
        ),
        // A mint never rides this channel (decision 0082); the dedicated
        // entitlement legs carry its summary.
        entitlement_summary: None,
    })
}

fn session_receipt(
    value: &account_ffi::BridgeAccountCompletion,
) -> Option<wire::AccountSessionReceipt> {
    Some(wire::AccountSessionReceipt {
        session_handle: value.session_handle.clone(),
        account_subject: value.account_subject.clone(),
        expires_at_monotonic_ms: value.expires_at_monotonic_ms,
        rotation: value.rotation,
        auth_method: wire::AccountAuthMethod::from_wire(u32::from(value.auth_method))?,
        // A flag set over an empty string is the browser contradicting itself,
        // and the protocol treats an identity it cannot read as one it was not
        // given rather than guessing which half to believe.
        email: bridge_label(value.has_email, &value.email),
        display_name: bridge_label(value.has_display_name, &value.display_name),
    })
}

/// One optional label across the bridge: present only when both halves agree.
fn bridge_label(present: bool, value: &str) -> Option<String> {
    (present && !value.is_empty()).then(|| value.to_owned())
}
