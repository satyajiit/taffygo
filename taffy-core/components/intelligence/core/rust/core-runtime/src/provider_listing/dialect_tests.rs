// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Each dialect, read from a body shaped the way its vendor publishes one.
//!
//! The rows below are cut down from what each vendor actually answered on
//! 2026-09-03 — the keys are theirs, the values are theirs, and the fields
//! nothing reads are dropped so the fixture says what is load-bearing. The
//! point of testing them separately from the protocol is that the protocol
//! has one shape and the dialects have four, and a dialect that quietly stops
//! reading a key produces a *smaller* list rather than a failure.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use model_router::catalog::types::{CatalogDocument, InputModality, Model, ModelRole};
use model_router::MergedCatalog;

use super::{ListingCandidate, ProviderListingProtocol, ProviderListingVerdict};
use crate::wire;

const NOW: u64 = 1_800_000_000_000;
const GENERATION: u64 = 7;

fn baseline() -> CatalogDocument {
    model_router::embedded_baseline()
        .expect("the compiled baseline is valid")
        .0
        .document
}

/// Runs one body through the whole protocol as `provider_id` served it.
fn listed(provider_id: &str, rows: &[&str]) -> (ProviderListingVerdict, Vec<Model>) {
    let mut protocol = ProviderListingProtocol::default();
    let candidates = vec![ListingCandidate {
        provider_id: provider_id.to_owned(),
        endpoint: "https://vendor.example".to_owned(),
        wire_api: wire::ProviderWireApi::OpenAiCompletions,
        credential_handle: "handle-1".to_owned(),
    }];
    protocol
        .begin_refresh(&candidates, GENERATION, NOW)
        .expect("a fetch is due");
    let result = wire::ProviderListingFetchResult {
        provider_id: provider_id.to_owned(),
        disposition: wire::CatalogFetchDisposition::Success,
        body: format!(r#"{{"data":[{}]}}"#, rows.join(",")).into_bytes(),
    };
    let baseline = baseline();
    let verdict = protocol.deliver_fetch_result(
        &baseline,
        &MergedCatalog::from_baseline(&baseline),
        &result,
        NOW,
    );
    let models = protocol.models().cloned().collect();
    (verdict, models)
}

/// The one model a body of one row produced.
fn one(provider_id: &str, row: &str) -> Model {
    let (verdict, models) = listed(provider_id, &[row]);
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: provider_id.to_owned(),
            offered: 1,
            kept: 1,
        },
        "{provider_id} should have kept its one row"
    );
    models.into_iter().next().expect("one model")
}

// ---------------------------------------------------------------- venice ---

/// Venice's text row, with its facts under a `model_spec` of its own and its
/// rates quoted in dollars per million.
const VENICE_TEXT: &str = r#"{"id":"claude-sonnet-5","type":"text","object":"model",
    "context_length":1000000,
    "model_spec":{"availableContextTokens":200000,"maxCompletionTokens":64000,
      "pricing":{"input":{"usd":3,"diem":3},"output":{"usd":15,"diem":15},
        "cache_input":{"usd":0.3,"diem":0.3}},
      "capabilities":{"supportsFunctionCalling":true,"supportsReasoning":true,
        "supportsVision":true,"supportsWebSearch":true}}}"#;

#[test]
fn venice_states_its_facts_under_its_own_model_spec() {
    let model = one("venice", VENICE_TEXT);
    assert_eq!(model.model_id.as_str(), "claude-sonnet-5");
    // The spec's own window wins over the row's, which is the larger of the
    // two here on purpose: reading the wrong one would publish a window the
    // vendor does not serve and every long request would fail at the vendor.
    assert_eq!(model.context_window, 200_000);
    assert_eq!(model.max_output_tokens, 64_000);
    assert!(model.reasoning);
    assert!(model.tool_calling);
    // Quoted per million, so three dollars is three million micro-units. Read
    // in the per-token dialect it would have been three million times that.
    assert_eq!(model.cost.input_micros_per_million, 3_000_000);
    assert_eq!(model.cost.output_micros_per_million, 15_000_000);
    assert_eq!(model.cost.cache_read_micros_per_million, 300_000);
    assert_eq!(
        model.cost.cache_write_micros_per_million, 3_000_000,
        "venice states no cache-write rate, so it takes the input rate"
    );
}

#[test]
fn a_stated_image_modality_is_a_role_and_a_modality_together() {
    let model = one("venice", VENICE_TEXT);
    assert!(model.roles.contains(&ModelRole::Vision));
    assert!(
        model.input_modalities.contains(&InputModality::Image),
        "the catalog refuses a vision role without the modality it needs"
    );
    assert!(
        model.roles.contains(&ModelRole::PrimaryReasoning),
        "accepting a picture does not stop a model planning"
    );
}

