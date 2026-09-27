// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded account identities, secure handles, and PKCE public values.

use core::fmt;

use super::crypto::{encode_base64url, is_base64url};
use super::AccountError;

/// Exact secure-random input requested for one state and one PKCE verifier.
pub const AUTHORIZATION_ENTROPY_BYTES: usize = 64;
/// Exact secure-random input encoded into one raw Google nonce.
pub const GOOGLE_NONCE_ENTROPY_BYTES: usize = core_service_types::GOOGLE_NONCE_ENTROPY_BYTES;

const MIN_STATE_BYTES: usize = 32;
const MAX_STATE_BYTES: usize = 128;
const MAX_HANDLE_BYTES: usize = 128;
const PKCE_S256_CHALLENGE_BYTES: usize = 43;
const GOOGLE_NONCE_HASH_HEX_BYTES: usize = core_service_types::GOOGLE_NONCE_HASH_HEX_BYTES;
const REDIRECT_BINDING_ID_BYTES: usize = 64;
const MAX_ACCOUNT_EMAIL_BYTES: usize = 320;
const MAX_ACCOUNT_DISPLAY_NAME_BYTES: usize = 128;

/// The account backend selected by product configuration, never by a caller URL.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AccountProvider {
    /// The pinned Supabase account plane.
    Supabase,
}

/// Closed person-visible account method routed through the pinned account plane.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AccountAuthMethod {
    /// A native Google credential exchanged by the browser account broker.
    Google,
    /// A one-time email link completed through the registered redirect binding.
    EmailLink,
    /// A GitHub OAuth flow brokered by the pinned account plane.
    Github,
    /// A Facebook OAuth flow brokered by the pinned account plane.
    Facebook,
}

/// Fixed product endpoint lookup; adapters map this to pinned configuration.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AccountEndpointId {
    /// Supabase's configured authorization endpoint.
    SupabaseAuthorize,
    /// Supabase's configured code/refresh token endpoint.
    SupabaseToken,
    /// Supabase's configured session revocation endpoint.
    SupabaseRevoke,
}

/// A closed authorization scope.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum AccountScope {
    /// Stable account identity.
    OpenId,
    /// The account email claim.
    Email,
    /// Display profile claims.
    Profile,
}

/// Opaque key for an exact callback registered by product configuration.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RedirectBindingId(String);

impl RedirectBindingId {
    /// Validates a product-owned identifier rather than a callback URI.
    pub fn new(value: impl Into<String>) -> Result<Self, AccountError> {
        let value = value.into();
        if value.is_empty() {
            return Err(AccountError::EmptyRedirectBinding);
        }
        if value.len() > REDIRECT_BINDING_ID_BYTES {
            return Err(AccountError::RedirectBindingTooLong);
        }
        if !value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
        {
            return Err(AccountError::InvalidRedirectBinding);
        }
        Ok(Self(value))
    }

    /// Product configuration lookup key.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

macro_rules! public_opaque_id {
    ($name:ident, $empty:ident, $long:ident) => {
        #[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name(String);

        impl $name {
            /// Validates an opaque, non-secret identifier.
            pub fn new(value: impl Into<String>) -> Result<Self, AccountError> {
                let value = value.into();
                if value.is_empty() {
                    return Err(AccountError::$empty);
                }
                if value.len() > MAX_HANDLE_BYTES {
                    return Err(AccountError::$long);
                }
                Ok(Self(value))
            }

            /// The opaque value used for correlation only.
            pub fn as_str(&self) -> &str {
                &self.0
            }
        }

        impl fmt::Debug for $name {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter
                    .debug_struct(stringify!($name))
                    .finish_non_exhaustive()
            }
        }
    };
}

public_opaque_id!(AuthFlowId, EmptyFlowId, FlowIdTooLong);
public_opaque_id!(AccountSubjectId, EmptyAccountSubject, AccountSubjectTooLong);

/// Bounded canonical email-link destination retained only for the active flow.
#[derive(Clone, PartialEq, Eq)]
pub struct AccountEmail(String);

