// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a person chose between models, and the questions asked about it.
//!
//! Split from the plane's own file rather than left beside the credentials,
//! because a preference and a credential answer different questions: one is a
//! secret's registry state, the other is a choice that outlives the secret
//! being replaced. They share a plane and nothing else, and reading either
//! half should not mean reading both.

use model_router::ThinkingLevel;

use crate::provider::{
    CatalogModel, ProviderEffect, ProviderError, ProviderId, ProviderModelPreference,
};

use super::ProviderProtocol;

impl ProviderProtocol {
    /// Files one provider's standing model choice (decision 0093).
    ///
    /// The arguments are the choice as it should now stand, so an absent model
    /// clears the pin and an absent rung clears back to "Taffy decides". A
    /// record stating neither is removed rather than kept empty: the record is
    /// the choice, and no choice is no record.
    ///
    /// A pin is checked against the merged catalog before it is stored, so
    /// `selected_model_id` is always a model the catalog carries. The refusal
    /// is `UnknownProvider` because that is what a `(provider, model)` pair the
    /// catalog does not carry is — a name filed under no identity this plane
    /// knows — and because the refusal vocabulary is closed at what the plane
    /// can reach on its own facts. Nothing is written on the way to it: the
    /// lookup happens before the record is touched, so a refused pin cannot
    /// leave a cleared rung behind it.
    ///
    /// The provider must exist and nothing else about it is asked. A kill
    /// switch is the catalog's answer for this generation, and refusing a
    /// choice for a provider switched off today would drop the person's
    /// standing choice on the replay that restores it.
    pub fn set_model_preference(
        &mut self,
        provider_id: &ProviderId,
        model_id: Option<&str>,
        thinking: Option<ThinkingLevel>,
    ) -> Result<ProviderEffect, ProviderError> {
        if !self.catalog.contains_key(provider_id) && !self.custom.contains_key(provider_id) {
            return Err(ProviderError::UnknownProvider);
        }
        let pinned = match model_id {
            Some(raw) => Some(
                self.carried_model(provider_id, raw)
                    .ok_or(ProviderError::UnknownProvider)?
                    .model_id
                    .clone(),
            ),
            None => None,
        };
        let preference = ProviderModelPreference {
            model_id: pinned,
            thinking,
        };
        if preference.states_a_choice() {
            self.preferences.insert(provider_id.clone(), preference);
        } else {
            self.preferences.remove(provider_id);
        }
        self.status_changed();
        Ok(ProviderEffect::None)
    }

    /// Every model the catalog carries, provider by provider.
    ///
    /// Flat and totally ordered: provider identity first, then the catalog's
    /// own order under it. A surface groups them again if it wants rows per
    /// provider; the ordering is stated here so two devices on one snapshot
    /// project the same list.
    pub fn models(&self) -> impl Iterator<Item = &CatalogModel> {
        self.models.values().flatten()
    }

    /// The model each provider is pinned to, in provider identity order.
    ///
    /// A pin whose model the catalog no longer carries cannot appear here:
    /// [`Self::replace_catalog`] drops it as the catalog moves.
    pub fn pinned_models(&self) -> impl Iterator<Item = &CatalogModel> {
        self.preferences.iter().filter_map(|(provider_id, choice)| {
            self.carried_model(provider_id, choice.model_id.as_ref()?.as_str())
        })
    }

    /// The standing choice for one provider, if a choice was made.
    pub fn preference(&self, provider_id: &ProviderId) -> Option<&ProviderModelPreference> {
        self.preferences.get(provider_id)
    }

    /// The one rung every provider that stated a rung agrees on.
    ///
    /// A request asks for a rung before route selection has chosen a provider,
    /// so a per-provider choice has to collapse to a single answer or to none
    /// at all. It collapses when the choices that exist agree. A person who
    /// asked two providers for different amounts of thinking has not told this
    /// request which of their answers to apply, and Taffy deciding is a better
    /// reading of that than picking whichever provider sorts first.
    ///
    /// The model half of a preference does not come through here: a pin leads
    /// its role through the policy the router is given, which is what keeps
    /// failover working when the pinned model is refused.
    pub fn agreed_thinking(&self) -> Option<ThinkingLevel> {
        let mut agreed: Option<ThinkingLevel> = None;
        for level in self
            .preferences
            .values()
            .filter_map(|choice| choice.thinking)
        {
            match agreed {
                Some(existing) if existing != level => return None,
                Some(_) => {}
                None => agreed = Some(level),
            }
        }
        agreed
    }

    /// The catalog row for one model under one provider.
    pub(super) fn carried_model(
        &self,
        provider_id: &ProviderId,
        model_id: &str,
    ) -> Option<&CatalogModel> {
        self.models
            .get(provider_id)?
            .iter()
            .find(|model| model.model_id.as_str() == model_id)
    }

    /// Drops every pin the current catalog no longer carries.
    pub(super) fn drop_departed_pins(&mut self) {
        let departed: Vec<ProviderId> = self
            .preferences
            .iter()
            .filter(|(provider_id, choice)| {
                choice.model_id.as_ref().is_some_and(|model_id| {
                    self.carried_model(provider_id, model_id.as_str()).is_none()
                })
            })
            .map(|(provider_id, _)| provider_id.clone())
            .collect();
        for provider_id in departed {
            let still_states_a_choice =
                self.preferences
                    .get_mut(&provider_id)
                    .is_some_and(|choice| {
                        choice.model_id = None;
                        choice.states_a_choice()
                    });
            if !still_states_a_choice {
                self.preferences.remove(&provider_id);
            }
        }
    }

    /// The standing choice for one provider, as a roster row carries it.
    pub(super) fn chosen(&self, provider_id: &ProviderId) -> ProviderModelPreference {
        self.preferences
            .get(provider_id)
            .cloned()
            .unwrap_or_default()
    }
}
