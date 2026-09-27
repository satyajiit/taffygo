// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one write of a person's own provider does, and what it refuses.
//!
//! Split from the plane's other tests the way the code was: a catalog
//! credential and an endpoint somebody typed are different subjects, and this
//! half is the one that carries models with it.

use model_router::wire::ServerKind;
use model_router::ModelId;

use super::{endpoint, handle, id, name, plane, saved};
use crate::provider::{
    CustomModel, ProviderEffect, ProviderError, ProviderWireApi, SavedCustomProvider,
    MAX_CUSTOM_MODELS, MAX_CUSTOM_PROVIDERS,
};

#[test]
fn a_custom_provider_is_one_write() {
    let mut plane = plane();
    plane
        .save_custom_provider(saved(
            id("my-gateway"),
            name("My gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            Some(handle("k")),
        ))
        .expect("defined");
    // The definition and the credential arrive together, so there is no moment
    // at which the provider exists without the key it needs.
    assert_eq!(plane.custom_providers().count(), 1);
    assert_eq!(plane.credentials().count(), 1);
    assert_eq!(
        plane.custom_endpoints(),
        vec![endpoint("https://gateway.example/v1")]
    );
}

#[test]
fn a_custom_provider_may_need_no_credential_at_all() {
    let mut plane = plane();
    let effect = plane
        .save_custom_provider(saved(
            id("my-local"),
            name("My local runner"),
            endpoint("https://localhost.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            None,
        ))
        .expect("defined");
    assert_eq!(effect, ProviderEffect::None);
    assert_eq!(plane.credentials().count(), 0);
    assert_eq!(plane.custom_providers().count(), 1);
}

#[test]
fn a_custom_provider_may_not_take_a_catalog_identity() {
    let mut plane = plane();
    assert_eq!(
        plane.save_custom_provider(saved(
            id("anthropic"),
            name("Not really Anthropic"),
            endpoint("https://elsewhere.example/v1"),
            ProviderWireApi::AnthropicMessages,
            Some(handle("k")),
        )),
        Err(ProviderError::ProviderIdReserved),
        "shadowing a catalog vendor would send its key somewhere else"
    );
}

#[test]
fn custom_providers_are_bounded() {
    let mut plane = plane();
    for index in 0..MAX_CUSTOM_PROVIDERS {
        plane
            .save_custom_provider(saved(
                id(&format!("gateway-{index}")),
                name("Gateway"),
                endpoint("https://gateway.example/v1"),
                ProviderWireApi::OpenAiCompletions,
                None,
            ))
            .expect("within the bound");
    }
    assert_eq!(
        plane.save_custom_provider(saved(
            id("one-too-many"),
            name("Gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            None,
        )),
        Err(ProviderError::TooManyCustomProviders)
    );
    // Replacing one already defined is not a new one and stays allowed at the
    // bound, or a person at the limit could never correct a typo.
    assert!(plane
        .save_custom_provider(saved(
            id("gateway-0"),
            name("Gateway renamed"),
            endpoint("https://gateway.example/v2"),
            ProviderWireApi::OpenAiCompletions,
            None,
        ))
        .is_ok());
}

#[test]
fn removing_a_custom_provider_releases_its_credential() {
    let mut plane = plane();
    plane
        .save_custom_provider(saved(
            id("my-gateway"),
            name("My gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            Some(handle("k")),
        ))
        .expect("defined");
    let effect = plane
        .remove_custom_provider(&id("my-gateway"))
        .expect("defined above");
    assert_eq!(effect, ProviderEffect::ReleaseHandle(handle("k")));
    assert_eq!(plane.credentials().count(), 0);
    assert!(plane.custom_endpoints().is_empty());
}

#[test]
fn replacing_a_custom_provider_with_a_new_key_releases_the_old_one() {
    let mut plane = plane();
    plane
        .save_custom_provider(saved(
            id("my-gateway"),
            name("My gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            Some(handle("old")),
        ))
        .expect("defined");
    let effect = plane
        .save_custom_provider(saved(
            id("my-gateway"),
            name("My gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            Some(handle("new")),
        ))
        .expect("replaced");
    assert_eq!(effect, ProviderEffect::ReleaseHandle(handle("old")));
}

#[test]
fn a_handle_still_in_use_elsewhere_is_not_released() {
    // Two of a person's own providers may legitimately share one key. Releasing
    // on the first removal would revoke the second one's credential too.
    let mut plane = plane();
    for slug in ["gateway-a", "gateway-b"] {
        plane
            .save_custom_provider(saved(
                id(slug),
                name("Gateway"),
                endpoint("https://gateway.example/v1"),
                ProviderWireApi::OpenAiCompletions,
                Some(handle("shared")),
            ))
            .expect("defined");
    }
    let effect = plane
        .save_custom_provider(saved(
            id("gateway-a"),
            name("Gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            None,
        ))
        .expect("cleared");
    assert_eq!(
        effect,
        ProviderEffect::None,
        "gateway-b still names that handle"
    );
}

/// One model as a probe reports it.
fn model(model_id: &str) -> CustomModel {
    CustomModel {
        model_id: ModelId::new(model_id).expect("valid"),
        display_name: "A model".to_owned(),
        context_window: 131_072,
        max_output_tokens: 8_192,
        reasoning: false,
        tool_calling: true,
    }
}

/// One save carrying `models` and a detected runtime.
fn with_models(models: Vec<CustomModel>) -> SavedCustomProvider {
    SavedCustomProvider {
        models,
        detected_server: Some(ServerKind::Ollama),
        ..saved(
            id("my-gateway"),
            name("My gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            None,
        )
    }
}

#[test]
fn a_saved_endpoint_keeps_the_models_and_the_runtime_it_arrived_with() {
    let mut plane = plane();
    plane
        .save_custom_provider(with_models(vec![model("gateway-large")]))
        .expect("defined");
    let entry = plane.custom_providers().next().expect("one provider");
    assert_eq!(entry.models.len(), 1);
    assert_eq!(
        entry.models.first().expect("one").model_id.as_str(),
        "gateway-large"
    );
    assert_eq!(entry.detected_server, Some(ServerKind::Ollama));
}

#[test]
fn an_endpoint_with_nothing_loaded_behind_it_is_saved_and_says_so() {
    let mut plane = plane();
    plane
        .save_custom_provider(with_models(Vec::new()))
        .expect("an address that is right and empty is still an address");
    assert!(plane
        .custom_providers()
        .next()
        .expect("one provider")
        .models
        .is_empty());
}

#[test]
fn more_models_than_one_provider_may_carry_are_refused_whole() {
    let mut plane = plane();
    let models: Vec<CustomModel> = (0..=MAX_CUSTOM_MODELS)
        .map(|index| model(&format!("model-{index}")))
        .collect();
    assert_eq!(
        plane.save_custom_provider(with_models(models)),
        Err(ProviderError::TooManyCustomModels)
    );
    assert_eq!(
        plane.custom_providers().count(),
        0,
        "a refused save leaves nothing half written"
    );
}

#[test]
fn a_refused_replacement_leaves_the_models_the_provider_already_had() {
    let mut plane = plane();
    plane
        .save_custom_provider(with_models(vec![model("gateway-large")]))
        .expect("defined");
    let too_many: Vec<CustomModel> = (0..=MAX_CUSTOM_MODELS)
        .map(|index| model(&format!("model-{index}")))
        .collect();
    assert_eq!(
        plane.save_custom_provider(with_models(too_many)),
        Err(ProviderError::TooManyCustomModels)
    );
    assert_eq!(
        plane
            .custom_providers()
            .next()
            .expect("still defined")
            .models
            .len(),
        1,
        "a half-applied replacement would empty the provider and then fail"
    );
}

#[test]
fn a_plain_http_address_on_somebody_s_own_network_is_saved_and_shown() {
    // The shape a phone actually saves, which no other test in this file
    // carries: `endpoint()` above builds addresses through `Endpoint::new`,
    // the catalog's https-only rule, and a model server somebody runs on
    // their own network answers on plain http at an address on the local
    // subnet. The save path builds it through `user_base_url` for exactly
    // that reason (decision 0096 section 2), so the plane has to be driven
    // the same way to be under test at all.
    let mut plane = plane();
    let effect = plane
        .save_custom_provider(SavedCustomProvider {
            provider_id: id("bench"),
            display_name: name("Bench"),
            endpoint: model_router::catalog::Endpoint::user_base_url("http://192.168.1.39:8099/v1")
                .expect("a person's own address"),
            wire_api: ProviderWireApi::OpenAiCompletions,
            credential: Some(handle("bench")),
            models: vec![
                CustomModel {
                    model_id: ModelId::new("taffy-local-reasoner").expect("model id"),
                    display_name: "taffy-local-reasoner".to_owned(),
                    context_window: 32_768,
                    max_output_tokens: 0,
                    reasoning: false,
                    tool_calling: false,
                },
                CustomModel {
                    model_id: ModelId::new("taffy-local-fast").expect("model id"),
                    display_name: "taffy-local-fast".to_owned(),
                    context_window: 8_192,
                    max_output_tokens: 0,
                    reasoning: false,
                    tool_calling: false,
                },
            ],
            detected_server: Some(ServerKind::Vllm),
        })
        .expect("a person's own address is not held to the catalog's rule");
    // Nothing was replaced, so no handle is orphaned by this write.
    assert!(matches!(effect, ProviderEffect::None));
    assert_eq!(plane.custom_providers().count(), 1);
    assert_eq!(plane.credentials().count(), 1);
    // And it reaches the roster whole: the address a person typed, port and
    // base path included, is what the edit screen puts back in the field.
    let row = plane
        .roster()
        .into_iter()
        .find(|view| view.provider_id == id("bench"))
        .expect("a saved provider is on the roster");
    assert_eq!(
        row.endpoint_base.as_deref(),
        Some("http://192.168.1.39:8099/v1")
    );
    // The host carries the port, because a machine on a home network is
    // reached at one and a disclosure line naming the address without it
    // would name a different service.
    assert_eq!(row.endpoint_host.as_deref(), Some("192.168.1.39:8099"));
}