impl AccountEmail {
    /// Validates a conservative ASCII mailbox shape without normalizing identity.
    pub fn new(value: impl Into<String>) -> Result<Self, AccountError> {
        let value = value.into();
        if value.is_empty() || value.len() > MAX_ACCOUNT_EMAIL_BYTES || !value.is_ascii() {
            return Err(AccountError::InvalidEmail);
        }
        if value
            .bytes()
            .any(|byte| byte.is_ascii_control() || byte.is_ascii_whitespace())
        {
            return Err(AccountError::InvalidEmail);
        }
        let mut parts = value.split('@');
        let local = parts.next().unwrap_or_default();
        let domain = parts.next().unwrap_or_default();
        if local.is_empty()
            || domain.is_empty()
            || !domain.contains('.')
            || parts.next().is_some()
            || domain.starts_with('.')
            || domain.ends_with('.')
        {
            return Err(AccountError::InvalidEmail);
        }
        Ok(Self(value))
    }

    /// Validated address supplied to the fixed email-link route only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// The name a person chose, as their provider reports it.
///
/// Bounded and stripped of nothing. This is a label and never an identifier:
/// two accounts may carry the same one, it may change under the person at any
/// time, and no decision may be taken on it. It is validated rather than
/// sanitized because the only safe treatment of provider-supplied text is to
/// refuse the shapes that are not a name and then to render the rest as text —
/// a value trimmed into acceptability is a value the product has edited on
/// somebody's behalf.
///
/// Control characters are refused because a name carrying a line break or a
/// bidirectional override is not a name, and because the one place this is
/// shown is a sentence with other words in it. Everything else printable is
/// allowed, including every script: an ASCII-only rule here would refuse most
/// of the world's names, which is a defect and not a safety measure.
#[derive(Clone, PartialEq, Eq)]
pub struct AccountDisplayName(String);

impl AccountDisplayName {
    /// Validates one bounded provider-supplied label.
    pub fn new(value: impl Into<String>) -> Result<Self, AccountError> {
        let value = value.into();
        if value.is_empty() || value.len() > MAX_ACCOUNT_DISPLAY_NAME_BYTES {
            return Err(AccountError::InvalidDisplayName);
        }
        if value.chars().any(char::is_control) || value.trim().is_empty() {
            return Err(AccountError::InvalidDisplayName);
        }
        Ok(Self(value))
    }

    /// The validated label, for display and for nothing else.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Debug for AccountDisplayName {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("AccountDisplayName")
            .finish_non_exhaustive()
    }
}

impl fmt::Debug for AccountEmail {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("AccountEmail")
            .finish_non_exhaustive()
    }
}

macro_rules! secret_handle {
    ($name:ident, $empty:ident, $long:ident) => {
        #[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name(String);

        impl $name {
            /// Validates a browser-owned secure-store reference, not its value.
            pub fn new(value: impl Into<String>) -> Result<Self, AccountError> {
                let value = value.into();
                if value.is_empty() {
                    return Err(AccountError::$empty);
                }
                if value.len() > MAX_HANDLE_BYTES {
                    return Err(AccountError::$long);
                }
                Ok(Self(value))
            }

            /// The opaque lookup key. It is not credential material.
            pub fn opaque_id(&self) -> &str {
                &self.0
            }
        }

        impl fmt::Debug for $name {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter
                    .debug_struct(stringify!($name))
                    .finish_non_exhaustive()
            }
        }
    };
}

secret_handle!(SecretHandle, EmptySecretHandle, SecretHandleTooLong);
secret_handle!(
    AuthorizationCodeHandle,
    EmptyAuthorizationCodeHandle,
    AuthorizationCodeHandleTooLong
);
secret_handle!(SessionHandle, EmptySessionHandle, SessionHandleTooLong);

/// Browser-supplied secure entropy consumed by Rust to derive state and PKCE.
#[derive(Clone, PartialEq, Eq)]
pub struct AuthorizationEntropy(pub(super) [u8; AUTHORIZATION_ENTROPY_BYTES]);

impl AuthorizationEntropy {
    /// Wraps the exact amount requested by the account protocol.
    pub const fn new(bytes: [u8; AUTHORIZATION_ENTROPY_BYTES]) -> Self {
        Self(bytes)
    }
}

/// Browser-supplied secure entropy consumed into one Google nonce pair.
#[derive(Clone, PartialEq, Eq)]
pub struct GoogleNonceEntropy(pub(super) [u8; GOOGLE_NONCE_ENTROPY_BYTES]);

impl GoogleNonceEntropy {
    /// Wraps the exact amount requested by the portable Google flow.
    pub const fn new(bytes: [u8; GOOGLE_NONCE_ENTROPY_BYTES]) -> Self {
        Self(bytes)
    }
}

impl Drop for GoogleNonceEntropy {
    fn drop(&mut self) {
        self.0.fill(0);
    }
}

