// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The kernel's view of the provider plane: an opaque handle, and the one
//! question the turn composer asks about it.
//!
//! The plane itself — catalog, credential lifecycle, endpoints — lives above,
//! in the runtime that owns it. The kernel neither holds a credential nor
//! learns anything about one: a [`CredentialHandle`] is the whole of what the
//! core is told about a key.

use core::fmt;

/// Longest accepted opaque credential handle, in bytes.
pub const MAX_CREDENTIAL_HANDLE_BYTES: usize = 128;

/// Why an opaque handle was refused at construction.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CredentialHandleError {
    Empty,
    TooLong,
}

/// An opaque browser secure-store handle standing in for a provider credential.
///
/// This is the whole of what the core is told about a key. `Debug` is
/// deliberately empty: a handle is not secret, but it is the one field on this
/// plane whose accidental appearance in a log would read as a credential and
/// send somebody looking for a leak that did not happen.
#[derive(Clone, PartialEq, Eq, Hash)]
pub struct CredentialHandle(String);

impl CredentialHandle {
    /// Validates one opaque handle.
    pub fn new(value: impl Into<String>) -> Result<Self, CredentialHandleError> {
        let value = value.into();
        if value.is_empty() {
            return Err(CredentialHandleError::Empty);
        }
        if value.len() > MAX_CREDENTIAL_HANDLE_BYTES {
            return Err(CredentialHandleError::TooLong);
        }
        Ok(Self(value))
    }

    /// The handle, for the browser that minted it.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Debug for CredentialHandle {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("CredentialHandle")
            .finish_non_exhaustive()
    }
}

/// Answers the one provider question composition asks: does a usable
/// credential exist for this provider, and what is its handle.
pub trait ProviderDirectory {
    /// A usable credential's handle for `provider_id`, when one exists.
    fn usable_credential(
        &self,
        provider_id: &model_router::ProviderId,
    ) -> Option<&CredentialHandle>;
}
