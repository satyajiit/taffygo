// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed account effect completion decoding and stage transitions.

use core_service_types as wire;

use crate::account::{
    AccountAuthMethod, AccountEffect, AccountError, AccountFailure, AccountFailureCode, AuthFlowId,
    AuthorizationEntropy, GoogleNonceEntropy, SecretHandle, Sha256Port,
};
use crate::ports::AccountPort;

use super::{AccountServiceError, AccountStage, PendingAccountOperation};
use crate::codec::account_wire::decode_session_receipt;

pub(super) fn complete_stage(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
    digest: &dyn Sha256Port,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    match pending.stage {
        AccountStage::Entropy => complete_entropy(account, pending, result, digest).map(Some),
        AccountStage::Verifier => complete_verifier(account, pending, result).map(Some),
        AccountStage::GoogleNonceEntropy => {
            complete_google_nonce_entropy(account, pending, result, digest).map(Some)
        }
        AccountStage::GoogleNonceHandle => {
            complete_google_nonce_handle(account, pending, result).map(Some)
        }
        AccountStage::OAuthSurface => complete_oauth_surface(account, pending, result),
        AccountStage::NativeSurface => complete_native_surface(account, pending, result),
        AccountStage::NativeNonceCleanup => complete_native_nonce_cleanup(result),
        AccountStage::EmailLink => complete_email_link(account, pending, result),
        AccountStage::CodeExchange | AccountStage::NativeExchange => {
            complete_exchange(account, pending, result)
        }
        AccountStage::SignOut => complete_sign_out(result),
        AccountStage::Refresh => complete_refresh(account, result),
    }
}

