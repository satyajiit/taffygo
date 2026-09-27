// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical provider-neutral routing over the compiled catalog.

use std::collections::BTreeMap;

use model_router::catalog::Endpoint;
use model_router::credential::{AuthType, CredentialDirectory, CredentialRef};
use model_router::ids::{ModelKey, ProviderId};
use model_router::route::{EndpointClassifier, ManagedEntitlement, ModelPolicy, RouteRequest};
use model_router::{
    embedded_baseline, MergedCatalog, RoutePlan, RouteRefusal, RouteSelector, TaskLedger,
};

use crate::ports::ModelRouterPort;

/// Why the compiled production router could not be constructed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ModelRouterBuildError {
    /// The compiled catalog did not decode.
    InvalidEmbeddedCatalog,
    /// Decoding discarded one or more entries.
    EmbeddedCatalogDefects,
    /// The decoded catalog broke a cross-entry invariant.
    EmbeddedCatalogViolations,
}

#[derive(Clone, Debug, Default)]
struct CredentialMetadata {
    entries: BTreeMap<ProviderId, CredentialRef>,
}

impl CredentialDirectory for CredentialMetadata {
    fn credential(&self, provider_id: &ProviderId) -> CredentialRef {
        self.entries.get(provider_id).cloned().unwrap_or_else(|| {
            // The method is immaterial while state is ABSENT; route selection
            // refuses before it can build an attachment.
            CredentialRef::absent(provider_id.clone(), AuthType::ApiKey)
        })
    }
}

#[derive(Clone, Debug, Default)]
struct LocalEndpointRegistry {
    endpoints: Vec<Endpoint>,
}

impl EndpointClassifier for LocalEndpointRegistry {
    fn is_local_endpoint(&self, endpoint: &Endpoint) -> bool {
        self.endpoints.contains(endpoint)
    }
}

/// Real model router initialized from the signed, compiled baseline.
///
/// Account entitlement, credential metadata, and reviewed local endpoints are
/// standing typed configuration. Their initial empty state fails closed; no
/// secret, URL loader, network handle, or provider body enters this adapter.
#[derive(Clone, Debug)]
pub struct ProductionModelRouter {
    catalog: MergedCatalog,
    credentials: CredentialMetadata,
    entitlement: ManagedEntitlement,
    policy: ModelPolicy,
    local_endpoints: LocalEndpointRegistry,
}

impl ProductionModelRouter {
    /// Validates the exact catalog compiled into this browser build.
    pub fn from_embedded_catalog() -> Result<Self, ModelRouterBuildError> {
        let (parsed, violations) =
            embedded_baseline().map_err(|_| ModelRouterBuildError::InvalidEmbeddedCatalog)?;
        if !parsed.is_clean() {
            return Err(ModelRouterBuildError::EmbeddedCatalogDefects);
        }
        if !violations.is_empty() {
            return Err(ModelRouterBuildError::EmbeddedCatalogViolations);
        }
        Ok(Self {
            catalog: MergedCatalog::from_baseline(&parsed.document),
            credentials: CredentialMetadata::default(),
            entitlement: ManagedEntitlement::default(),
            policy: ModelPolicy::new(),
            local_endpoints: LocalEndpointRegistry::default(),
        })
    }

    /// Replaces non-secret credential metadata obtained through a typed
    /// profile configuration completion.
    pub fn replace_credentials(&mut self, entries: impl IntoIterator<Item = CredentialRef>) {
        self.credentials.entries.clear();
        for entry in entries {
            self.credentials
                .entries
                .insert(entry.provider_id.clone(), entry);
        }
    }

    /// Replaces the backend-issued managed-route entitlement.
    pub fn set_entitlement(&mut self, entitlement: ManagedEntitlement) {
        self.entitlement = entitlement;
    }

    /// Replaces the frozen per-role assistant routing preference.
    pub fn set_policy(&mut self, policy: ModelPolicy) {
        self.policy = policy;
    }

    /// Replaces reviewed endpoint descriptors known to be user-run locally.
    pub fn replace_local_endpoints(&mut self, endpoints: Vec<Endpoint>) {
        self.local_endpoints.endpoints = endpoints;
    }

    /// Every provider the compiled catalog files, in identity order.
    ///
    /// The provider plane is built over these rather than over a second list,
    /// because "which identities are reserved" and "which vendor offers which
    /// method" have to be the same answer here and in routing. A second copy
    /// would be a vocabulary with no gate comparing it to this one.
    pub fn catalog_providers(
        &self,
    ) -> impl Iterator<Item = &model_router::catalog::merge::ProviderEntry> {
        self.catalog.providers()
    }

    /// The merged snapshot this router is currently selecting over.
    ///
    /// Read-only, and read by the composition that derives the provider plane
    /// from it: the models a person chooses between and the models routing
    /// resolves have to be the same set, and a second list built beside this
    /// one would be a correspondence nothing compares.
    pub const fn catalog(&self) -> &MergedCatalog {
        &self.catalog
    }
}

impl ModelRouterPort for ProductionModelRouter {
    fn route(
        &mut self,
        request: &RouteRequest,
        ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal> {
        RouteSelector::new(
            &self.catalog,
            &self.credentials,
            &self.entitlement,
            &self.policy,
            &self.local_endpoints,
        )
        .select(request, ledger)
    }

    fn context_window(&self, model: &ModelKey) -> Option<u64> {
        self.catalog.model(model).map(|entry| entry.context_window)
    }

    fn replace_credentials(&mut self, entries: Vec<CredentialRef>) {
        Self::replace_credentials(self, entries);
    }

    fn replace_local_endpoints(&mut self, endpoints: Vec<Endpoint>) {
        Self::replace_local_endpoints(self, endpoints);
    }

    fn install_catalog(&mut self, catalog: MergedCatalog) {
        self.catalog = catalog;
    }

    fn set_entitlement(&mut self, entitlement: ManagedEntitlement) {
        Self::set_entitlement(self, entitlement);
    }

    fn set_policy(&mut self, policy: ModelPolicy) {
        Self::set_policy(self, policy);
    }
}

#[cfg(test)]
mod tests {
    use super::ProductionModelRouter;

    #[test]
    fn production_router_accepts_the_exact_compiled_catalog() {
        assert!(ProductionModelRouter::from_embedded_catalog().is_ok());
    }
}