impl fmt::Debug for GoogleNonceEntropy {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("GoogleNonceEntropy")
            .finish_non_exhaustive()
    }
}

impl fmt::Debug for AuthorizationEntropy {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("AuthorizationEntropy")
            .finish_non_exhaustive()
    }
}

impl Drop for AuthorizationEntropy {
    fn drop(&mut self) {
        self.0.fill(0);
    }
}

/// Transient verifier bytes sent only to the browser secure-store adapter.
#[derive(Clone, PartialEq, Eq)]
pub struct PkceVerifierMaterial(pub(super) Vec<u8>);

impl PkceVerifierMaterial {
    /// Consumes the transient value at the secure-store boundary.
    pub fn into_bytes(mut self) -> Vec<u8> {
        core::mem::take(&mut self.0)
    }
}

impl Drop for PkceVerifierMaterial {
    fn drop(&mut self) {
        self.0.fill(0);
        self.0.clear();
    }
}

impl fmt::Debug for PkceVerifierMaterial {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PkceVerifierMaterial")
            .finish_non_exhaustive()
    }
}

/// Transient raw nonce bytes sent only to the browser secure-store adapter.
#[derive(Clone, PartialEq, Eq)]
pub struct GoogleRawNonceMaterial(pub(super) Vec<u8>);

impl GoogleRawNonceMaterial {
    /// Consumes the transient value at the secure-store boundary.
    pub fn into_bytes(mut self) -> Vec<u8> {
        core::mem::take(&mut self.0)
    }
}

impl Drop for GoogleRawNonceMaterial {
    fn drop(&mut self) {
        self.0.fill(0);
        self.0.clear();
    }
}

impl fmt::Debug for GoogleRawNonceMaterial {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("GoogleRawNonceMaterial")
            .finish_non_exhaustive()
    }
}

/// Public SHA-256 hexadecimal value bound into Google's ID-token request.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GoogleNonceHash(String);

impl GoogleNonceHash {
    pub(super) fn from_digest(digest: &[u8]) -> Result<Self, AccountError> {
        if digest.len() != GOOGLE_NONCE_ENTROPY_BYTES {
            return Err(AccountError::CryptoInvariant);
        }
        let mut value = String::with_capacity(GOOGLE_NONCE_HASH_HEX_BYTES);
        for byte in digest {
            use core::fmt::Write as _;
            write!(&mut value, "{byte:02x}").map_err(|_| AccountError::CryptoInvariant)?;
        }
        if value.len() != GOOGLE_NONCE_HASH_HEX_BYTES {
            return Err(AccountError::CryptoInvariant);
        }
        Ok(Self(value))
    }

    /// Lowercase SHA-256 hexadecimal sent to the native Google surface.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// CSRF state generated from browser-owned secure randomness.
#[derive(Clone, PartialEq, Eq)]
pub struct RedirectState(String);

impl RedirectState {
    /// Accepts unpadded base64url state within the protocol bound.
    pub fn new(value: impl Into<String>) -> Result<Self, AccountError> {
        let value = value.into();
        if value.len() < MIN_STATE_BYTES {
            return Err(AccountError::StateTooShort);
        }
        if value.len() > MAX_STATE_BYTES {
            return Err(AccountError::StateTooLong);
        }
        if !is_base64url(&value) {
            return Err(AccountError::InvalidStateEncoding);
        }
        Ok(Self(value))
    }

    /// Compares state without an early exit on its bytes.
    pub fn matches(&self, received: &Self) -> bool {
        if self.0.len() != received.0.len() {
            return false;
        }
        self.0
            .bytes()
            .zip(received.0.bytes())
            .fold(0_u8, |difference, (left, right)| {
                difference | (left ^ right)
            })
            == 0
    }

    /// Derived state sent to the pinned authorization endpoint.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Debug for RedirectState {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("RedirectState")
            .finish_non_exhaustive()
    }
}

/// An S256 PKCE challenge. The verifier remains behind a secure handle.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PkceChallenge(String);

impl PkceChallenge {
    pub(super) fn from_digest(digest: &[u8]) -> Result<Self, AccountError> {
        let value = encode_base64url(digest)?;
        if value.len() != PKCE_S256_CHALLENGE_BYTES {
            return Err(AccountError::CryptoInvariant);
        }
        Ok(Self(value))
    }

    /// The public challenge sent to the pinned authorization endpoint.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}
