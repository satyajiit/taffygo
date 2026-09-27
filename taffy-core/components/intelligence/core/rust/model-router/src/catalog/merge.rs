// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The three-layer merge and the lookups the router reads.
//!
//! One merged snapshot is built from three layers, later layers winning by id:
//! the embedded baseline compiled into the release, the served overlay cached
//! on the device, and the user's own providers and overrides. Reads against the
//! snapshot are synchronous; a network refresh replaces a snapshot and never
//! blocks a request.
//!
//! One rule in this module is a safety property rather than a preference: an
//! overlay whose generation time is not strictly newer than the baseline's is
//! ignored. Without it, a device that has cached an old served catalog would
//! silently downgrade a freshly built release to a staler description of the
//! world — including staler prices and a staler kill switch.
//!
//! What a catalog cannot do is as important as what it can. It can add,
//! describe, re-price, and disable providers and models. It cannot change route
//! semantics, disclosure classes, redaction policy, or tool authorization:
//! those live in code and in policy bundles with their own review path, and
//! nothing in this module reads them.

use std::collections::BTreeMap;

use crate::catalog::types::{
    AuthMethod, CatalogDocument, CatalogHeader, Endpoint, Model, ModelRole, Provider, WireApi,
};
use crate::ids::{ModelId, ModelKey, ProviderId};

/// Which layer an entry came from.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum CatalogLayer {
    /// Compiled into the release. First run and offline operation use this
    /// layer alone.
    EmbeddedBaseline,
    /// Served and cached on the device.
    RemoteOverlay,
    /// The user's own providers, endpoints, and per-model overrides.
    UserOverride,
}

/// A provider and the layer that supplied it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProviderEntry {
    /// The entry.
    pub provider: Provider,
    /// Where it came from.
    pub layer: CatalogLayer,
}

/// A model and the layer that supplied it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelEntry {
    /// The entry.
    pub model: Model,
    /// Where it came from.
    pub layer: CatalogLayer,
}

/// Why a lookup refused.
///
/// Every variant is a closed door. Nothing here resolves to a nearest match, a
/// default provider, or a substitute model: an unknown or disabled entry is a
/// refusal the caller has to handle.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LookupError {
    /// No such provider in the merged snapshot.
    UnknownProvider {
        /// The requested provider.
        provider_id: ProviderId,
    },
    /// No such model under that provider.
    UnknownModel {
        /// The requested model.
        model_id: ModelId,
    },
    /// The provider's kill switch is off.
    ProviderDisabled {
        /// The requested provider.
        provider_id: ProviderId,
    },
    /// The model's kill switch is off.
    ModelDisabled {
        /// The requested model.
        model_id: ModelId,
    },
}

impl core::fmt::Display for LookupError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::UnknownProvider { provider_id } => write!(f, "unknown provider {provider_id}"),
            Self::UnknownModel { model_id } => write!(f, "unknown model {model_id}"),
            Self::ProviderDisabled { provider_id } => write!(f, "provider {provider_id} disabled"),
            Self::ModelDisabled { model_id } => write!(f, "model {model_id} disabled"),
        }
    }
}

/// A model resolved together with the provider settings it inherits.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ResolvedModel<'a> {
    /// The provider entry.
    pub provider: &'a Provider,
    /// The model entry.
    pub model: &'a Model,
    /// The protocol family after applying the model's override, for a
    /// credential that authenticates with a key.
    ///
    /// A vendor a subscription reaches differently answers differently, and
    /// which of its two answers applies is a fact about the credential rather
    /// than about the catalog — so a caller that holds one asks
    /// [`ResolvedModel::wire_api_for`] instead of reading this.
    pub wire_api: WireApi,
    /// The endpoint after applying the model's override, for a credential that
    /// authenticates with a key.
    pub endpoint: &'a Endpoint,
    /// Which layer named the endpoint this model is reached at.
    ///
    /// The layer of whichever entry supplied the address, which is not always
    /// the layer of the model: an aggregator lists its models onto a person's
    /// own layer while the address they are reached at stays the published
    /// provider's. The answer holds for [`ResolvedModel::endpoint_for`] too,
    /// because both spellings of the provider's address come from the provider
    /// entry and a model override wins over either.
    ///
    /// It is read where a request is composed, to decide which of the two
    /// endpoint rules the browser is being asked to apply (decision 0096
    /// section 2). Deriving that from the address itself would be a guess —
    /// a person may run https on the default port, and a served catalog may
    /// not — so it is carried as the fact the merge already recorded.
    pub endpoint_layer: CatalogLayer,
}

impl<'a> ResolvedModel<'a> {
    /// The family this model speaks to a credential of `method`.
    ///
    /// A model's own override still wins: it is the more specific statement,
    /// and quietly ignoring an explicit per-model value because a subscription
    /// happened to be in use would be the catalog saying one thing and the
    /// request doing another.
    pub fn wire_api_for(&self, method: AuthMethod) -> WireApi {
        self.model
            .wire_api
            .unwrap_or_else(|| self.provider.wire_api_for(method))
    }

