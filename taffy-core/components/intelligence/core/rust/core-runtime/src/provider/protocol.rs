// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one place a provider's credential or definition changes.
//!
//! Every write goes through `&mut self` on a value the ordered core sequence
//! owns, so "one serialized modify path" is enforced by the borrow checker
//! rather than by a lock somebody has to remember to take. The reference
//! designs need an explicit mutex because their state is shared across async
//! tasks; here the single ordered sequence already provides it, and the type
//! system will not let a second writer exist to be forgotten about.
//!
//! The authoritative re-check happens inside each method, on the state as it
//! is at that moment, never on a copy read earlier.

use std::collections::{BTreeMap, BTreeSet};

use model_router::catalog::CatalogLayer;
use model_router::CredentialState;

use super::identity::{CredentialHandle, ProviderId};
use super::messages::{
    CatalogModel, CustomProvider, ProviderAuthMethod, ProviderCredential, ProviderEffect,
    ProviderModelPreference, ProviderOrigin, ProviderPresentation, ProviderRefusal, ProviderView,
    StoredCredentialView,
};
use super::{ProviderError, ProviderStatusProjection};

/// One provider the compiled catalog ships.
#[derive(Clone, Debug, PartialEq, Eq)]
// The same four independent yes/no facts `ProviderView` carries, for the same
// reason: shipped, actionable, address refused and plan-backed are orthogonal,
// and a row can hold any combination of them. Folding two into an enum would
// invent a relationship the catalog does not state.
#[allow(clippy::struct_excessive_bools)]
pub struct CatalogProvider {
    /// The identity the baseline files it under.
    pub provider_id: ProviderId,
    /// The name the baseline shows for it.
    pub display_name: String,
    /// The methods the baseline says it offers.
    pub auth_methods: Vec<ProviderAuthMethod>,
    /// Whether the baseline ships it switched on.
    pub enabled: bool,
    /// Which merge layer supplied this row (decision 0080).
    pub layer: CatalogLayer,
    /// Whether this build can act on the row: at least one declared method
    /// has a working entry path in this binary. A served row must not claim
    /// what the binary cannot do.
    pub configurable: bool,
    /// Whether OAUTH on this row means a plan the person already pays for.
    ///
    /// The catalog's own answer, carried rather than derived: an exchange that
    /// mints a metered key is spent exactly as a pasted one is, so a surface
    /// reading `auth_methods` alone would file it under a tab named for plans.
    pub subscription: bool,
    /// The served catalog asked to move this provider's endpoint host and the
    /// guard refused (decision 0080). Surfaces state it; nothing connects to
    /// the refused host.
    pub endpoint_changed: bool,
    /// The host the guard refused, beside the flag rather than instead of it:
    /// the sanitized overlay carries the baseline address again, so only this
    /// says where the document wanted to go. Present exactly while
    /// `endpoint_changed` is true, and carried for disclosure only.
    pub refused_endpoint_host: Option<String>,
    /// The few setup facts the catalog carries for this vendor (decision 0094),
    /// empty when it carries none.
    pub presentation: ProviderPresentation,
}

/// The most subscription sign-ins that may be pending at once.
///
/// A resource bound rather than a policy: the compiled flow map has four
/// vendors and the screen runs one flow at a time, so a fifth concurrent
/// admission is a runaway caller, not a person. Small on purpose — every
/// pending flow is a browser-side state machine holding a Custom Tab or a
/// polling loop open.
pub const MAX_PENDING_SIGN_IN_FLOWS: usize = 4;

/// One admitted subscription sign-in the plane is keeping the record of.
///
/// The record, not the flow: the browser runs every network leg and owns the
/// deadline (decision 0078), and this entry exists so the roster can say
/// `signing_in` from a fact the plane holds rather than a flag a caller
/// asserts, and so a callback for a flow nobody started is refused by name.
#[derive(Clone, Debug, PartialEq, Eq)]
struct PendingSignIn {
    provider_id: ProviderId,
    redirect_binding_id: String,
}

