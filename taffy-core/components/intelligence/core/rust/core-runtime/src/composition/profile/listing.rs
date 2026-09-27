// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A connected provider's own model list, on the composition surface
//! (decision 0098).
//!
//! The same two legs the served catalog has: the browser pokes and the core
//! decides whether anything is due, and the browser delivers what it fetched
//! and the core alone decides what it means. What is here rather than in
//! [`crate::provider_listing`] is the half that needs to see both planes at
//! once — a provider is a candidate only when the merged catalog says its
//! models are served *and* the provider plane holds a credential to ask with,
//! and neither half knows the other.

use model_router::catalog::{AuthMethod, ModelSource};
use model_router::CredentialState;

use crate::provider_listing::{ListingCandidate, ProviderListingVerdict};
use crate::wire;

use super::provider::wire_api_of;
use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Plans one listing fetch if any connected provider is due.
    ///
    /// Nothing is planned while a fetch is in flight, and nothing is planned
    /// for a provider with no credential — an aggregator's list is
    /// per-account, so before there is a credential there is nothing to ask
    /// for and the row says plainly that connecting is what fills it.
    pub fn plan_provider_listing_refresh(
        &mut self,
        now_utc_ms: u64,
    ) -> Option<wire::EffectEnvelope> {
        if self.listings.in_flight() {
            return None;
        }
        let candidates = self.listing_candidates();
        let generation = self.core.service_generation().value();
        self.listings
            .begin_refresh(&candidates, generation, now_utc_ms)
    }

    /// Judges one delivered listing and installs what it accepts.
    ///
    /// Acceptance rebuilds the merge and re-installs everything that reads it,
    /// so a fetched model is a merged-catalog model and reaches every surface
    /// through the roster like any other. The caller republishes status exactly
    /// when this answers `Accepted`, which is how a picker hears that a
    /// provider that had nothing under it now has something.
    pub fn deliver_provider_listing_result(
        &mut self,
        result: &wire::ProviderListingFetchResult,
        now_utc_ms: u64,
    ) -> ProviderListingVerdict {
        let merged = self.merged_catalog();
        let verdict =
            self.listings
                .deliver_fetch_result(&self.catalog_baseline, &merged, result, now_utc_ms);
        if matches!(verdict, ProviderListingVerdict::Accepted { .. }) {
            self.reinstall_catalog_state();
        }
        verdict
    }

    /// Drops every listing whose provider no longer has a usable credential.
    ///
    /// Stated as a fact about the planes rather than as a reaction to a
    /// particular command, so it holds however the credential went — forgotten,
    /// replaced by a save that refused, or taken away with the provider itself.
    /// Returns whether anything was dropped.
    pub(super) fn forget_unconnected_listings(&mut self) -> bool {
        let connected: Vec<String> = self
            .providers
            .credentials()
            .filter(|credential| credential.state == CredentialState::Usable)
            .map(|credential| credential.provider_id.as_str().to_owned())
            .collect();
        let departed: Vec<String> = self
            .listings
            .listed_providers()
            .filter(|provider_id| !connected.iter().any(|held| held == provider_id))
            .map(str::to_owned)
            .collect();
        let mut dropped = false;
        for provider_id in departed {
            dropped |= self.listings.forget(&provider_id);
        }
        dropped
    }

    /// Every provider whose list may be fetched right now.
    ///
    /// In the merge's own provider order, which is what makes "which one is
    /// asked first" a property of the catalog rather than of the moment. The
    /// endpoint and the family are the ones the credential in play reaches, not
    /// the row's defaults: a vendor a subscription reaches differently would
    /// otherwise be asked for its models at the address a key is spent at.
    fn listing_candidates(&self) -> Vec<ListingCandidate> {
        let merged = self.merged_catalog();
        let mut candidates = Vec::new();
        for credential in self.providers.credentials() {
            if credential.state != CredentialState::Usable {
                continue;
            }
            let Some(provider) = merged.provider(credential.provider_id.as_router()) else {
                continue;
            };
            if !provider.enabled || provider.model_source != ModelSource::DynamicListing {
                continue;
            }
            let method = match credential.auth_method {
                crate::provider::ProviderAuthMethod::ApiKey => AuthMethod::ApiKey,
                crate::provider::ProviderAuthMethod::Oauth => AuthMethod::Oauth,
            };
            candidates.push(ListingCandidate {
                provider_id: credential.provider_id.as_str().to_owned(),
                endpoint: provider.endpoint_for(method).as_str().to_owned(),
                wire_api: wire_api_of(provider.wire_api_for(method)),
                credential_handle: credential.handle.as_str().to_owned(),
            });
        }
        candidates
    }
}
