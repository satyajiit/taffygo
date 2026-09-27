// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A person's own providers, written and discarded whole (decision 0096).
//!
//! Split from the plane's own file for the same reason the preferences were: a
//! catalog credential and an endpoint somebody typed are different subjects
//! that happen to share a plane. This half owns the one write that defines a
//! provider, everything that write must carry with it, and the reads the
//! router's own layer is built from.
//!
//! One write, and everything in it. Decision 0096 section 4 is the whole shape
//! of this file: a saved endpoint arrives with the models the probe found and
//! the runtime it named, because a provider filed with nothing behind it is a
//! row a person can see, select and never reach. Saving them separately would
//! make that unreachable state a real one somebody would eventually be left in.

use model_router::catalog::Endpoint;
use model_router::wire::ServerKind;
use model_router::CredentialState;

use crate::provider::identity::{
    CredentialHandle, ProviderDisplayName, ProviderId, MAX_CUSTOM_MODELS, MAX_CUSTOM_PROVIDERS,
};
use crate::provider::messages::{
    CustomModel, CustomProvider, ProviderAuthMethod, ProviderCredential, ProviderEffect,
    ProviderWireApi,
};
use crate::provider::ProviderError;

use super::ProviderProtocol;

/// Everything one save states about a person's own provider.
///
/// A record rather than seven arguments, because the fields are one statement
/// and a caller that could supply some of them is a caller that could file a
/// provider halfway. It is the plane's own shape, not the contract's: the
/// conversion happens at the seam so this module keeps holding no wire type.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SavedCustomProvider {
    /// The identity the person's provider is filed under.
    pub provider_id: ProviderId,
    /// The name shown for it.
    pub display_name: ProviderDisplayName,
    /// The validated endpoint the person supplied.
    pub endpoint: Endpoint,
    /// The request family that endpoint speaks.
    pub wire_api: ProviderWireApi,
    /// The opaque handle for its key, absent when the endpoint needs none.
    pub credential: Option<CredentialHandle>,
    /// The models the endpoint listed, in the order it listed them.
    pub models: Vec<CustomModel>,
    /// What the endpoint turned out to be, absent when nothing was detected.
    pub detected_server: Option<ServerKind>,
}

impl ProviderProtocol {
    /// Defines or replaces one of a person's own providers in a single write.
    ///
    /// Nothing is written until every bound has been checked, so a refused save
    /// leaves the provider that was there exactly as it was — including the
    /// models it already had, which a half-applied replacement would have
    /// emptied.
    pub fn save_custom_provider(
        &mut self,
        saved: SavedCustomProvider,
    ) -> Result<ProviderEffect, ProviderError> {
        let SavedCustomProvider {
            provider_id,
            display_name,
            endpoint,
            wire_api,
            credential,
            models,
            detected_server,
        } = saved;
        if self.catalog.contains_key(&provider_id) {
            return Err(ProviderError::ProviderIdReserved);
        }
        let replacing = self.custom.contains_key(&provider_id);
        if !replacing && self.custom.len() >= MAX_CUSTOM_PROVIDERS {
            return Err(ProviderError::TooManyCustomProviders);
        }
        if models.len() > MAX_CUSTOM_MODELS {
            return Err(ProviderError::TooManyCustomModels);
        }
        let previous_handle = self
            .custom
            .insert(
                provider_id.clone(),
                CustomProvider {
                    provider_id: provider_id.clone(),
                    display_name,
                    endpoint,
                    wire_api,
                    credential: credential.clone(),
                    models,
                    detected_server,
                },
            )
            .and_then(|entry| entry.credential);
        match credential {
            Some(handle) => {
                self.credentials.insert(
                    provider_id.clone(),
                    ProviderCredential {
                        provider_id,
                        auth_method: ProviderAuthMethod::ApiKey,
                        handle,
                        state: CredentialState::Usable,
                    },
                );
            }
            None => {
                self.credentials.remove(&provider_id);
            }
        }
        let effect = Self::release_if_orphaned(previous_handle, self.credentials.values());
        self.status_changed();
        Ok(effect)
    }

    /// Discards one of a person's own providers together with its credential.
    pub fn remove_custom_provider(
        &mut self,
        provider_id: &ProviderId,
    ) -> Result<ProviderEffect, ProviderError> {
        let removed = self
            .custom
            .remove(provider_id)
            .ok_or(ProviderError::UnknownProvider)?;
        self.credentials.remove(provider_id);
        let effect = match removed.credential {
            Some(handle) => ProviderEffect::ReleaseHandle(handle),
            None => ProviderEffect::None,
        };
        self.status_changed();
        Ok(effect)
    }

    /// Every endpoint a person runs themselves, for the router's override layer.
    pub fn custom_endpoints(&self) -> Vec<Endpoint> {
        self.custom
            .values()
            .map(|entry| entry.endpoint.clone())
            .collect()
    }

    /// Every provider a person defined, in identity order.
    pub fn custom_providers(&self) -> impl Iterator<Item = &CustomProvider> {
        self.custom.values()
    }

    /// A replaced handle is released only when nothing else still names it.
    fn release_if_orphaned<'a>(
        previous: Option<CredentialHandle>,
        live: impl Iterator<Item = &'a ProviderCredential>,
    ) -> ProviderEffect {
        let Some(handle) = previous else {
            return ProviderEffect::None;
        };
        for entry in live {
            if entry.handle == handle {
                return ProviderEffect::None;
            }
        }
        ProviderEffect::ReleaseHandle(handle)
    }
}