/// Profile-scoped provider credentials and a person's own providers.
///
/// Holds no material, performs no I/O, reads no clock.
#[derive(Clone, Debug, Default)]
pub struct ProviderProtocol {
    catalog: BTreeMap<ProviderId, CatalogProvider>,
    /// The models each catalog provider carries, in catalog order.
    models: BTreeMap<ProviderId, Vec<CatalogModel>>,
    credentials: BTreeMap<ProviderId, ProviderCredential>,
    custom: BTreeMap<ProviderId, CustomProvider>,
    /// The standing model choice per provider. A provider nobody chose for
    /// holds no record, so "no choice" is an absence rather than a value.
    preferences: BTreeMap<ProviderId, ProviderModelPreference>,
    /// What each provider last refused with while its credential was usable.
    /// In-memory and per process, like the probe verdicts beside it: a refusal
    /// is a fact about a minute ago, and one restored from disk would banner a
    /// quota that has long since reset.
    refusals: BTreeMap<ProviderId, ProviderRefusal>,
    /// Pending subscription sign-ins, keyed by browser-minted flow identity.
    /// In-memory on purpose: a flow does not survive the process that opened
    /// its surface, so restoring one from a journal would resurrect a sign-in
    /// whose browser half no longer exists.
    pending_sign_ins: BTreeMap<String, PendingSignIn>,
    /// Eager status answer rebuilt at every successful mutation. Reads borrow
    /// it; they never walk and clone the authoritative maps again.
    status_projection: ProviderStatusProjection,
}

pub mod custom;
mod preferences;

impl ProviderProtocol {
    /// Builds the plane over the providers and models the catalog ships.
    ///
    /// The two arrive together and are replaced together, because a pin is
    /// only valid against the models of the catalog it was checked against
    /// (decision 0093).
    pub fn new(
        catalog: impl IntoIterator<Item = CatalogProvider>,
        models: impl IntoIterator<Item = CatalogModel>,
    ) -> Self {
        let mut protocol = Self {
            catalog: catalog
                .into_iter()
                .map(|entry| (entry.provider_id.clone(), entry))
                .collect(),
            models: group_by_provider(models),
            credentials: BTreeMap::new(),
            custom: BTreeMap::new(),
            preferences: BTreeMap::new(),
            refusals: BTreeMap::new(),
            pending_sign_ins: BTreeMap::new(),
            status_projection: ProviderStatusProjection::default(),
        };
        protocol.status_changed();
        protocol
    }

    /// Replaces the catalog rows after an accepted overlay refresh.
    ///
    /// Credentials and the person's own providers survive untouched: they are
    /// the person's state, and a served snapshot has no authority over either.
    /// A credential for a provider the overlay no longer lists keeps its
    /// record — the merge never removes a baseline row, and a person's key is
    /// not the catalog's to discard.
    ///
    /// A pin is the one part of the person's state a refresh may change, and
    /// it may only drop it: a model the new catalog does not carry is a name
    /// nothing can route to, so it stops being a pin rather than being shown
    /// against nothing. The rung the person asked for survives, because it is
    /// about the thinking ladder rather than about the model that left.
    pub fn replace_catalog(
        &mut self,
        catalog: impl IntoIterator<Item = CatalogProvider>,
        models: impl IntoIterator<Item = CatalogModel>,
    ) {
        self.catalog = catalog
            .into_iter()
            .map(|entry| (entry.provider_id.clone(), entry))
            .collect();
        self.models = group_by_provider(models);
        self.drop_departed_pins();
        self.status_changed();
    }

    /// Installs the durable state one profile bootstrap restored.
    pub fn restore(
        &mut self,
        credentials: impl IntoIterator<Item = ProviderCredential>,
        custom: impl IntoIterator<Item = CustomProvider>,
    ) {
        self.credentials = credentials
            .into_iter()
            .map(|entry| (entry.provider_id.clone(), entry))
            .collect();
        self.custom = custom
            .into_iter()
            .map(|entry| (entry.provider_id.clone(), entry))
            .collect();
        self.status_changed();
    }

    /// Records one provider credential, replacing whatever was filed before.
    ///
    /// Returns the handle the replaced record held, if there was one: the
    /// browser owns that material and nothing else will ever mention it again,
    /// so a caller that drops this effect leaves a key in the store for a
    /// credential no component references.
    pub fn save_credential(
        &mut self,
        provider_id: ProviderId,
        auth_method: ProviderAuthMethod,
        handle: CredentialHandle,
    ) -> Result<ProviderEffect, ProviderError> {
        self.require_offers(&provider_id, auth_method)?;
        // Read the handle being displaced before the insert, so the new one can
        // be moved into the record rather than copied past it. Saving the same
        // handle twice displaces nothing: releasing it there would revoke a
        // credential that is still in use.
        let displaced = match self.credentials.get(&provider_id) {
            Some(held) if held.handle == handle => None,
            Some(held) => Some(held.handle.clone()),
            None => None,
        };
        self.credentials.insert(
            provider_id.clone(),
            ProviderCredential {
                provider_id,
                auth_method,
                handle,
                state: CredentialState::Usable,
            },
        );
        let effect = match displaced {
            Some(previous) => ProviderEffect::ReleaseHandle(previous),
            None => ProviderEffect::None,
        };
        self.status_changed();
        Ok(effect)
    }

