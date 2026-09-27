// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The catalog's composition surface.
//!
//! Two layers now, not three. Decision 0200 removed the served overlay and
//! everything that fetched it, so what a profile composes is the embedded
//! baseline and the person's own layer — their custom providers and the model
//! lists their own providers answered with their own credentials.
//!
//! What survives the overlay's departure is the reason this file exists: a
//! change to the catalog becomes one state everywhere — the router's catalog,
//! the provider plane's rows, and the credential references routing spends —
//! because a catalog some component has not heard about is the staleness class
//! decision 0049 was written about. Two of the three paths that change it
//! remain, and they still share [`ProfileServiceRuntime::reinstall_catalog_state`].

use model_router::catalog::types::CatalogDocument;
use model_router::MergedCatalog;

use crate::adapters::model::{ModelRouterBuildError, ProductionModelRouter};
use crate::adapters::provider::install_provider_state;

use super::super::provider_catalog::{catalog_models_from, catalog_providers_from};
use super::super::user_catalog::user_catalog_document;
use super::{ProfileRuntimeBuildError, ProfileServiceRuntime};

/// Builds the boot-time catalog state: the router and the baseline document.
///
/// There is nothing to adopt at boot any more. The overlay this used to
/// restore from a cache is gone with the host that served it, and the person's
/// own layer is installed by the first change to either plane, through
/// [`ProfileServiceRuntime::reinstall_catalog_state`] — which is where it was
/// installed before, because a runtime that can read the provider plane does
/// not exist yet at this point.
pub(super) fn bootstrap_catalog() -> Result<
    (
        ProductionModelRouter,
        model_router::catalog::types::CatalogDocument,
    ),
    ProfileRuntimeBuildError,
> {
    let mut models = ProductionModelRouter::from_embedded_catalog()
        .map_err(ProfileRuntimeBuildError::InvalidModelCatalog)?;
    // The one M7 seam (decision 0082): the per-role preference surface does
    // not exist yet, and the honest-empty policy stated here is what it will
    // replace. Candidate order is catalog key order until then, by this call
    // rather than by an uninitialized default nobody chose.
    models.set_policy(model_router::route::ModelPolicy::new());
    let (parsed_baseline, _) = model_router::embedded_baseline().map_err(|_| {
        ProfileRuntimeBuildError::InvalidModelCatalog(ModelRouterBuildError::InvalidEmbeddedCatalog)
    })?;
    let catalog_baseline = parsed_baseline.document;
    Ok((models, catalog_baseline))
}

impl ProfileServiceRuntime {
    /// The person's own layer, rebuilt from what the two planes hold now.
    ///
    /// Rebuilt rather than kept. A held copy is a copy that can still describe
    /// a provider the person removed a moment ago, and it would be a second
    /// thing to remember to update on every path that changes either plane —
    /// which is precisely how the state a surface reads goes stale without
    /// anything being wrong anywhere.
    pub(super) fn user_catalog_layer(&self) -> CatalogDocument {
        user_catalog_document(
            &self.catalog_baseline,
            self.providers.custom_providers(),
            self.listings.models(),
        )
    }

    /// The merge as it stands: the baseline and the person's own layer.
    ///
    /// The middle argument is the served overlay's place and it is now always
    /// `None`. The parameter stays until the contract reset takes
    /// `CatalogLayer::RemoteOverlay` with it, so that the layer order a reader
    /// sees here is the layer order the merge still implements.
    pub(super) fn merged_catalog(&self) -> MergedCatalog {
        let user = self.user_catalog_layer();
        MergedCatalog::build(&self.catalog_baseline, None, Some(&user))
    }

    /// Rebuilds the merge and re-installs everything that reads it.
    ///
    /// Every path that changes what the catalog says goes through here, and
    /// there are two: an accepted provider command and an accepted model
    /// listing. There were three — an accepted served snapshot was the first —
    /// and it left with the overlay. They share this function rather
    /// than each doing the same four installs, because the failure they all
    /// have is the same one — a component that was not told, still answering
    /// from the state it was built with, with no log and no failing test
    /// because everything is behaving as written (decision 0049).
    pub(super) fn reinstall_catalog_state(&mut self) {
        let merged = self.merged_catalog();
        if let (Ok(rows), Ok(models)) = (
            catalog_providers_from(merged.providers()),
            catalog_models_from(&merged),
        ) {
            self.providers.replace_catalog(rows, models);
        }
        self.core.models_mut().install_catalog(merged);
        install_provider_state(&self.providers, self.core.models_mut());
        // The entitled model set was resolved against the merge this call just
        // replaced, so it is resolved again over the new one — a changed
        // catalog must not leave routing spending yesterday's model keys
        // (decision 0082).
        self.install_managed_entitlement();
    }
}
