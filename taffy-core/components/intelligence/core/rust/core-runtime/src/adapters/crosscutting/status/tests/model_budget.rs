// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the flat model list does when the providers ask for more than it holds.
//!
//! The old projection cut the list in identity order, so the overrun fell
//! entirely on whoever sorted last: not a short roster but an empty one, which
//! a screen cannot tell from a provider that carries no models. Every case
//! below is written so it would have failed against that cut, and the counts
//! are spelled out rather than derived so a change to the deal has to restate
//! what it now believes.

use std::collections::{BTreeMap, BTreeSet};

use model_router::catalog::CatalogLayer;
use model_router::ModelId;

use super::{fixture, provider_id};
use crate::adapters::crosscutting::{ProductionProviderModels, ProductionProviderRoster};
use crate::ports::{StatusContributionFacts, StatusContributorPort};
use crate::provider::{
    CatalogModel, ProviderAuthMethod, ProviderModelPreference, ProviderOrigin,
    ProviderPresentation, ProviderStatusProjection, ProviderView,
};

/// One catalog row, carrying only what the budget cares about.
fn model(provider: &str, index: usize) -> CatalogModel {
    CatalogModel {
        provider_id: provider_id(provider),
        model_id: ModelId::new(&format!("{provider}-m{index:03}")).expect("a valid model id"),
        display_name: format!("{provider} model {index}"),
        context_window: 131_072,
        max_output_tokens: 8_192,
        reasoning: false,
        tool_calling: true,
        roles: Vec::new(),
        input_modalities: Vec::new(),
        thinking_levels: Vec::new(),
    }
}

/// One provider's rows, in the catalog order the plane hands them over in.
fn models(provider: &str, count: usize) -> Vec<CatalogModel> {
    (0..count).map(|index| model(provider, index)).collect()
}

/// The overrun this closes, at the shape the product now admits.
///
/// Thirteen providers and 397 rows against a 256-row surface: two published
/// vendors with short lists, ten of a person's own endpoints at the full
/// `MAX_CUSTOM_MODEL_ENTRIES`, and one aggregator whose identity sorts last.
/// `MAX_CUSTOM_PROVIDERS` × `MAX_CUSTOM_MODEL_ENTRIES` alone is 1024, so this
/// is a third of what one person can reach.
fn overrunning_catalog() -> Vec<CatalogModel> {
    let mut rows = models("anthropic", 2);
    rows.extend(models("cerebras", 11));
    for gateway in 0..10 {
        rows.extend(models(&format!("gateway-{gateway:02}"), 32));
    }
    rows.extend(models("zz-aggregator", 64));
    rows
}

/// One roster row, with nothing pinned; only the identity is read here.
fn listed(provider: &str) -> ProviderView {
    ProviderView {
        provider_id: provider_id(provider),
        display_name: provider.to_owned(),
        origin: ProviderOrigin::Custom,
        auth_methods: vec![ProviderAuthMethod::ApiKey],
        stored: None,
        signing_in: false,
        enabled: true,
        endpoint_host: None,
        endpoint_base: None,
        last_refusal: None,
        configurable: true,
        subscription: false,
        endpoint_changed: false,
        refused_endpoint_host: None,
        catalog_layer: CatalogLayer::UserOverride,
        preference: ProviderModelPreference::default(),
        presentation: ProviderPresentation::default(),
    }
}

/// The roster row a pinned-model case needs; nothing else reads the roster.
fn pinning(provider: &str, model_id: &str) -> ProviderView {
    ProviderView {
        preference: ProviderModelPreference {
            model_id: Some(ModelId::new(model_id).expect("a valid model id")),
            thinking: None,
        },
        ..listed(provider)
    }
}

/// Runs the contributor and returns how many rows each provider kept.
fn projected(models: &[CatalogModel], roster: &[ProviderView]) -> BTreeMap<String, usize> {
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::for_testing(roster, models);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &[],
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    let mut status = fixture();
    ProductionProviderModels.contribute(&facts, &mut status);
    let mut kept: BTreeMap<String, usize> = BTreeMap::new();
    for entry in &status.provider_models {
        *kept.entry(entry.provider_id.clone()).or_default() += 1;
    }
    kept
}

#[test]
fn no_provider_is_emptied_by_the_lists_that_sort_before_it() {
    // The defect, stated as the thing a person sees: `zz-aggregator` sorts
    // last of thirteen and the 256 rows are gone long before the projection
    // reaches it, so `take(MAX_PROVIDER_MODEL_ENTRIES)` left it with nothing
    // and its picker was empty — indistinguishable from a provider that
    // carries no models at all. Under the old cut this assertion fails on
    // every provider from `gateway-08` onward.
    let catalog = overrunning_catalog();
    let kept = projected(&catalog, &[]);
    let asked: BTreeSet<String> = catalog
        .iter()
        .map(|model| model.provider_id.as_str().to_owned())
        .collect();
    assert_eq!(asked.len(), 13);
    for provider in &asked {
        assert!(
            kept.get(provider).copied().unwrap_or(0) > 0,
            "{provider} was emptied by somebody else's model count"
        );
    }
}

#[test]
fn a_short_list_survives_the_cut_whole() {
    // Two published vendors with 2 and 11 rows keep every row, however long
    // the endpoints beside them are: the deal takes the tail of the longest
    // lists, and a short list has no tail to take.
    let kept = projected(&overrunning_catalog(), &[]);
    assert_eq!(kept.get("anthropic").copied(), Some(2));
    assert_eq!(kept.get("cerebras").copied(), Some(11));
}