    /// Clears one provider's credential and leaves the provider defined.
    ///
    /// A person's own provider keeps its endpoint and its wire family, so a key
    /// can be revoked without discarding what they set up.
    pub fn forget_credential(
        &mut self,
        provider_id: &ProviderId,
    ) -> Result<ProviderEffect, ProviderError> {
        let removed = self
            .credentials
            .remove(provider_id)
            .ok_or(ProviderError::UnknownProvider)?;
        if let Some(entry) = self.custom.get_mut(provider_id) {
            entry.credential = None;
        }
        self.status_changed();
        Ok(ProviderEffect::ReleaseHandle(removed.handle))
    }

    /// Files the browser layer's report of one stored credential's state.
    ///
    /// The report is registry fact about a record that exists: a state about
    /// nothing is refused as `UnknownProvider`, because absence is a deletion
    /// (`forget_credential`) and a reporter that lost that race must observe
    /// the deletion rather than resurrect the record — the in-section re-read
    /// decision 0078 requires browser-side has this refusal as its backstop.
    /// Re-filing the current state is accepted and is still a write, so the
    /// caller republishes and every surface converges on the reporter's view.
    pub fn set_credential_state(
        &mut self,
        provider_id: &ProviderId,
        state: CredentialState,
    ) -> Result<ProviderEffect, ProviderError> {
        let record = self
            .credentials
            .get_mut(provider_id)
            .ok_or(ProviderError::UnknownProvider)?;
        record.state = state;
        self.status_changed();
        Ok(ProviderEffect::None)
    }

    /// Files what one provider last refused with, or clears it.
    ///
    /// `None` is the provider spending again, and clearing rather than leaving
    /// the old fact standing is the whole point: a banner that outlives the
    /// refusal it names is worse than no banner. Filed against any identity,
    /// including one no roster row carries — the roster reads this map rather
    /// than the other way round, so a refusal for a provider that has since
    /// been removed simply never surfaces.
    pub fn record_refusal(&mut self, provider_id: &ProviderId, refusal: Option<ProviderRefusal>) {
        match refusal {
            Some(held) => {
                self.refusals.insert(provider_id.clone(), held);
            }
            None => {
                self.refusals.remove(provider_id);
            }
        }
        self.status_changed();
    }

    /// What one provider last refused with, for a caller that is not the
    /// roster projection.
    #[must_use]
    pub fn last_refusal(&self, provider_id: &ProviderId) -> Option<ProviderRefusal> {
        self.refusals.get(provider_id).copied()
    }

    /// Every credential the router should know about.
    pub fn credentials(&self) -> impl Iterator<Item = &ProviderCredential> {
        self.credentials.values()
    }

    /// The roster a surface draws, catalog providers first and then the
    /// person's own, each in a stable order so a redraw never reshuffles.
    ///
    /// `signing_in` is the pending-admission marker and nothing else: it is
    /// true exactly while [`Self::begin_sign_in`] has admitted a flow for the
    /// provider that [`Self::finish_sign_in`] has not yet cleared, never a
    /// flag a caller asserts.
    pub fn roster(&self) -> Vec<ProviderView> {
        self.status_projection.views().cloned().collect()
    }

    /// The eager bounded provider answer status contributors borrow.
    pub fn status_projection(&self) -> &ProviderStatusProjection {
        &self.status_projection
    }

    /// Rebuilds the projection from the authoritative maps after one accepted
    /// mutation. This is the only invalidation point.
    fn status_changed(&mut self) {
        let revision = self.status_projection.next_revision();
        let roster = self.build_roster();
        self.status_projection =
            ProviderStatusProjection::rebuild(revision, roster, self.models.values().flatten());
    }

