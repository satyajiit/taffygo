// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider plane's eager, bounded status projection.
//!
//! A ready-state publication is frequent: model stream chunks publish state as
//! well as provider writes. Building the provider roster and dealing the whole
//! merged model catalog into the Core API budget on every publication made an
//! unrelated stream chunk clone every provider twice and sort hundreds of
//! model positions. This value pays that work once, on the provider mutation
//! that changed its answer. Publications borrow it and copy only the already
//! bounded output into their owning status payload.

use std::collections::BTreeMap;

use super::{CatalogModel, ProviderView};

/// One provider row together with the whole-catalog count surfaces compare
/// against the bounded flat model list.
#[derive(Clone, Debug, PartialEq, Eq)]
struct ProjectedRosterRow {
    view: ProviderView,
    model_count: u32,
}

/// The provider status facts precomputed at the last successful plane write.
///
/// `revision` is internal change evidence, not a durable identity or wire
/// field. It changes for every accepted provider write, including an
/// idempotent report the plane deliberately treats as a write. Reads never
/// change it, so repeated state publications reuse one projection.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ProviderStatusProjection {
    revision: u64,
    roster: Vec<ProjectedRosterRow>,
    models: Vec<CatalogModel>,
}

impl ProviderStatusProjection {
    /// Rebuilds the complete bounded answer after one accepted provider write.
    pub(super) fn rebuild<'a>(
        revision: u64,
        roster: Vec<ProviderView>,
        models: impl Iterator<Item = &'a CatalogModel>,
    ) -> Self {
        let models: Vec<&CatalogModel> = models.collect();
        let counts = catalog_counts(&models);
        let selected = fit_to_budget(&models, &roster, core_api_types::MAX_PROVIDER_MODEL_ENTRIES);
        Self {
            revision,
            roster: roster
                .into_iter()
                .map(|view| ProjectedRosterRow {
                    model_count: counts.get(view.provider_id.as_str()).copied().unwrap_or(0),
                    view,
                })
                .collect(),
            models: selected.into_iter().cloned().collect(),
        }
    }

    /// In-memory evidence identifying this projection build.
    pub const fn revision(&self) -> u64 {
        self.revision
    }

    /// The next in-memory revision. Wrapping is harmless because the number
    /// carries no authority, and it keeps every adjacent rebuild distinguishable.
    pub(super) const fn next_revision(&self) -> u64 {
        self.revision.wrapping_add(1)
    }

    /// Provider views in stable order, paired with their pre-cut model counts.
    pub(crate) fn roster(&self) -> impl Iterator<Item = (&ProviderView, u32)> {
        self.roster.iter().map(|row| (&row.view, row.model_count))
    }

    /// Provider views alone, for explicit non-status callers of the plane.
    pub(super) fn views(&self) -> impl Iterator<Item = &ProviderView> {
        self.roster.iter().map(|row| &row.view)
    }

    /// The already-dealt flat model list, always within the Core API bound.
    pub(crate) fn models(&self) -> &[CatalogModel] {
        &self.models
    }

    /// Builds synthetic facts for the contributor's direct projection tests.
    #[cfg(test)]
    pub(crate) fn for_testing(roster: &[ProviderView], models: &[CatalogModel]) -> Self {
        Self::rebuild(1, roster.to_vec(), models.iter())
    }
}

/// How many models each provider carries before the surface budget is dealt.
fn catalog_counts<'a>(models: &[&'a CatalogModel]) -> BTreeMap<&'a str, u32> {
    let mut counts: BTreeMap<&str, u32> = BTreeMap::new();
    for model in models {
        let carried = counts.entry(model.provider_id.as_str()).or_insert(0);
        *carried = carried.saturating_add(1);
    }
    counts
}

/// Fits the flat model list without letting identities that sort first empty
/// the providers after them (decision 0093 and Core API 3.19).
///
/// Rows are dealt one per provider per round. A pinned model takes its
/// provider's first-round place. The selected indices are sorted back into the
/// catalog's stable order before the selected rows are cloned into the cache.
fn fit_to_budget<'a>(
    models: &[&'a CatalogModel],
    roster: &[ProviderView],
    budget: usize,
) -> Vec<&'a CatalogModel> {
    if models.len() <= budget {
        return models.to_vec();
    }
    let pinned = roster
        .iter()
        .filter_map(|view| {
            Some((
                view.provider_id.as_str(),
                view.preference.model_id.as_ref()?.as_str(),
            ))
        })
        .collect::<BTreeMap<_, _>>();
    let mut rounds: BTreeMap<&str, usize> = BTreeMap::new();
    let mut dealt: Vec<(usize, usize)> = Vec::with_capacity(models.len());
    for (index, model) in models.iter().enumerate() {
        let next = rounds.entry(model.provider_id.as_str()).or_insert(1);
        if pinned
            .get(model.provider_id.as_str())
            .is_some_and(|model_id| *model_id == model.model_id.as_str())
        {
            dealt.push((0, index));
        } else {
            dealt.push((*next, index));
            *next += 1;
        }
    }
    dealt.sort_unstable();
    let mut kept: Vec<usize> = dealt.into_iter().take(budget).map(|(_, at)| at).collect();
    kept.sort_unstable();
    kept.into_iter()
        .filter_map(|at| models.get(at).copied())
        .collect()
}

/// Every provider the roster can hold must keep at least one model in the
/// first round. Tie the three independently generated limits where the deal is
/// built so one cannot move silently past another.
const _: () = assert!(
    core_api_types::MAX_CUSTOM_PROVIDERS <= core_api_types::MAX_PROVIDER_ROSTER_ENTRIES
        && core_api_types::MAX_PROVIDER_ROSTER_ENTRIES
            <= core_api_types::MAX_PROVIDER_MODEL_ENTRIES,
    "every provider the roster can hold must be able to keep at least one model"
);
