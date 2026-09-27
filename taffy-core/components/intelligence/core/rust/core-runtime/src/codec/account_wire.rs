// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fail-closed generated Core Service account projections.

use core_service_types as wire;

use crate::account::{
    AccountAuthMethod, AccountDisplayName, AccountEmail, AccountError, AccountScope,
    AccountSession, AccountSubjectId, AuthFlowId, AuthorizationCodeHandle, AuthorizationIntent,
    NativeCredentialOutcome, RedirectBindingId, RedirectOutcome, RedirectReceipt, RedirectState,
    SecretHandle, SessionHandle, SessionReceipt,
};
use crate::contract::Deadline;

mod effect;

pub use effect::encode_account_effect;

/// Validated start operation selected by the closed account method.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AccountStart {
    Pkce(AuthorizationIntent),
    Native {
        flow_id: AuthFlowId,
        auth_method: AccountAuthMethod,
        deadline: Deadline,
    },
}

/// Why a generated account command cannot enter the portable protocol.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AccountWireError {
    InvalidPresence,
    InvalidExpiry,
    InvalidScope,
    InvalidEffect,
    Domain(AccountError),
}

/// Converts a browser-stamped account start into the exact portable flow family.
pub fn decode_start_auth(
    command: &wire::StartAuthCommand,
    operation: &wire::OperationEnvelope,
) -> Result<AccountStart, AccountWireError> {
    let flow_id = AuthFlowId::new(command.flow_id.clone())?;
    let auth_method = decode_auth_method(command.method);
    let deadline = Deadline::from_millis(operation.deadline_monotonic_ms);
    if auth_method == AccountAuthMethod::Google {
        if !command.scopes.is_empty() {
            return Err(AccountWireError::InvalidScope);
        }
        return Ok(AccountStart::Native {
            flow_id,
            auth_method,
            deadline,
        });
    }
    Ok(AccountStart::Pkce(AuthorizationIntent {
        flow_id,
        auth_method,
        email: None,
        redirect_binding: RedirectBindingId::new(command.redirect_binding_id.clone())?,
        scopes: decode_scopes(&command.scopes)?,
        issued_at_millis: command.issued_at_monotonic_ms,
        deadline,
    }))
}

/// Converts the dedicated email-link command into a PKCE authorization intent.
pub fn decode_email_link(
    command: &wire::RequestEmailLinkCommand,
    operation: &wire::OperationEnvelope,
) -> Result<AuthorizationIntent, AccountWireError> {
    Ok(AuthorizationIntent {
        flow_id: AuthFlowId::new(command.flow_id.clone())?,
        auth_method: AccountAuthMethod::EmailLink,
        email: Some(AccountEmail::new(command.email.clone())?),
        redirect_binding: RedirectBindingId::new(command.redirect_binding_id.clone())?,
        scopes: decode_scopes(&command.scopes)?,
        issued_at_millis: command.issued_at_monotonic_ms,
        deadline: Deadline::from_millis(operation.deadline_monotonic_ms),
    })
}

/// Converts one terminal native credential command with exact success presence.
pub fn decode_native_credential(
    command: &wire::AuthCredentialResultCommand,
) -> Result<(AuthFlowId, AccountAuthMethod, NativeCredentialOutcome), AccountWireError> {
    let method = decode_auth_method(command.method);
    let outcome = match (command.status, command.credential_handle.as_ref()) {
        (wire::AuthCredentialStatus::Success, Some(handle)) => {
            NativeCredentialOutcome::Success(SecretHandle::new(handle.clone())?)
        }
        (wire::AuthCredentialStatus::Cancelled, None) => NativeCredentialOutcome::Cancelled,
        (wire::AuthCredentialStatus::NoCredential, None) => NativeCredentialOutcome::NoCredential,
        (wire::AuthCredentialStatus::Unavailable, None) => NativeCredentialOutcome::Unavailable,
        _ => return Err(AccountWireError::InvalidPresence),
    };
    Ok((AuthFlowId::new(command.flow_id.clone())?, method, outcome))
}

impl From<AccountError> for AccountWireError {
    fn from(value: AccountError) -> Self {
        Self::Domain(value)
    }
}

/// Converts the generated handle-only callback into a bound redirect receipt.
pub fn decode_auth_callback(
    command: &wire::AuthCallbackCommand,
) -> Result<RedirectReceipt, AccountWireError> {
    let outcome = match (&command.status, &command.authorization_code_handle) {
        (wire::AuthCallbackStatus::AuthorizationCode, Some(handle)) => {
            RedirectOutcome::AuthorizationCode(AuthorizationCodeHandle::new(handle.clone())?)
        }
        (wire::AuthCallbackStatus::Denied, None) => RedirectOutcome::Denied,
        (wire::AuthCallbackStatus::ProviderError, None) => RedirectOutcome::ProviderError,
        (wire::AuthCallbackStatus::DeadlineExceeded, None) => RedirectOutcome::DeadlineExceeded,
        (wire::AuthCallbackStatus::PlatformUnavailable, None) => {
            RedirectOutcome::PlatformUnavailable
        }
        _ => return Err(AccountWireError::InvalidPresence),
    };
    Ok(RedirectReceipt {
        flow_id: AuthFlowId::new(command.flow_id.clone())?,
        redirect_binding: RedirectBindingId::new(command.redirect_binding_id.clone())?,
        state: RedirectState::new(command.returned_state.clone())?,
        outcome,
    })
}