    /// Builds the roster once for a provider mutation, never for publication.
    fn build_roster(&self) -> Vec<ProviderView> {
        let catalog = self.catalog.values().map(|entry| ProviderView {
            presentation: entry.presentation.clone(),
            provider_id: entry.provider_id.clone(),
            display_name: entry.display_name.clone(),
            origin: ProviderOrigin::Catalog,
            auth_methods: entry.auth_methods.clone(),
            stored: Self::stored_view(self.credentials.get(&entry.provider_id)),
            signing_in: self.signing_in(&entry.provider_id),
            enabled: entry.enabled,
            endpoint_host: None,
            endpoint_base: None,
            last_refusal: self.refusals.get(&entry.provider_id).copied(),
            configurable: entry.configurable,
            subscription: entry.subscription,
            endpoint_changed: entry.endpoint_changed,
            refused_endpoint_host: entry.refused_endpoint_host.clone(),
            catalog_layer: entry.layer,
            preference: self.chosen(&entry.provider_id),
        });
        let custom = self.custom.values().map(|entry| ProviderView {
            // A person's own server has no vendor page and no key prefix to
            // publish, so there is nothing here that would not be invented.
            presentation: ProviderPresentation::default(),
            provider_id: entry.provider_id.clone(),
            display_name: entry.display_name.as_str().to_owned(),
            origin: ProviderOrigin::Custom,
            auth_methods: vec![ProviderAuthMethod::ApiKey],
            stored: Self::stored_view(self.credentials.get(&entry.provider_id)),
            // A person's own provider is key-only, so no sign-in can be
            // admitted for it and this is structurally false.
            signing_in: false,
            enabled: true,
            endpoint_host: Some(entry.endpoint.host().to_owned()),
            // The whole address, port and base path included. The host beside
            // it is what a disclosure line may show; this is what the edit
            // screen puts back in the field, and decision 0096 exists because
            // those two are not the same string.
            endpoint_base: Some(entry.endpoint.as_str().to_owned()),
            last_refusal: self.refusals.get(&entry.provider_id).copied(),
            // A person's own provider is its own layer, always actionable,
            // and no served snapshot may touch its endpoint at all.
            configurable: true,
            // Key-only by construction, so there is no plan to claim.
            subscription: false,
            endpoint_changed: false,
            refused_endpoint_host: None,
            catalog_layer: CatalogLayer::UserOverride,
            preference: self.chosen(&entry.provider_id),
        });
        catalog.chain(custom).collect()
    }

    /// Projects one credential record into its roster facts.
    ///
    /// No record projects nothing, and so does a record whose state claims
    /// `Absent`: no write path produces that shape, but the type admits it,
    /// and a view that said "stored" while its state said "nothing" would be
    /// the contradiction the closed contract enum exists to refuse.
    fn stored_view(credential: Option<&ProviderCredential>) -> Option<StoredCredentialView> {
        let held = credential?;
        if held.state == CredentialState::Absent {
            return None;
        }
        Some(StoredCredentialView {
            auth_method: held.auth_method,
            state: held.state,
            subscription_backed: held.auth_method == ProviderAuthMethod::Oauth,
        })
    }

    /// Admits one subscription sign-in and records it as pending.
    ///
    /// Admission is the plane's whole part in the flow (decision 0078): the
    /// browser mints the flow identity, runs every network leg, owns the
    /// deadline and the redirect-state comparison, and reports the terminal
    /// through [`Self::finish_sign_in`]. What is decided here is decided on
    /// the plane's own facts — the provider offers OAUTH, the catalog ships
    /// it switched on, and no flow is already running for it — so a flow can
    /// never start against a provider the eventual save would refuse.
    pub fn begin_sign_in(
        &mut self,
        provider_id: ProviderId,
        flow_id: String,
        redirect_binding_id: String,
    ) -> Result<ProviderEffect, ProviderError> {
        self.require_offers(&provider_id, ProviderAuthMethod::Oauth)?;
        if self
            .pending_sign_ins
            .values()
            .any(|pending| pending.provider_id == provider_id)
            || self.pending_sign_ins.contains_key(&flow_id)
        {
            return Err(ProviderError::FlowAlreadyRunning);
        }
        if self.pending_sign_ins.len() >= MAX_PENDING_SIGN_IN_FLOWS {
            return Err(ProviderError::TooManyPendingFlows);
        }
        self.pending_sign_ins.insert(
            flow_id,
            PendingSignIn {
                provider_id,
                redirect_binding_id,
            },
        );
        self.status_changed();
        Ok(ProviderEffect::None)
    }