#[test]
fn the_long_lists_are_cut_to_within_one_row_of_each_other() {
    // What "proportionately rather than alphabetically" has to mean: the ten
    // full endpoints and the 64-row aggregator all land within one row of each
    // other, and the one extra row is the integer remainder of the last round
    // rather than a whole provider's list.
    let kept = projected(&overrunning_catalog(), &[]);
    let cut: Vec<usize> = kept
        .iter()
        .filter(|(provider, _)| provider.starts_with("gateway-") || *provider == "zz-aggregator")
        .map(|(_, count)| *count)
        .collect();
    assert_eq!(cut.len(), 11);
    let highest = cut.iter().max().copied().unwrap_or(0);
    let lowest = cut.iter().min().copied().unwrap_or(0);
    assert_eq!((lowest, highest), (22, 23));
}

#[test]
fn the_budget_is_spent_exactly_when_the_lists_overrun_it() {
    // The coarse truncation signal, and it has to stay exact: a full list means
    // rows may be missing, a short one means these are all the models there
    // are. Which provider lost how many is the roster's `model_count`, pinned
    // by the two cases below.
    let kept = projected(&overrunning_catalog(), &[]);
    let total: usize = kept.values().sum();
    assert_eq!(total, core_api_types::MAX_PROVIDER_MODEL_ENTRIES);
}

#[test]
fn the_roster_counts_what_the_catalog_carries_not_what_survived_the_cut() {
    // The whole point of Core API 3.19. `zz-aggregator` asks for 64 rows and
    // keeps 22 or 23 of them; a count taken from `provider_models` would say
    // 22, which is the number the surface already has. The count has to be the
    // one it is missing, so it is taken from the catalog the facts carry.
    let catalog = overrunning_catalog();
    let roster: Vec<ProviderView> = asked_of(&catalog).keys().map(|id| listed(id)).collect();
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::for_testing(&roster, &catalog);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &[],
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    let mut status = fixture();
    ProductionProviderModels.contribute(&facts, &mut status);
    ProductionProviderRoster.contribute(&facts, &mut status);

    let asked = asked_of(&catalog);
    for entry in &status.provider_roster {
        assert_eq!(
            usize::try_from(entry.model_count).unwrap_or(usize::MAX),
            asked.get(entry.provider_id.as_str()).copied().unwrap_or(0),
            "{} was counted after the deal rather than before it",
            entry.provider_id
        );
    }
    let aggregator = status
        .provider_roster
        .iter()
        .find(|entry| entry.provider_id == "zz-aggregator")
        .expect("the aggregator is on the roster");
    assert_eq!(aggregator.model_count, 64);
    let survived = projected(&catalog, &roster)
        .get("zz-aggregator")
        .copied()
        .unwrap_or(0);
    assert!(
        survived < 64,
        "this case only means something while the aggregator is being cut"
    );
}

#[test]
fn the_count_does_not_move_when_the_two_contributors_run_the_other_way_round() {
    // The capture point stated as a property rather than as a comment: the
    // deal builds a new list and never shortens the facts, so no ordering of
    // the contributors can turn the catalog count into a count of survivors.
    let catalog = overrunning_catalog();
    let roster: Vec<ProviderView> = asked_of(&catalog).keys().map(|id| listed(id)).collect();
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::for_testing(&roster, &catalog);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &[],
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };

    let mut models_first = fixture();
    ProductionProviderModels.contribute(&facts, &mut models_first);
    ProductionProviderRoster.contribute(&facts, &mut models_first);

    let mut roster_first = fixture();
    ProductionProviderRoster.contribute(&facts, &mut roster_first);
    ProductionProviderModels.contribute(&facts, &mut roster_first);

    assert_eq!(models_first, roster_first);
}

/// How many rows each provider asked for, straight off the catalog.
fn asked_of(catalog: &[CatalogModel]) -> BTreeMap<&str, usize> {
    let mut asked: BTreeMap<&str, usize> = BTreeMap::new();
    for model in catalog {
        *asked.entry(model.provider_id.as_str()).or_default() += 1;
    }
    asked
}

#[test]
fn nothing_is_cut_or_reordered_while_the_lists_fit() {
    let mut catalog = models("anthropic", 2);
    catalog.extend(models("zz-aggregator", 64));
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::for_testing(&[], &catalog);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &[],
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    let mut status = fixture();
    ProductionProviderModels.contribute(&facts, &mut status);
    assert_eq!(status.provider_models.len(), catalog.len());
    assert!(status.provider_models.len() < core_api_types::MAX_PROVIDER_MODEL_ENTRIES);
    let projected_ids: Vec<&str> = status
        .provider_models
        .iter()
        .map(|entry| entry.model_id.as_str())
        .collect();
    let asked_ids: Vec<&str> = catalog
        .iter()
        .map(|model| model.model_id.as_str())
        .collect();
    assert_eq!(
        projected_ids, asked_ids,
        "the plane's order is the surface's"
    );
}

#[test]
fn a_pinned_model_survives_a_cut_that_reaches_its_provider() {
    // `gateway-09` keeps 22 of its 32 rows, so its last row is well past the
    // cut. The same snapshot's roster names that row as the standing choice,
    // and a picker showing a chosen model it has no row for is this defect one
    // layer down.
    let roster = vec![pinning("gateway-09", "gateway-09-m031")];
    let catalog = overrunning_catalog();
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::for_testing(&roster, &catalog);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &[],
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    let mut status = fixture();
    ProductionProviderModels.contribute(&facts, &mut status);
    assert!(
        status
            .provider_models
            .iter()
            .any(|entry| entry.model_id == "gateway-09-m031"),
        "the model the roster says is chosen must have a row to show"
    );
    // And it costs its provider nothing: the pin takes a place in that
    // provider's own deal rather than a place from anybody else's.
    let total: usize = projected(&catalog, &roster).values().sum();
    assert_eq!(total, core_api_types::MAX_PROVIDER_MODEL_ENTRIES);
}