#[test]
fn venice_serves_pictures_and_speech_from_the_same_list_and_they_are_not_models() {
    let image = r#"{"id":"venice-sd35","type":"image","object":"model",
        "model_spec":{"pricing":{"input":{"usd":1},"output":{"usd":1}},
          "capabilities":{"supportsFunctionCalling":true}}}"#;
    let (verdict, models) = listed("venice", &[image, VENICE_TEXT]);
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: "venice".to_owned(),
            offered: 2,
            kept: 1,
        }
    );
    assert_eq!(models.len(), 1);
    assert_eq!(models[0].model_id.as_str(), "claude-sonnet-5");
}

// ---------------------------------------------------------------- novita ---

#[test]
fn novita_records_the_dearer_of_the_two_prices_it_quotes() {
    // The vendor quotes a standing price and a discounted one. A discount is
    // a condition, and decision 0094 section 2 records a conditional price at
    // the higher value.
    let row = r#"{"id":"zai-org/glm-5.3-flash","model_type":"chat","object":"model",
        "context_size":1048576,"max_output_tokens":131072,
        "features":["function-calling","reasoning","serverless"],
        "input_modalities":["text","image"],"output_modalities":["text"],
        "pricing":{"prompt":{"origin_price_per_m_decimal":"0.15","price_per_m_decimal":"0.075"},
          "completion":{"origin_price_per_m_decimal":"0.5","price_per_m_decimal":"0.25"},
          "input_cache_read":{"origin_price_per_m_decimal":"0.03","price_per_m_decimal":"0.015"}}}"#;
    let model = one("novita", row);
    assert_eq!(model.cost.input_micros_per_million, 150_000);
    assert_eq!(model.cost.output_micros_per_million, 500_000);
    assert_eq!(model.cost.cache_read_micros_per_million, 30_000);
    assert_eq!(model.context_window, 1_048_576);
    assert_eq!(model.max_output_tokens, 131_072);
    assert!(model.roles.contains(&ModelRole::Vision));
}

#[test]
fn a_novita_row_that_calls_no_tools_serves_no_role_and_is_dropped() {
    let row = r#"{"id":"vendor/plain","model_type":"chat","object":"model",
        "context_size":8192,"max_output_tokens":1024,
        "features":["serverless"],"input_modalities":["text"],
        "pricing":{"prompt":{"price_per_m_decimal":"1"},
          "completion":{"price_per_m_decimal":"2"}}}"#;
    let (verdict, models) = listed("novita", &[row]);
    assert_eq!(verdict, ProviderListingVerdict::RejectedEmpty);
    assert!(models.is_empty());
}

#[test]
fn novita_serves_more_than_chat_models_and_only_the_chat_ones_are_read() {
    let speech = r#"{"id":"vendor/voice","model_type":"tts","object":"model",
        "context_size":8192,"max_output_tokens":1024,
        "features":["function-calling"],"input_modalities":["text"],
        "pricing":{"prompt":{"price_per_m_decimal":"1"},
          "completion":{"price_per_m_decimal":"2"}}}"#;
    let (verdict, models) = listed("novita", &[speech]);
    assert_eq!(verdict, ProviderListingVerdict::RejectedEmpty);
    assert!(models.is_empty());
}

// ---------------------------------------------------------------- chutes ---

#[test]
fn chutes_spells_a_per_million_rate_exactly_as_the_default_dialect_spells_a_per_token_one() {
    // The whole reason a dialect is chosen by name. This body is valid in
    // both readings and means two things a million apart.
    let row = r#"{"id":"Qwen/Qwen3-32B","object":"model","context_length":40960,
        "max_output_length":40960,"input_modalities":["text"],
        "output_modalities":["text"],
        "supported_features":["json_mode","tools","reasoning"],
        "pricing":{"prompt":0.104,"completion":0.416,"input_cache_read":0.0104}}"#;
    let model = one("chutes", row);
    assert_eq!(model.cost.input_micros_per_million, 104_000);
    assert_eq!(model.cost.output_micros_per_million, 416_000);
    assert_eq!(model.cost.cache_read_micros_per_million, 10_400);
    assert_eq!(model.context_window, 40_960);
    assert!(model.reasoning);
    assert!(model.tool_calling);
    assert!(
        !model.roles.contains(&ModelRole::Vision),
        "this row accepts text alone"
    );

    // Read under the dialect that has always been here, this exact row is
    // refused outright — it states its capabilities under a key that dialect
    // does not read, so nothing about it is believed.
    let (verdict, models) = listed("openrouter", &[row]);
    assert_eq!(verdict, ProviderListingVerdict::RejectedEmpty);
    assert!(models.is_empty());
}