    /// The endpoint a credential of `method` is spent at.
    pub fn endpoint_for(&self, method: AuthMethod) -> &'a Endpoint {
        self.model
            .endpoint
            .as_ref()
            .unwrap_or_else(|| self.provider.endpoint_for(method))
    }
}

/// The merged snapshot the router reads.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MergedCatalog {
    header: CatalogHeader,
    providers: BTreeMap<ProviderId, ProviderEntry>,
    models: BTreeMap<ModelKey, ModelEntry>,
    overlay_applied: bool,
}

impl MergedCatalog {
    /// Builds a snapshot from the baseline alone.
    pub fn from_baseline(baseline: &CatalogDocument) -> Self {
        Self::build(baseline, None, None)
    }

    /// Builds a snapshot from all three layers.
    ///
    /// The overlay is applied only when its generation time is strictly newer
    /// than the baseline's. User overrides always apply: they are the user's
    /// own configuration, not remote input.
    pub fn build(
        baseline: &CatalogDocument,
        overlay: Option<&CatalogDocument>,
        overrides: Option<&CatalogDocument>,
    ) -> Self {
        let mut merged = Self {
            header: baseline.header.clone(),
            providers: BTreeMap::new(),
            models: BTreeMap::new(),
            overlay_applied: false,
        };
        merged.apply(baseline, CatalogLayer::EmbeddedBaseline);
        if let Some(overlay) = overlay {
            if overlay.header.generated_at > baseline.header.generated_at {
                merged.header = overlay.header.clone();
                merged.apply(overlay, CatalogLayer::RemoteOverlay);
                merged.overlay_applied = true;
            }
        }
        if let Some(overrides) = overrides {
            merged.apply(overrides, CatalogLayer::UserOverride);
        }
        merged
    }

    fn apply(&mut self, document: &CatalogDocument, layer: CatalogLayer) {
        for provider in &document.providers {
            self.providers.insert(
                provider.provider_id.clone(),
                ProviderEntry {
                    provider: provider.clone(),
                    layer,
                },
            );
        }
        for model in &document.models {
            self.models.insert(
                model.key(),
                ModelEntry {
                    model: model.clone(),
                    layer,
                },
            );
        }
    }

    /// The header of the layer that won.
    pub fn header(&self) -> &CatalogHeader {
        &self.header
    }

    /// Whether the served overlay was newer than the baseline and applied.
    pub fn overlay_applied(&self) -> bool {
        self.overlay_applied
    }

    /// Every provider entry, in id order.
    pub fn providers(&self) -> impl Iterator<Item = &ProviderEntry> {
        self.providers.values()
    }

    /// Every model entry, in key order.
    pub fn models(&self) -> impl Iterator<Item = &ModelEntry> {
        self.models.values()
    }

    /// Looks a provider up without checking its kill switch.
    pub fn provider(&self, provider_id: &ProviderId) -> Option<&Provider> {
        self.providers.get(provider_id).map(|entry| &entry.provider)
    }

    /// Looks a model up without checking its kill switch.
    pub fn model(&self, key: &ModelKey) -> Option<&Model> {
        self.models.get(key).map(|entry| &entry.model)
    }

    /// The layer an entry came from, for a surface that explains a change.
    pub fn model_layer(&self, key: &ModelKey) -> Option<CatalogLayer> {
        self.models.get(key).map(|entry| entry.layer)
    }

    /// Resolves a model with its inherited provider settings, failing closed.
    pub fn resolve(&self, key: &ModelKey) -> Result<ResolvedModel<'_>, LookupError> {
        let provider =
            self.providers
                .get(&key.provider_id)
                .ok_or_else(|| LookupError::UnknownProvider {
                    provider_id: key.provider_id.clone(),
                })?;
        let model = self
            .models
            .get(key)
            .ok_or_else(|| LookupError::UnknownModel {
                model_id: key.model_id.clone(),
            })?;
        if !provider.provider.enabled {
            return Err(LookupError::ProviderDisabled {
                provider_id: key.provider_id.clone(),
            });
        }
        if !model.model.enabled {
            return Err(LookupError::ModelDisabled {
                model_id: key.model_id.clone(),
            });
        }
        Ok(ResolvedModel {
            provider: &provider.provider,
            model: &model.model,
            wire_api: model.model.wire_api.unwrap_or(provider.provider.wire_api),
            endpoint: model
                .model
                .endpoint
                .as_ref()
                .unwrap_or(&provider.provider.default_endpoint),
            endpoint_layer: if model.model.endpoint.is_some() {
                model.layer
            } else {
                provider.layer
            },
        })
    }

    /// Every enabled model cataloged for `role`, in key order.
    ///
    /// Key order is total and stable, so two devices with the same snapshot
    /// enumerate candidates identically. Candidate ordering feeds failover, and
    /// failover that varies by iteration order is not reproducible.
    pub fn candidates_for_role(&self, role: ModelRole) -> Vec<ResolvedModel<'_>> {
        self.models
            .keys()
            .filter_map(|key| self.resolve(key).ok())
            .filter(|resolved| resolved.model.serves(role))
            .collect()
    }
}