    /// Files a sign-in flow's terminal and clears its pending record.
    ///
    /// The identity is the pair the flow was admitted under — flow id and
    /// redirect binding — so a callback that names a live flow with someone
    /// else's binding is a callback for a flow that was never started, not a
    /// live flow with a detail wrong. The terminal itself changes nothing
    /// else here: a successful exchange arrives later as an ordinary
    /// [`Self::save_credential`] from the browser coordinator, and a failed
    /// flow simply stops being pending.
    pub fn finish_sign_in(
        &mut self,
        flow_id: &str,
        redirect_binding_id: &str,
    ) -> Result<ProviderEffect, ProviderError> {
        let pending = self
            .pending_sign_ins
            .get(flow_id)
            .ok_or(ProviderError::UnknownFlow)?;
        if pending.redirect_binding_id != redirect_binding_id {
            return Err(ProviderError::UnknownFlow);
        }
        self.pending_sign_ins.remove(flow_id);
        self.status_changed();
        Ok(ProviderEffect::None)
    }

    /// Cancels one exact pending subscription sign-in.
    ///
    /// The portable record is removed before the browser is asked to stop its
    /// native work. That ordering closes the race with a terminal callback: a
    /// callback that arrives after an accepted cancel finds no flow and cannot
    /// revive it, while a cancel that loses to a terminal is refused by name.
    pub fn cancel_sign_in(&mut self, flow_id: &str) -> Result<ProviderEffect, ProviderError> {
        self.pending_sign_ins
            .remove(flow_id)
            .ok_or(ProviderError::UnknownFlow)?;
        self.status_changed();
        Ok(ProviderEffect::None)
    }

    /// Whether this provider may hold a credential of that method.
    ///
    /// The same question `save_credential` asks itself, exposed because a
    /// subscription sign-in has to reach it before there is anything to save.
    /// It is one method rather than two answers so a flow can never be started
    /// against a provider a save would go on to refuse.
    pub fn offers(
        &self,
        provider_id: &ProviderId,
        auth_method: ProviderAuthMethod,
    ) -> Result<(), ProviderError> {
        self.require_offers(provider_id, auth_method)
    }

    /// Refuses a credential for a provider that cannot hold one.
    ///
    /// A person's own provider is always reachable with a key. A catalog
    /// provider must both be shipped switched on and offer the method: saving
    /// an OAuth credential for a key-only vendor would file a record no route
    /// can ever spend.
    fn require_offers(
        &self,
        provider_id: &ProviderId,
        auth_method: ProviderAuthMethod,
    ) -> Result<(), ProviderError> {
        if self.custom.contains_key(provider_id) {
            return Ok(());
        }
        let entry = self
            .catalog
            .get(provider_id)
            .ok_or(ProviderError::UnknownProvider)?;
        if !entry.enabled {
            return Err(ProviderError::ProviderDisabled);
        }
        if !entry.auth_methods.contains(&auth_method) {
            return Err(ProviderError::MethodNotOffered);
        }
        Ok(())
    }

    /// Whether a sign-in is pending for this provider.
    fn signing_in(&self, provider_id: &ProviderId) -> bool {
        self.pending_sign_ins
            .values()
            .any(|pending| &pending.provider_id == provider_id)
    }

    /// The identities the compiled catalog reserves.
    pub fn catalog_ids(&self) -> BTreeSet<ProviderId> {
        self.catalog.keys().cloned().collect()
    }
}

/// Files models under the provider that carries them, keeping catalog order.
fn group_by_provider(
    models: impl IntoIterator<Item = CatalogModel>,
) -> BTreeMap<ProviderId, Vec<CatalogModel>> {
    let mut grouped: BTreeMap<ProviderId, Vec<CatalogModel>> = BTreeMap::new();
    for model in models {
        grouped
            .entry(model.provider_id.clone())
            .or_default()
            .push(model);
    }
    grouped
}

/// The kernel's one provider question, answered by the plane that owns the
/// credential lifecycle (decision 0072).
impl loop_kernel::provider::ProviderDirectory for ProviderProtocol {
    fn usable_credential(
        &self,
        provider_id: &model_router::ProviderId,
    ) -> Option<&loop_kernel::provider::CredentialHandle> {
        self.credentials()
            .find(|credential| {
                credential.provider_id.as_router() == provider_id && credential.state.is_usable()
            })
            .map(|credential| &credential.handle)
    }
}
