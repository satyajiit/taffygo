// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable account effects projected into the closed generated union.

use core_service_types as wire;

use crate::account::AccountEffect;

use super::{account_response_limit, encode_auth_method, encode_scopes, AccountWireError};

/// Projects one portable account plan into the generated Core Service effect.
pub fn encode_account_effect(
    effect: AccountEffect,
    operation: wire::OperationEnvelope,
    effect_id: String,
) -> Result<wire::EffectEnvelope, AccountWireError> {
    let envelope = empty_envelope(operation, effect_id);
    let projected = match effect {
        effect @ (AccountEffect::RequestSecureEntropy { .. }
        | AccountEffect::RequestGoogleNonceEntropy { .. }
        | AccountEffect::StorePkceVerifier { .. }
        | AccountEffect::StoreGoogleRawNonce { .. }
        | AccountEffect::DeleteNativeNonce { .. }
        | AccountEffect::OpenAuthorization(_)
        | AccountEffect::RequestNativeCredential { .. }) => encode_platform(effect, envelope)?,
        effect @ (AccountEffect::RequestEmailLink(_)
        | AccountEffect::ExchangeCode(_)
        | AccountEffect::ExchangeNativeCredential(_)
        | AccountEffect::Refresh(_)
        | AccountEffect::SignOut { .. }) => encode_network(effect, envelope)?,
    };
    if !projected.has_valid_body() {
        return Err(AccountWireError::InvalidEffect);
    }
    Ok(projected)
}

fn empty_envelope(operation: wire::OperationEnvelope, effect_id: String) -> wire::EffectEnvelope {
    wire::EffectEnvelope {
        operation,
        effect_id,
        kind: wire::EffectKind::SecureStore,
        retry_class: wire::RetryClass::Consequential,
        storage_commit: None,
        page_observation: None,
        model_request: None,
        network_request: None,
        browser_action: None,
        tool_job: None,
        secure_store: None,
        auth_surface: None,
        permission_request: None,
        asset_delivery: None,
        catalog_fetch: None,
        provider_listing_fetch: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    }
}

fn encode_platform(
    effect: AccountEffect,
    mut envelope: wire::EffectEnvelope,
) -> Result<wire::EffectEnvelope, AccountWireError> {
    match effect {
        AccountEffect::RequestSecureEntropy { flow_id, bytes }
        | AccountEffect::RequestGoogleNonceEntropy { flow_id, bytes } => {
            encode_entropy_request(&flow_id, bytes, &mut envelope)?;
        }
        AccountEffect::StorePkceVerifier { flow_id, verifier } => {
            envelope.secure_store = Some(wire::SecureStoreEffect {
                operation_kind: wire::SecureStoreOperation::WriteTransient,
                generate_entropy: None,
                write_transient: Some(wire::WriteTransientSecretRequest {
                    flow_id: flow_id.as_str().to_owned(),
                    purpose: wire::SecretMaterialPurpose::PkceVerifier,
                    material: verifier.into_bytes(),
                }),
                delete_handle: None,
            });
        }
        AccountEffect::StoreGoogleRawNonce {
            flow_id, raw_nonce, ..
        } => {
            envelope.secure_store = Some(wire::SecureStoreEffect {
                operation_kind: wire::SecureStoreOperation::WriteTransient,
                generate_entropy: None,
                write_transient: Some(wire::WriteTransientSecretRequest {
                    flow_id: flow_id.as_str().to_owned(),
                    purpose: wire::SecretMaterialPurpose::GoogleRawNonce,
                    material: raw_nonce.into_bytes(),
                }),
                delete_handle: None,
            });
        }
        AccountEffect::DeleteNativeNonce {
            raw_nonce_handle, ..
        } => {
            envelope.secure_store = Some(wire::SecureStoreEffect {
                operation_kind: wire::SecureStoreOperation::DeleteHandle,
                generate_entropy: None,
                write_transient: None,
                delete_handle: Some(wire::DeleteSecretHandleRequest {
                    secret_handle: raw_nonce_handle.opaque_id().to_owned(),
                }),
            });
        }
        AccountEffect::OpenAuthorization(plan) => {
            envelope.kind = wire::EffectKind::OpenAuthSurface;
            envelope.auth_surface = Some(wire::AuthSurfaceEffect {
                operation_kind: wire::AuthSurfaceOperation::OpenOauth,
                oauth: Some(wire::OAuthSurfaceRequest {
                    flow_id: plan.flow_id.as_str().to_owned(),
                    auth_method: encode_auth_method(plan.auth_method),
                    redirect_binding_id: plan.redirect_binding.as_str().to_owned(),
                    pkce_challenge: plan.pkce_challenge.as_str().to_owned(),
                    state: plan.state.as_str().to_owned(),
                    scopes: encode_scopes(&plan.scopes),
                    pkce_verifier_handle: plan.pkce_verifier_handle.opaque_id().to_owned(),
                }),
                native_credential: None,
            });
        }
        AccountEffect::RequestNativeCredential {
            flow_id,
            auth_method,
            raw_nonce_handle,
            hashed_nonce,
        } => {
            envelope.kind = wire::EffectKind::OpenAuthSurface;
            envelope.auth_surface = Some(wire::AuthSurfaceEffect {
                operation_kind: wire::AuthSurfaceOperation::RequestNativeCredential,
                oauth: None,
                native_credential: Some(wire::NativeCredentialSurfaceRequest {
                    flow_id: flow_id.as_str().to_owned(),
                    auth_method: encode_auth_method(auth_method),
                    raw_nonce_handle: raw_nonce_handle.opaque_id().to_owned(),
                    hashed_nonce: hashed_nonce.as_str().to_owned(),
                }),
            });
        }
        _ => return Err(AccountWireError::InvalidEffect),
    }
    Ok(envelope)
}

