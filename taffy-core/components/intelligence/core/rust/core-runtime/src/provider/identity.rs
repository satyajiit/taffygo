// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded provider identities and the opaque handles that stand in for keys.
//!
//! The contract can express a length and not an alphabet, so the narrower of
//! the two sides is expressed here: the Android secure store accepts
//! `[a-z0-9][a-z0-9-]{0,63}` for a provider record, and a value the platform
//! cannot honour would otherwise fail at the last hop as a generic "could not
//! be changed on this device". Refusing it here names the real reason.

use core::fmt;

use super::ProviderError;

/// Longest provider identity either side of the seam accepts.
///
/// 64 rather than the router's 128: see the module note. `core_service_types`
/// declares the same number, and the two are compared by a test rather than by
/// a comment.
pub const MAX_PROVIDER_ID_BYTES: usize = core_service_types::MAX_PROVIDER_ID_BYTES;
/// Longest shown name for one of a person's own providers.
pub const MAX_PROVIDER_DISPLAY_NAME_BYTES: usize =
    core_service_types::MAX_PROVIDER_DISPLAY_NAME_BYTES;
/// Most providers one person may define beyond the catalog.
pub const MAX_CUSTOM_PROVIDERS: usize = core_service_types::MAX_CUSTOM_PROVIDERS;
/// Most models one of a person's own providers may arrive with.
///
/// Read from the contract rather than restated, for the reason every other
/// bound here is: a number typed twice is a number that can disagree with
/// itself, and the disagreement would show up as a save the browser thought it
/// was allowed to send.
pub const MAX_CUSTOM_MODELS: usize = core_service_types::MAX_CUSTOM_MODEL_ENTRIES;

/// A validated provider identity.
///
/// One credential per provider, and this is the whole key. Never
/// `(provider_id, auth_method)`: a composite key would let one vendor hold two
/// credentials with two disclosure classes and make "which one applies" a
/// lookup rather than a fact, which decisions 0015 and 0029 both forbid.
///
/// It wraps the router's own key rather than a `String`, so handing a provider
/// to the router is infallible. The alternative was a conversion at the wiring
/// point that cannot fail in practice and still has to be written as if it
/// could, and whose plausible body — drop the entry — would mean a credential
/// a person saved never reaching routing, with nothing to see.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ProviderId(model_router::ProviderId);

impl ProviderId {
    /// Validates one provider identity against the narrower of the two sides.
    pub fn new(value: impl Into<String>) -> Result<Self, ProviderError> {
        let value = value.into();
        if value.is_empty() {
            return Err(ProviderError::EmptyProviderId);
        }
        if value.len() > MAX_PROVIDER_ID_BYTES {
            return Err(ProviderError::ProviderIdTooLong);
        }
        let mut bytes = value.bytes();
        // The first byte may not be the separator, so an identity can never be
        // confused with a prefix or sort ahead of every real one.
        match bytes.next() {
            Some(byte) if byte.is_ascii_lowercase() || byte.is_ascii_digit() => {}
            _ => return Err(ProviderError::InvalidProviderId),
        }
        for byte in bytes {
            if !(byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'-') {
                return Err(ProviderError::InvalidProviderId);
            }
        }
        // This alphabet is a strict subset of the router's, so the only way
        // past the loop above and into this error is the two drifting apart.
        let router =
            model_router::ProviderId::new(&value).map_err(|_| ProviderError::InvalidProviderId)?;
        Ok(Self(router))
    }

    /// The identity, for a lookup or for a catalog key.
    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }

    /// The same identity as the router's own key.
    pub fn as_router(&self) -> &model_router::ProviderId {
        &self.0
    }
}

impl fmt::Display for ProviderId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.0.as_str())
    }
}

/// The name shown for one of a person's own providers.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProviderDisplayName(String);

impl ProviderDisplayName {
    /// Validates one shown name.
    pub fn new(value: impl Into<String>) -> Result<Self, ProviderError> {
        let value = value.into();
        if value.trim().is_empty() {
            return Err(ProviderError::EmptyDisplayName);
        }
        if value.len() > MAX_PROVIDER_DISPLAY_NAME_BYTES {
            return Err(ProviderError::DisplayNameTooLong);
        }
        Ok(Self(value))
    }

    /// The name, for a surface.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// The opaque credential handle is the loop kernel's type (decision 0072);
/// this plane keeps the path and the plane-shaped error spelling.
pub use loop_kernel::provider::CredentialHandle;

impl From<loop_kernel::provider::CredentialHandleError> for ProviderError {
    fn from(error: loop_kernel::provider::CredentialHandleError) -> Self {
        match error {
            loop_kernel::provider::CredentialHandleError::Empty => Self::EmptyCredentialHandle,
            loop_kernel::provider::CredentialHandleError::TooLong => Self::CredentialHandleTooLong,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn provider_id_accepts_the_catalog_spellings() {
        for id in ["anthropic", "openai", "google-ai-studio", "xai", "x1"] {
            assert!(ProviderId::new(id).is_ok(), "{id} should be accepted");
        }
    }

    #[test]
    fn provider_id_refuses_what_the_android_store_would() {
        // Every one of these is inside the contract's length bound and outside
        // the platform's alphabet, which is the whole reason this type exists.
        for id in ["Anthropic", "my_gateway", "my.gateway", "-leading", "a b"] {
            assert_eq!(
                ProviderId::new(id),
                Err(ProviderError::InvalidProviderId),
                "{id} should be refused"
            );
        }
    }

    #[test]
    fn provider_id_bound_is_the_narrower_of_the_two_sides() {
        assert_eq!(MAX_PROVIDER_ID_BYTES, 64);
        const {
            assert!(
                MAX_PROVIDER_ID_BYTES < model_router::ids::MAX_KEY_LEN,
                "the contract must take the narrower bound, not the router's"
            );
        }
        assert!(ProviderId::new("a".repeat(MAX_PROVIDER_ID_BYTES)).is_ok());
        assert_eq!(
            ProviderId::new("a".repeat(MAX_PROVIDER_ID_BYTES + 1)),
            Err(ProviderError::ProviderIdTooLong)
        );
    }

    #[test]
    fn every_accepted_identity_is_also_a_router_key() {
        // The router's alphabet is a superset of this one, so a value that
        // reaches the router can never be refused there. If that stops being
        // true this test fails rather than a route silently disappearing.
        for id in ["anthropic", "google-ai-studio", &"z9-".repeat(21)] {
            let ours = ProviderId::new(id).expect("accepted here");
            assert!(
                model_router::ProviderId::new(ours.as_str()).is_ok(),
                "{id} is accepted here and refused by the router"
            );
        }
    }

    #[test]
    fn a_credential_handle_does_not_print_itself() {
        let handle = CredentialHandle::new("handle-1").expect("valid");
        assert_eq!(format!("{handle:?}"), "CredentialHandle { .. }");
    }

    #[test]
    fn display_name_refuses_blank_and_overlong() {
        assert_eq!(
            ProviderDisplayName::new("   "),
            Err(ProviderError::EmptyDisplayName)
        );
        assert_eq!(
            ProviderDisplayName::new("n".repeat(MAX_PROVIDER_DISPLAY_NAME_BYTES + 1)),
            Err(ProviderError::DisplayNameTooLong)
        );
    }
}
