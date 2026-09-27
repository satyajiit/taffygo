// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The person's own layer, projected and then read back through the merge.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use model_router::catalog::{CatalogLayer, Endpoint, ModelRole, PriceBasis};
use model_router::wire::ServerKind;
use model_router::{MergedCatalog, ModelId, ModelKey};

use super::{user_catalog_document, UNSTATED_CONTEXT_WINDOW, UNSTATED_MAX_OUTPUT_TOKENS};
use crate::provider::{CustomModel, CustomProvider, ProviderDisplayName, ProviderId};
use crate::provider::{ProviderWireApi, SavedCustomProvider};

fn baseline() -> model_router::catalog::types::CatalogDocument {
    model_router::embedded_baseline()
        .expect("the compiled baseline is valid")
        .0
        .document
}

fn model(model_id: &str, reasoning: bool, tool_calling: bool) -> CustomModel {
    CustomModel {
        model_id: ModelId::new(model_id).expect("valid"),
        display_name: "A model".to_owned(),
        context_window: 0,
        max_output_tokens: 0,
        reasoning,
        tool_calling,
    }
}

fn provider(models: Vec<CustomModel>, detected: Option<ServerKind>) -> CustomProvider {
    let saved = SavedCustomProvider {
        provider_id: ProviderId::new("my-gateway").expect("valid"),
        display_name: ProviderDisplayName::new("My gateway").expect("valid"),
        endpoint: Endpoint::new("https://gateway.example/v1").expect("valid"),
        wire_api: ProviderWireApi::OpenAiCompletions,
        credential: None,
        models,
        detected_server: detected,
    };
    CustomProvider {
        provider_id: saved.provider_id,
        display_name: saved.display_name,
        endpoint: saved.endpoint,
        wire_api: saved.wire_api,
        credential: saved.credential,
        models: saved.models,
        detected_server: saved.detected_server,
    }
}

fn layer(custom: &[CustomProvider]) -> model_router::catalog::types::CatalogDocument {
    user_catalog_document(&baseline(), custom.iter(), core::iter::empty())
}

#[test]
fn a_saved_endpoint_arrives_with_models_the_router_can_resolve() {
    let baseline = baseline();
    let custom = vec![provider(vec![model("gateway-large", false, true)], None)];
    let document = layer(&custom);
    let merged = MergedCatalog::build(&baseline, None, Some(&document));
    let key = ModelKey::new(
        model_router::ProviderId::new("my-gateway").expect("valid"),
        ModelId::new("gateway-large").expect("valid"),
    );
    let resolved = merged
        .resolve(&key)
        .expect("a person's own model resolves like any other");
    assert_eq!(
        merged.model_layer(&key),
        Some(CatalogLayer::UserOverride),
        "it is the person's own layer that supplies it"
    );
    assert_eq!(resolved.endpoint.as_str(), "https://gateway.example/v1");
}

#[test]
fn a_model_the_endpoint_did_not_measure_is_given_a_window_it_can_fit_in() {
    let document = layer(&[provider(vec![model("gateway-large", false, true)], None)]);
    let entry = document.models.first().expect("one model");
    assert_eq!(entry.context_window, UNSTATED_CONTEXT_WINDOW);
    assert_eq!(entry.max_output_tokens, UNSTATED_MAX_OUTPUT_TOKENS);
    assert!(
        entry.max_output_tokens <= entry.context_window,
        "a zero window would be a model that accepts nothing"
    );
}

#[test]
fn a_pair_of_limits_that_contradict_itself_drops_the_model() {
    let mut carried = model("gateway-large", false, true);
    carried.context_window = 4_096;
    carried.max_output_tokens = 8_192;
    let document = layer(&[provider(vec![carried], None)]);
    assert!(
        document.models.is_empty(),
        "an allowance larger than the window is not repaired into a smaller one"
    );
    assert_eq!(
        document.providers.len(),
        1,
        "the provider is still the person's, with nothing under it"
    );
}

#[test]
fn a_model_that_cannot_call_tools_is_not_cataloged_for_any_role() {
    let document = layer(&[provider(vec![model("chat-only", false, false)], None)]);
    assert!(
        document.models.is_empty(),
        "every task role is tool driven, and the catalog refuses the pair"
    );
}

#[test]
fn a_model_that_thinks_leads_the_reasoning_rung_alone() {
    let document = layer(&[provider(vec![model("thinker", true, true)], None)]);
    let entry = document.models.first().expect("one model");
    assert_eq!(entry.roles, vec![ModelRole::PrimaryReasoning]);
}

#[test]
fn a_model_that_does_not_think_serves_both_text_rungs() {
    let document = layer(&[provider(vec![model("quick", false, true)], None)]);
    let entry = document.models.first().expect("one model");
    assert_eq!(
        entry.roles,
        vec![ModelRole::PrimaryReasoning, ModelRole::FastBrowsing],
        "a person whose only server has one such model must still run a task"
    );
}

#[test]
fn a_persons_own_machine_is_priced_at_nothing_and_says_the_value_was_imputed() {
    let document = layer(&[provider(vec![model("quick", false, true)], None)]);
    let cost = &document.models.first().expect("one model").cost;
    assert_eq!(cost.basis, PriceBasis::Implied);
    assert_eq!(cost.input_micros_per_million, 0);
    assert_eq!(cost.output_micros_per_million, 0);
    assert_eq!(
        cost.currency,
        baseline().models.first().expect("priced").cost.currency,
        "a budget refuses a snapshot in a currency it was not set in"
    );
}

#[test]
fn the_layer_is_valid_against_the_catalogs_own_invariants() {
    let document = layer(&[provider(
        vec![model("quick", false, true), model("thinker", true, true)],
        Some(ServerKind::Ollama),
    )]);
    assert!(
        model_router::catalog::validate(&document).is_empty(),
        "the layer must describe a world the router can act in"
    );
}

#[test]
fn a_detected_server_travels_as_the_dialect_overrides_it_needs() {
    let document = layer(&[provider(
        vec![model("quick", false, true)],
        Some(ServerKind::Ollama),
    )]);
    let compat = &document.models.first().expect("one model").compat;
    assert_eq!(
        compat.get("answer_tokens_field").map(String::as_str),
        Some("max_tokens"),
        "the older dialect takes the allowance under its original name"
    );
    assert_eq!(
        compat.get("accepts_reasoning_effort").map(String::as_str),
        Some("false")
    );
}

#[test]
fn an_endpoint_that_detected_as_nothing_carries_no_overrides() {
    let document = layer(&[provider(vec![model("quick", false, true)], None)]);
    assert!(
        document
            .models
            .first()
            .expect("one model")
            .compat
            .is_empty(),
        "absence is the answer, not a sixth server wearing the platform's row"
    );
}

#[test]
fn the_layer_never_wins_an_ordering_against_a_real_document() {
    let baseline = baseline();
    let document = layer(&[]);
    assert!(
        document.header.generated_at < baseline.header.generated_at,
        "a document that was never generated anywhere must not order ahead of one that was"
    );
}