#[test]
fn the_same_rate_means_two_things_a_million_apart_in_two_dialects() {
    // The hazard the name dispatch exists for, made explicit. This row is
    // deliberately readable in *both* dialects: it states its capabilities
    // twice, under the key each one reads. Nothing a vendor sends looks quite
    // like this — what a vendor sends is one half or the other, and the half
    // it sends does not say which scale it meant.
    let ambiguous = r#"{"id":"vendor/alpha","context_length":40960,
        "max_output_length":8192,"max_completion_tokens":8192,
        "input_modalities":["text"],
        "supported_features":["tools"],"supported_parameters":["tools"],
        "pricing":{"prompt":0.104,"completion":0.416}}"#;
    let per_million = one("chutes", ambiguous);
    let per_token = one("openrouter", ambiguous);
    assert_eq!(per_million.cost.input_micros_per_million, 104_000);
    assert_eq!(per_token.cost.input_micros_per_million, 104_000_000_000);
    assert_eq!(
        per_token.cost.input_micros_per_million,
        per_million.cost.input_micros_per_million * 1_000_000,
        "one of these is the vendor's price and the other is a millionfold          error in whichever direction the dialect was guessed wrong"
    );
}

// ------------------------------------------------------- the default one ---

#[test]
fn a_repeating_rate_rounds_up_by_one_unit_rather_than_dropping_the_model() {
    // What a vendor publishes when it divides a rate and serialises the
    // result through a binary float. It used to refuse the row, which lost
    // sixteen live models out of one aggregator's list on every device.
    let row = r#"{"id":"google/gemini-3.6-flash","context_length":1048576,
        "max_completion_tokens":65536,"supported_parameters":["tools","reasoning"],
        "pricing":{"prompt":"0.0000003","completion":"0.0000025",
          "input_cache_write":"0.0000000416666666666667"}}"#;
    let model = one("openrouter", row);
    assert_eq!(model.cost.input_micros_per_million, 300_000);
    assert_eq!(
        model.cost.cache_write_micros_per_million, 41_667,
        "0.0000000416666… per token is 41_666.66… micro-units, kept at the \
         dearer whole one so a cap can only ever fire early"
    );
}

#[test]
fn the_listing_dialect_reads_the_limits_and_the_modality_one_level_down() {
    // Exactly the shape both gateways publish: the window twice, the answer
    // allowance *only* under `top_provider`, and the modalities under
    // `architecture`. Reading the row's own fields alone gave every model in
    // both lists the standing default allowance.
    let row = r#"{"id":"google/gemini-3.6-flash","context_length":1048576,
        "architecture":{"input_modalities":["text","image","pdf"],
          "output_modalities":["text"],"tokenizer":"Other"},
        "top_provider":{"context_length":1048576,"max_completion_tokens":65536,
          "is_moderated":true},
        "supported_parameters":["tools","reasoning","temperature"],
        "pricing":{"prompt":"0.0000003","completion":"0.0000025"}}"#;
    let model = one("openrouter", row);
    assert_eq!(model.context_window, 1_048_576);
    assert_eq!(
        model.max_output_tokens, 65_536,
        "the vendor stated it, so the standing default is not what it means"
    );
    assert!(model.roles.contains(&ModelRole::Vision));
    assert!(model.input_modalities.contains(&InputModality::Image));

    // A row that states neither still gets the standing defaults, which is
    // the behaviour every such row had before this dialect looked deeper.
    let bare = r#"{"id":"vendor/bare","supported_parameters":["tools"],
        "pricing":{"prompt":"0.000001","completion":"0.000002"}}"#;
    let plain = one("openrouter", bare);
    assert_eq!(plain.context_window, 8_192);
    assert_eq!(plain.max_output_tokens, 2_048);
    assert!(!plain.roles.contains(&ModelRole::Vision));
}

#[test]
fn a_spelling_this_build_cannot_read_is_still_refused() {
    // Rounding is for a number this build can read too precisely. A sign or
    // an exponent is a spelling it cannot read at all, and reading one wrongly
    // is a price.
    for unreadable in [
        r#"{"id":"vendor/exp","context_length":8192,"supported_parameters":["tools"],
            "pricing":{"prompt":"1e-6","completion":"0.000006"}}"#,
        r#"{"id":"vendor/neg","context_length":8192,"supported_parameters":["tools"],
            "pricing":{"prompt":"-1","completion":"-1"}}"#,
    ] {
        let (verdict, models) = listed("openrouter", &[unreadable]);
        assert_eq!(verdict, ProviderListingVerdict::RejectedEmpty);
        assert!(models.is_empty());
    }
}