/// Restores the browser's committed session handle without token material.
pub fn decode_account_session(
    session: &wire::AccountSessionHandle,
) -> Result<AccountSession, AccountWireError> {
    if session.expires_at_monotonic_ms == 0 {
        return Err(AccountWireError::InvalidExpiry);
    }
    Ok(AccountSession {
        session_handle: SessionHandle::new(session.session_handle.clone())?,
        auth_method: decode_auth_method(session.auth_method),
        account_subject: AccountSubjectId::new(session.account_subject.clone())?,
        expires_at: Deadline::from_millis(session.expires_at_monotonic_ms),
        rotation: session.rotation,
        email: decode_optional_email(session.email.as_deref())?,
        display_name: decode_optional_display_name(session.display_name.as_deref())?,
    })
}

/// Converts a browser-validated handle-only network receipt into portable session state.
pub fn decode_session_receipt(
    receipt: &wire::AccountSessionReceipt,
) -> Result<SessionReceipt, AccountWireError> {
    if receipt.expires_at_monotonic_ms == 0 {
        return Err(AccountWireError::InvalidExpiry);
    }
    Ok(SessionReceipt {
        session_handle: SessionHandle::new(receipt.session_handle.clone())?,
        auth_method: decode_auth_method(receipt.auth_method),
        account_subject: AccountSubjectId::new(receipt.account_subject.clone())?,
        expires_at: Deadline::from_millis(receipt.expires_at_monotonic_ms),
        rotation: receipt.rotation,
        email: decode_optional_email(receipt.email.as_deref())?,
        display_name: decode_optional_display_name(receipt.display_name.as_deref())?,
    })
}

/// An address the browser passed on, or nothing.
///
/// Absent and invalid are deliberately different answers. Absent is a provider
/// that confirmed no address, which is ordinary; invalid is a value that
/// reached this boundary claiming to be one and is not, which fails the whole
/// decode rather than being dropped quietly. Dropping it would leave a session
/// whose identity silently disagreed with the account plane's, and nothing
/// downstream could tell that had happened.
fn decode_optional_email(value: Option<&str>) -> Result<Option<AccountEmail>, AccountWireError> {
    value.map(AccountEmail::new).transpose().map_err(Into::into)
}

/// A provider-supplied label the browser passed on, or nothing.
fn decode_optional_display_name(
    value: Option<&str>,
) -> Result<Option<AccountDisplayName>, AccountWireError> {
    value
        .map(AccountDisplayName::new)
        .transpose()
        .map_err(Into::into)
}

fn decode_scopes(scopes: &[wire::AccountScope]) -> Result<Vec<AccountScope>, AccountWireError> {
    if scopes.is_empty() || scopes.len() > wire::MAX_ACCOUNT_SCOPES {
        return Err(AccountWireError::InvalidScope);
    }
    Ok(scopes
        .iter()
        .copied()
        .map(|scope| match scope {
            wire::AccountScope::OpenId => AccountScope::OpenId,
            wire::AccountScope::Email => AccountScope::Email,
            wire::AccountScope::Profile => AccountScope::Profile,
        })
        .collect())
}

fn encode_scopes(scopes: &[AccountScope]) -> Vec<wire::AccountScope> {
    scopes
        .iter()
        .copied()
        .map(|scope| match scope {
            AccountScope::OpenId => wire::AccountScope::OpenId,
            AccountScope::Email => wire::AccountScope::Email,
            AccountScope::Profile => wire::AccountScope::Profile,
        })
        .collect()
}

fn account_response_limit() -> Result<u32, AccountWireError> {
    u32::try_from(wire::MAX_ACCOUNT_RESPONSE_BYTES).map_err(|_| AccountWireError::InvalidEffect)
}

const fn decode_auth_method(method: wire::AccountAuthMethod) -> AccountAuthMethod {
    match method {
        wire::AccountAuthMethod::Google => AccountAuthMethod::Google,
        wire::AccountAuthMethod::EmailLink => AccountAuthMethod::EmailLink,
        wire::AccountAuthMethod::Github => AccountAuthMethod::Github,
        wire::AccountAuthMethod::Facebook => AccountAuthMethod::Facebook,
    }
}

const fn encode_auth_method(method: AccountAuthMethod) -> wire::AccountAuthMethod {
    match method {
        AccountAuthMethod::Google => wire::AccountAuthMethod::Google,
        AccountAuthMethod::EmailLink => wire::AccountAuthMethod::EmailLink,
        AccountAuthMethod::Github => wire::AccountAuthMethod::Github,
        AccountAuthMethod::Facebook => wire::AccountAuthMethod::Facebook,
    }
}

#[cfg(test)]
mod tests {
    use core_service_types as wire;

    use super::{decode_account_session, decode_auth_callback, AccountWireError};

    #[test]
    fn generated_callback_requires_binding_and_exact_code_presence() {
        let callback = wire::AuthCallbackCommand {
            flow_id: "flow-1".to_owned(),
            redirect_binding_id: "product.auth.primary".to_owned(),
            returned_state: "a".repeat(43),
            status: wire::AuthCallbackStatus::AuthorizationCode,
            authorization_code_handle: Some("code-handle".to_owned()),
        };
        assert!(decode_auth_callback(&callback).is_ok());
        let mut malformed = callback;
        malformed.authorization_code_handle = None;
        assert_eq!(
            decode_auth_callback(&malformed),
            Err(AccountWireError::InvalidPresence)
        );
    }

    #[test]
    fn profile_restart_restores_only_bounded_handles() {
        let session = wire::AccountSessionHandle {
            session_handle: "session-handle".to_owned(),
            account_subject: "subject-1".to_owned(),
            expires_at_monotonic_ms: 50_000,
            rotation: 3,
            auth_method: wire::AccountAuthMethod::Github,
            email: None,
            display_name: None,
        };
        assert!(decode_account_session(&session).is_ok());
    }
}
