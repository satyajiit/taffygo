// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable provider credentials and a person's own providers.
//!
//! The plane holds references and never material: a credential is an opaque
//! handle the browser's secure store minted, and no type here has a field that
//! could hold a key. It performs no I/O, reads no clock, and enters no Mojo,
//! so every decision it reaches is reproducible from its own state.
//!
//! What is deliberately absent is subscription sign-in. Decision 0049 records
//! that a credential crosses the contract as a reference; it leaves open, as
//! OD-103, who runs a provider's OAuth flow. That question is not academic
//! here: `OAuthSurfaceRequest` is typed on `AccountAuthMethod` and every member
//! of `AccountNetworkOperation` names an account operation, so a provider flow
//! planned in portable Rust would need sibling records on the core-service
//! contract that do not exist. This module stops at the boundary of what is
//! decided rather than inventing them.

mod error;
mod identity;
mod messages;
mod protocol;
mod status_projection;

pub use self::error::ProviderError;
pub use self::identity::{
    CredentialHandle, ProviderDisplayName, ProviderId, MAX_CUSTOM_MODELS, MAX_CUSTOM_PROVIDERS,
    MAX_PROVIDER_DISPLAY_NAME_BYTES, MAX_PROVIDER_ID_BYTES,
};
pub use self::messages::{
    CatalogModel, CustomModel, CustomProvider, ProviderAuthMethod, ProviderCredential,
    ProviderEffect, ProviderModelPreference, ProviderOrigin, ProviderPresentation, ProviderRefusal,
    ProviderRefusalKind, ProviderView, ProviderWireApi, StoredCredentialView,
};
pub use self::protocol::custom::SavedCustomProvider;
pub use self::protocol::{CatalogProvider, ProviderProtocol, MAX_PENDING_SIGN_IN_FLOWS};
pub use self::status_projection::ProviderStatusProjection;

#[cfg(test)]
mod tests;