fn encode_entropy_request(
    flow_id: &crate::account::AuthFlowId,
    bytes: usize,
    envelope: &mut wire::EffectEnvelope,
) -> Result<(), AccountWireError> {
    envelope.kind = wire::EffectKind::SecureStore;
    envelope.retry_class = wire::RetryClass::Never;
    envelope.secure_store = Some(wire::SecureStoreEffect {
        operation_kind: wire::SecureStoreOperation::GenerateEntropy,
        generate_entropy: Some(wire::GenerateEntropyRequest {
            flow_id: flow_id.as_str().to_owned(),
            byte_count: u32::try_from(bytes).map_err(|_| AccountWireError::InvalidEffect)?,
        }),
        write_transient: None,
        delete_handle: None,
    });
    Ok(())
}

fn encode_network(
    effect: AccountEffect,
    mut envelope: wire::EffectEnvelope,
) -> Result<wire::EffectEnvelope, AccountWireError> {
    envelope.kind = wire::EffectKind::NetworkRequest;
    envelope.network_request = Some(match effect {
        AccountEffect::RequestEmailLink(plan) => wire::NetworkRequestEffect {
            operation_kind: wire::AccountNetworkOperation::RequestEmailLink,
            exchange_authorization_code: None,
            exchange_native_credential: None,
            request_email_link: Some(wire::EmailLinkNetworkRequest {
                flow_id: plan.flow_id.as_str().to_owned(),
                email: plan.email.as_str().to_owned(),
                pkce_verifier_handle: plan.pkce_verifier_handle.opaque_id().to_owned(),
                redirect_binding_id: plan.redirect_binding.as_str().to_owned(),
                pkce_challenge: plan.pkce_challenge.as_str().to_owned(),
                state: plan.state.as_str().to_owned(),
            }),
            refresh_session: None,
            revoke_session: None,
            max_response_bytes: account_response_limit()?,
            fetch_entitlement: None,
        },
        AccountEffect::ExchangeCode(plan) => wire::NetworkRequestEffect {
            operation_kind: wire::AccountNetworkOperation::ExchangeAuthorizationCode,
            exchange_authorization_code: Some(wire::ExchangeAuthorizationCodeRequest {
                flow_id: plan.flow_id.as_str().to_owned(),
                auth_method: encode_auth_method(plan.auth_method),
                authorization_code_handle: plan.authorization_code_handle.opaque_id().to_owned(),
                pkce_verifier_handle: plan.pkce_verifier_handle.opaque_id().to_owned(),
                redirect_binding_id: plan.redirect_binding.as_str().to_owned(),
            }),
            exchange_native_credential: None,
            request_email_link: None,
            refresh_session: None,
            revoke_session: None,
            max_response_bytes: account_response_limit()?,
            fetch_entitlement: None,
        },
        AccountEffect::ExchangeNativeCredential(plan) => wire::NetworkRequestEffect {
            operation_kind: wire::AccountNetworkOperation::ExchangeNativeCredential,
            exchange_authorization_code: None,
            exchange_native_credential: Some(wire::ExchangeNativeCredentialRequest {
                flow_id: plan.flow_id.as_str().to_owned(),
                auth_method: encode_auth_method(plan.auth_method),
                credential_handle: plan.credential_handle.opaque_id().to_owned(),
                raw_nonce_handle: plan.raw_nonce_handle.opaque_id().to_owned(),
            }),
            request_email_link: None,
            refresh_session: None,
            revoke_session: None,
            max_response_bytes: account_response_limit()?,
            fetch_entitlement: None,
        },
        AccountEffect::Refresh(plan) => wire::NetworkRequestEffect {
            operation_kind: wire::AccountNetworkOperation::RefreshSession,
            exchange_authorization_code: None,
            exchange_native_credential: None,
            request_email_link: None,
            refresh_session: Some(wire::RefreshSessionRequest {
                session_handle: plan.session_handle.opaque_id().to_owned(),
                expected_rotation: plan.expected_rotation,
                expected_account_subject: plan.expected_account_subject.as_str().to_owned(),
                expected_auth_method: encode_auth_method(plan.expected_auth_method),
            }),
            revoke_session: None,
            max_response_bytes: account_response_limit()?,
            fetch_entitlement: None,
        },
        AccountEffect::SignOut { session_handle, .. } => wire::NetworkRequestEffect {
            operation_kind: wire::AccountNetworkOperation::RevokeSession,
            exchange_authorization_code: None,
            exchange_native_credential: None,
            request_email_link: None,
            refresh_session: None,
            revoke_session: Some(wire::RevokeSessionRequest {
                session_handle: session_handle.opaque_id().to_owned(),
            }),
            max_response_bytes: account_response_limit()?,
            fetch_entitlement: None,
        },
        _ => return Err(AccountWireError::InvalidEffect),
    });
    Ok(envelope)
}
