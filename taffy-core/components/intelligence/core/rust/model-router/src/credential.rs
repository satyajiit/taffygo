// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Credential references, and the secret material they deliberately are not.
//!
//! The router needs to know whether a provider is configured, by what method,
//! and whether that configuration currently works. It never needs the secret,
//! and it must never be able to leak one, so the two are different types with
//! different capabilities:
//!
//! - [`CredentialRef`] is metadata. It serializes, it appears in audit records
//!   and disclosures, and there is no field on it that could hold a secret.
//! - [`SecretMaterial`] is the secret. It does not implement serde traits, its
//!   `Debug` output is a fixed marker, and it overwrites its buffer when
//!   dropped. Reading it is one explicitly named method, so every read site is
//!   greppable.
//!
//! Two rules from the credential contract are encoded here rather than
//! described. Listing configured providers never decrypts anything: a
//! [`CredentialDirectory`] answers from metadata alone. And a credential
//! failure never changes routes — [`CredentialState`] has states for "sign in
//! again" and "refresh failed" precisely so a caller has somewhere to go that
//! is not another provider or another route.

use core::fmt;
use core::sync::atomic::{compiler_fence, Ordering};

use crate::catalog::AuthMethod;
use crate::ids::ProviderId;
use crate::time::Timestamp;

/// How a stored credential authenticates.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum AuthType {
    /// A stored secret sent as a header value.
    ApiKey,
    /// Refreshable tokens from an authorization flow.
    Oauth,
}

impl AuthType {
    /// The catalog auth method this credential satisfies.
    pub fn method(self) -> AuthMethod {
        match self {
            Self::ApiKey => AuthMethod::ApiKey,
            Self::Oauth => AuthMethod::Oauth,
        }
    }
}

/// What the secret store knows about a provider, without decrypting anything.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum CredentialState {
    /// Nothing is stored for this provider.
    Absent,
    /// Stored and usable.
    Usable,
    /// Stored, but the authorization has to be renewed by the user.
    NeedsSignIn,
    /// Stored, and the last refresh failed. The record is preserved: a failed
    /// refresh must not delete a credential.
    RefreshFailed,
}

impl CredentialState {
    /// Whether a request may proceed on this credential.
    pub fn is_usable(self) -> bool {
        matches!(self, Self::Usable)
    }
}

/// Everything the router, an audit record, and a disclosure may know about a
/// credential.
///
/// A stored credential owns its provider: if its method has no usable handler,
/// or a refresh fails, resolution fails visibly. It never falls through to
/// another auth method, and it never changes the route.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct CredentialRef {
    /// The provider this credential is filed under. One credential per
    /// provider; the registry entry and the credential are linked by this key
    /// alone.
    pub provider_id: ProviderId,
    /// The method.
    pub auth_type: AuthType,
    /// The current state.
    pub state: CredentialState,
    /// Whether access is backed by a user plan rather than metered billing.
    pub subscription_backed: bool,
    /// When the record was written.
    pub created_at: Option<Timestamp>,
    /// When it was last replaced.
    pub rotated_at: Option<Timestamp>,
}

impl CredentialRef {
    /// A reference saying nothing is stored for this provider.
    pub fn absent(provider_id: ProviderId, auth_type: AuthType) -> Self {
        Self {
            provider_id,
            auth_type,
            state: CredentialState::Absent,
            subscription_backed: false,
            created_at: None,
            rotated_at: None,
        }
    }
}

/// A secret, held as briefly as possible.
///
/// Decrypted secrets exist only in memory, scoped to a single request or
/// refresh. This type carries no serde implementation, prints a fixed marker,
/// and overwrites its buffer on drop. The overwrite is best effort — without a
/// vetted third-party crate there is no way to bind an optimizer's hands
/// completely — so it is a defence in depth, not a guarantee, and the real
/// control remains the short lifetime.
pub struct SecretMaterial {
    bytes: Vec<u8>,
}

impl SecretMaterial {
    /// Takes ownership of secret bytes.
    pub fn new(bytes: Vec<u8>) -> Self {
        Self { bytes }
    }

    /// The secret, for the one adapter call that builds a request header.
    ///
    /// Named to be conspicuous: every call site is a place a secret could
    /// escape, and there should be very few of them.
    pub fn expose_for_request(&self) -> &[u8] {
        &self.bytes
    }

    /// Length in bytes, which is not secret and is useful in a defect report.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Whether the material is empty, as a keyless local server's would be.
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }
}

impl fmt::Debug for SecretMaterial {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("SecretMaterial(redacted)")
    }
}

impl Drop for SecretMaterial {
    fn drop(&mut self) {
        for byte in &mut self.bytes {
            *byte = 0;
        }
        compiler_fence(Ordering::SeqCst);
    }
}

/// The non-secret half of attaching a credential to a request.
///
/// The router says which credential applies and which header names carry it.
/// The value is fetched by the adapter, from the secret store, at the moment
/// the request is built — so nothing that is logged, audited, or rendered has
/// ever held it.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct AuthAttachment {
    /// Which credential applies.
    pub credential: CredentialRef,
    /// Header names the adapter will fill from the secret store.
    pub secret_header_names: Vec<String>,
    /// Non-secret headers the provider entry declares.
    pub static_header_names: Vec<String>,
}

/// Metadata-only view of the secret store.
///
/// Listing configured providers must not decrypt secrets, so this trait cannot
/// return any.
pub trait CredentialDirectory {
    /// What is stored for `provider_id`.
    fn credential(&self, provider_id: &ProviderId) -> CredentialRef;
}