fn complete_oauth_surface(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let body = result
        .auth_surface
        .as_ref()
        .and_then(|value| value.oauth.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending_flow(pending, &body.flow_id)?;
    account.accept_authorization_surface(flow_id, body.opened)?;
    Ok(None)
}

fn complete_native_surface(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let body = result
        .auth_surface
        .as_ref()
        .and_then(|value| value.native_credential.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let _ = pending_flow(pending, &body.flow_id)?;
    if !body.opened {
        if let Some(flow_id) = pending.flow_id.as_ref() {
            // The native credential sheet did not open at all. On Google that
            // is what an empty server client id produces — the broker refuses
            // before Credential Manager runs — so this is the one path that
            // makes an unregistered build say so instead of doing nothing.
            account.cancel_flow(
                flow_id,
                AccountFailure {
                    code: AccountFailureCode::NotConfigured,
                    method: pending.auth_method,
                },
            );
        }
        return Err(AccountError::SurfaceUnavailable.into());
    }
    Ok(None)
}

fn complete_native_nonce_cleanup(
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let body = result
        .secure_store
        .as_ref()
        .and_then(|value| value.deleted_handle.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    if body.secret_handle.is_empty() {
        return Err(AccountServiceError::InvalidCompletion);
    }
    Ok(None)
}

fn complete_email_link(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let body = result
        .network
        .as_ref()
        .and_then(|value| value.email_link.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending_flow(pending, &body.flow_id)?;
    account.accept_email_link_delivery(flow_id, body.accepted)?;
    Ok(None)
}

fn complete_exchange(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let network = result
        .network
        .as_ref()
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let body = match pending.stage {
        AccountStage::CodeExchange => network.authorization_code_session.as_ref(),
        AccountStage::NativeExchange => network.native_credential_session.as_ref(),
        _ => None,
    }
    .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending
        .flow_id
        .as_ref()
        .ok_or(AccountServiceError::InvalidCompletion)?;
    account.accept_exchange(flow_id, decode_session_receipt(body)?)?;
    Ok(None)
}

fn complete_sign_out(
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let body = result
        .network
        .as_ref()
        .and_then(|value| value.revoked_session.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    if !body.deleted {
        return Err(AccountServiceError::InvalidCompletion);
    }
    Ok(None)
}

fn complete_refresh(
    account: &mut dyn AccountPort,
    result: &wire::EffectResult,
) -> Result<Option<AccountEffect>, AccountServiceError> {
    let body = result
        .network
        .as_ref()
        .and_then(|value| value.refreshed_session.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    account.accept_refresh(decode_session_receipt(body)?)?;
    Ok(None)
}

fn complete_entropy(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
    digest: &dyn Sha256Port,
) -> Result<AccountEffect, AccountServiceError> {
    let body = result
        .secure_store
        .as_ref()
        .and_then(|value| value.generated_entropy.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending_flow(pending, &body.flow_id)?;
    let entropy = AuthorizationEntropy::new(
        body.entropy
            .as_slice()
            .try_into()
            .map_err(|_| AccountServiceError::InvalidCompletion)?,
    );
    Ok(account.accept_authorization_entropy(flow_id, &entropy, digest)?)
}

fn complete_verifier(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
) -> Result<AccountEffect, AccountServiceError> {
    let body = result
        .secure_store
        .as_ref()
        .and_then(|value| value.transient_write.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending_flow(pending, &body.flow_id)?;
    if body.purpose != wire::SecretMaterialPurpose::PkceVerifier {
        return Err(AccountServiceError::InvalidCompletion);
    }
    Ok(account
        .accept_pkce_verifier_handle(flow_id, SecretHandle::new(body.secret_handle.clone())?)?)
}

fn complete_google_nonce_entropy(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
    digest: &dyn Sha256Port,
) -> Result<AccountEffect, AccountServiceError> {
    let body = result
        .secure_store
        .as_ref()
        .and_then(|value| value.generated_entropy.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending_flow(pending, &body.flow_id)?;
    let entropy = GoogleNonceEntropy::new(
        body.entropy
            .as_slice()
            .try_into()
            .map_err(|_| AccountServiceError::InvalidCompletion)?,
    );
    Ok(account.accept_google_nonce_entropy(flow_id, &entropy, digest)?)
}

fn complete_google_nonce_handle(
    account: &mut dyn AccountPort,
    pending: &PendingAccountOperation,
    result: &wire::EffectResult,
) -> Result<AccountEffect, AccountServiceError> {
    let body = result
        .secure_store
        .as_ref()
        .and_then(|value| value.transient_write.as_ref())
        .ok_or(AccountServiceError::InvalidCompletion)?;
    let flow_id = pending_flow(pending, &body.flow_id)?;
    if body.purpose != wire::SecretMaterialPurpose::GoogleRawNonce {
        return Err(AccountServiceError::InvalidCompletion);
    }
    Ok(account
        .accept_google_raw_nonce_handle(flow_id, SecretHandle::new(body.secret_handle.clone())?)?)
}

pub(super) fn stage_and_flow(effect: &AccountEffect) -> (AccountStage, Option<AuthFlowId>) {
    match effect {
        AccountEffect::RequestSecureEntropy { flow_id, .. } => {
            (AccountStage::Entropy, Some(flow_id.clone()))
        }
        AccountEffect::StorePkceVerifier { flow_id, .. } => {
            (AccountStage::Verifier, Some(flow_id.clone()))
        }
        AccountEffect::RequestGoogleNonceEntropy { flow_id, .. } => {
            (AccountStage::GoogleNonceEntropy, Some(flow_id.clone()))
        }
        AccountEffect::StoreGoogleRawNonce { flow_id, .. } => {
            (AccountStage::GoogleNonceHandle, Some(flow_id.clone()))
        }
        AccountEffect::OpenAuthorization(plan) => {
            (AccountStage::OAuthSurface, Some(plan.flow_id.clone()))
        }
        AccountEffect::RequestNativeCredential { flow_id, .. } => {
            (AccountStage::NativeSurface, Some(flow_id.clone()))
        }
        AccountEffect::DeleteNativeNonce { flow_id, .. } => {
            (AccountStage::NativeNonceCleanup, Some(flow_id.clone()))
        }
        AccountEffect::RequestEmailLink(plan) => {
            (AccountStage::EmailLink, Some(plan.flow_id.clone()))
        }
        AccountEffect::ExchangeCode(plan) => {
            (AccountStage::CodeExchange, Some(plan.flow_id.clone()))
        }
        AccountEffect::ExchangeNativeCredential(plan) => {
            (AccountStage::NativeExchange, Some(plan.flow_id.clone()))
        }
        AccountEffect::Refresh(_) => (AccountStage::Refresh, None),
        AccountEffect::SignOut { .. } => (AccountStage::SignOut, None),
    }
}

fn pending_flow<'a>(
    pending: &'a PendingAccountOperation,
    returned_flow_id: &str,
) -> Result<&'a AuthFlowId, AccountServiceError> {
    let flow_id = pending
        .flow_id
        .as_ref()
        .ok_or(AccountServiceError::InvalidCompletion)?;
    if flow_id.as_str() != returned_flow_id {
        return Err(AccountServiceError::InvalidCompletion);
    }
    Ok(flow_id)
}

pub(super) fn derive_effect_id(
    operation_id: &str,
    stage: AccountStage,
    digest: &dyn Sha256Port,
) -> Result<String, AccountServiceError> {
    let mut input = b"taffy.account-effect.v1\0".to_vec();
    input.extend_from_slice(operation_id.as_bytes());
    input.push(0);
    input.extend_from_slice(stage.label().as_bytes());
    let digest = digest
        .sha256(&input)
        .map_err(|_| AccountServiceError::InvalidCommand)?;
    let mut output = String::with_capacity(64);
    for byte in digest {
        use core::fmt::Write as _;
        write!(&mut output, "{byte:02x}").map_err(|_| AccountServiceError::InvalidCommand)?;
    }
    Ok(output)
}

/// Which of the four methods an effect belongs to, where the effect says so.
///
/// The entropy, verifier and sign-out stages carry no method and return `None`
/// rather than a default: a failure that named the wrong button would be worse
/// than one that names none.
pub(super) const fn auth_method_of(effect: &AccountEffect) -> Option<AccountAuthMethod> {
    match effect {
        AccountEffect::RequestGoogleNonceEntropy { .. }
        | AccountEffect::StoreGoogleRawNonce { .. }
        | AccountEffect::DeleteNativeNonce { .. } => Some(AccountAuthMethod::Google),
        AccountEffect::RequestNativeCredential { auth_method, .. } => Some(*auth_method),
        AccountEffect::OpenAuthorization(plan) => Some(plan.auth_method),
        AccountEffect::RequestEmailLink(_) => Some(AccountAuthMethod::EmailLink),
        AccountEffect::ExchangeCode(plan) => Some(plan.auth_method),
        AccountEffect::ExchangeNativeCredential(plan) => Some(plan.auth_method),
        AccountEffect::RequestSecureEntropy { .. }
        | AccountEffect::StorePkceVerifier { .. }
        | AccountEffect::Refresh(_)
        | AccountEffect::SignOut { .. } => None,
    }
}

/// What a non-completed effect status means to the person who pressed a button.
///
/// The browser's `EffectStatus` describes what happened to an effect; this is
/// the same event in the eight-value vocabulary screen SCR-701 can render.
///
/// Two groupings are worth stating, because both are claims about what the
/// person should do next rather than about the enum:
///
/// - `UNAVAILABLE` is the only status that reports the method itself as
///   unconfigured, and the status projection demotes that method's row on the
///   strength of it. That is right here because the broker refuses this effect
///   outright when the build carries no server client id, which is a permanent
///   fact rather than a bad moment; a transient platform hiccup arrives as a
///   cancelled or credential-less sheet instead. `RESOURCE_LIMIT` is a bound
///   that was exceeded, which says nothing about whether the method works.
/// - `INVALID_RESULT` is not a refusal. The account plane answered with
///   something that failed structural validation, so the honest thing to say
///   is that nothing more is known — telling someone a method will never work
///   on the strength of an unreadable answer claims more than was observed.
///
/// `COMPLETED` cannot reach this function; the caller tests for it first. It
/// shares the arm that says nothing is known rather than being handed a reason
/// it does not have.
pub(super) const fn failure_code_for(status: wire::EffectStatus) -> AccountFailureCode {
    match status {
        wire::EffectStatus::Cancelled => AccountFailureCode::Cancelled,
        wire::EffectStatus::Denied => AccountFailureCode::Rejected,
        wire::EffectStatus::DeadlineExceeded | wire::EffectStatus::OutcomeUnknown => {
            AccountFailureCode::Network
        }
        wire::EffectStatus::Unavailable => AccountFailureCode::NotConfigured,
        wire::EffectStatus::ResourceLimit
        | wire::EffectStatus::InvalidResult
        | wire::EffectStatus::Completed => AccountFailureCode::Unknown,
    }
}
