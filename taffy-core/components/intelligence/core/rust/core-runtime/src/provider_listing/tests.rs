// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One provider's own model list, driven through every verdict it can reach.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use model_router::catalog::types::CatalogDocument;
use model_router::MergedCatalog;

use super::{
    ListingCandidate, ProviderListingProtocol, ProviderListingVerdict, MAX_LISTED_MODELS,
    MAX_LISTING_RESPONSE_BYTES,
};
use crate::wire;

/// The provider the compiled baseline ships as serving its own model list.
const LISTED: &str = "openrouter";
const NOW: u64 = 1_800_000_000_000;
const GENERATION: u64 = 4;

fn baseline() -> CatalogDocument {
    model_router::embedded_baseline()
        .expect("the compiled baseline is valid")
        .0
        .document
}

fn merged(baseline: &CatalogDocument) -> MergedCatalog {
    MergedCatalog::from_baseline(baseline)
}

fn candidates() -> Vec<ListingCandidate> {
    vec![ListingCandidate {
        provider_id: LISTED.to_owned(),
        endpoint: "https://openrouter.ai/api/v1".to_owned(),
        wire_api: wire::ProviderWireApi::OpenAiCompletions,
        credential_handle: "handle-1".to_owned(),
    }]
}

/// One listing row as an aggregator publishes it.
fn row(id: &str) -> String {
    format!(
        r#"{{"id":"{id}","context_length":131072,"max_completion_tokens":8192,
           "supported_parameters":["tools","temperature"],
           "pricing":{{"prompt":"0.0000012","completion":"0.000006"}}}}"#
    )
}

fn body(rows: &[String]) -> Vec<u8> {
    format!(r#"{{"data":[{}]}}"#, rows.join(",")).into_bytes()
}

fn success(rows: &[String]) -> wire::ProviderListingFetchResult {
    wire::ProviderListingFetchResult {
        provider_id: LISTED.to_owned(),
        disposition: wire::CatalogFetchDisposition::Success,
        body: body(rows),
    }
}

fn refused(disposition: wire::CatalogFetchDisposition) -> wire::ProviderListingFetchResult {
    wire::ProviderListingFetchResult {
        provider_id: LISTED.to_owned(),
        disposition,
        body: Vec::new(),
    }
}

/// Plans one fetch and delivers `result` against it.
fn round_trip(
    protocol: &mut ProviderListingProtocol,
    result: &wire::ProviderListingFetchResult,
) -> ProviderListingVerdict {
    let baseline = baseline();
    protocol
        .begin_refresh(&candidates(), GENERATION, NOW)
        .expect("a fetch is due");
    protocol.deliver_fetch_result(&baseline, &merged(&baseline), result, NOW)
}

#[test]
fn the_first_poke_plans_one_fetch_and_a_second_waits_on_it() {
    let mut protocol = ProviderListingProtocol::default();
    let effect = protocol
        .begin_refresh(&candidates(), GENERATION, NOW)
        .expect("a fetch is due");
    assert_eq!(effect.kind, wire::EffectKind::FetchProviderListing);
    let fetch = effect
        .provider_listing_fetch
        .expect("the body its kind names");
    assert_eq!(fetch.provider_id, LISTED);
    assert_eq!(fetch.max_response_bytes, MAX_LISTING_RESPONSE_BYTES);
    assert_eq!(fetch.credential_handle.as_deref(), Some("handle-1"));
    assert!(
        protocol
            .begin_refresh(&candidates(), GENERATION, NOW)
            .is_none(),
        "one fetch in flight, never two"
    );
}

#[test]
fn a_provider_with_no_credential_is_never_a_candidate() {
    // The precondition lives with the caller that can see both planes, so the
    // protocol's half of it is simply that an empty candidate list plans
    // nothing at all — no credential, no fetch.
    let mut protocol = ProviderListingProtocol::default();
    assert!(protocol.begin_refresh(&[], GENERATION, NOW).is_none());
}

#[test]
fn the_effect_identity_names_the_provider_and_not_a_position() {
    let mut protocol = ProviderListingProtocol::default();
    let effect = protocol
        .begin_refresh(&candidates(), GENERATION, NOW)
        .expect("due");
    assert!(
        effect.effect_id.contains(LISTED),
        "an identity taken from a position collides the moment the list shortens"
    );
}

#[test]
fn a_fetched_listing_becomes_models_under_the_provider_that_served_it() {
    let mut protocol = ProviderListingProtocol::default();
    let verdict = round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: LISTED.to_owned(),
            offered: 1,
            kept: 1,
        }
    );
    let model = protocol.models().next().expect("one model");
    assert_eq!(model.model_id.as_str(), "vendor/alpha");
    assert_eq!(model.provider_id.as_str(), LISTED);
    assert_eq!(model.context_window, 131_072);
    assert!(model.tool_calling);
    assert_eq!(
        model.endpoint, None,
        "a listing describes models, never where to reach them"
    );
}

#[test]
fn a_stated_price_is_converted_exactly_into_the_catalogs_own_units() {
    let mut protocol = ProviderListingProtocol::default();
    round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    let cost = &protocol.models().next().expect("one model").cost;
    // A millionth of a millionth of a currency unit per token is one
    // micro-unit per million tokens, so 0.0000012 per token is 1_200_000.
    assert_eq!(cost.input_micros_per_million, 1_200_000);
    assert_eq!(cost.output_micros_per_million, 6_000_000);
    assert_eq!(
        cost.cache_read_micros_per_million, 1_200_000,
        "an unstated cache rate takes the input rate, never zero"
    );
}

#[test]
fn a_price_this_build_cannot_state_exactly_refuses_the_row() {
    let mut protocol = ProviderListingProtocol::default();
    let priced = r#"{"id":"vendor/alpha","supported_parameters":["tools"],
        "pricing":{"prompt":"1e-6","completion":"0.000006"}}"#;
    let verdict = round_trip(&mut protocol, &success(&[priced.to_owned()]));
    assert_eq!(
        verdict,
        ProviderListingVerdict::RejectedEmpty,
        "a model whose price is not known is a model no spend cap can hold"
    );
}

#[test]
fn a_row_that_names_an_endpoint_is_refused_rather_than_trimmed() {
    let mut protocol = ProviderListingProtocol::default();
    let repointing = r#"{"id":"vendor/alpha","endpoint":"https://elsewhere.invalid/v1",
        "supported_parameters":["tools"],
        "pricing":{"prompt":"0.000001","completion":"0.000001"}}"#;
    let verdict = round_trip(
        &mut protocol,
        &success(&[repointing.to_owned(), row("vendor/beta")]),
    );
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: LISTED.to_owned(),
            offered: 2,
            kept: 1,
        }
    );
    assert_eq!(
        protocol
            .models()
            .next()
            .expect("one model")
            .model_id
            .as_str(),
        "vendor/beta",
        "the row that answered with an address is gone, not sanitized"
    );
}

#[test]
fn a_row_that_states_no_capabilities_is_dropped_and_the_rest_are_kept() {
    let mut protocol = ProviderListingProtocol::default();
    let silent = r#"{"id":"vendor/silent","pricing":{"prompt":"0.000001",
        "completion":"0.000001"}}"#;
    let verdict = round_trip(
        &mut protocol,
        &success(&[silent.to_owned(), row("vendor/beta")]),
    );
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: LISTED.to_owned(),
            offered: 2,
            kept: 1,
        }
    );
}

#[test]
fn a_successful_answer_with_no_readable_row_replaces_nothing() {
    let mut protocol = ProviderListingProtocol::default();
    round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    let baseline = baseline();
    protocol
        .begin_refresh(
            &candidates(),
            GENERATION,
            NOW + super::LISTING_REFRESH_DUE_MS,
        )
        .expect("due again");
    let verdict = protocol.deliver_fetch_result(
        &baseline,
        &merged(&baseline),
        &success(&[]),
        NOW + super::LISTING_REFRESH_DUE_MS,
    );
    assert_eq!(verdict, ProviderListingVerdict::RejectedEmpty);
    assert_eq!(
        protocol.models().count(),
        1,
        "an empty answer is not an empty catalog"
    );
}

#[test]
fn an_unserved_fetch_reports_unavailable_and_keeps_what_it_had() {
    let mut protocol = ProviderListingProtocol::default();
    round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    let baseline = baseline();
    protocol
        .begin_refresh(
            &candidates(),
            GENERATION,
            NOW + super::LISTING_REFRESH_DUE_MS,
        )
        .expect("due again");
    let verdict = protocol.deliver_fetch_result(
        &baseline,
        &merged(&baseline),
        &refused(wire::CatalogFetchDisposition::Unavailable),
        NOW + super::LISTING_REFRESH_DUE_MS,
    );
    assert_eq!(
        verdict,
        ProviderListingVerdict::TransportFailed,
        "a build that cannot serve the fetch says so rather than succeeding with nothing"
    );
    assert_eq!(protocol.models().count(), 1);
}

#[test]
fn an_answer_this_build_cannot_read_leaves_the_models_it_had() {
    let mut protocol = ProviderListingProtocol::default();
    round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    let baseline = baseline();
    protocol
        .begin_refresh(
            &candidates(),
            GENERATION,
            NOW + super::LISTING_REFRESH_DUE_MS,
        )
        .expect("due again");
    let verdict = protocol.deliver_fetch_result(
        &baseline,
        &merged(&baseline),
        &wire::ProviderListingFetchResult {
            provider_id: LISTED.to_owned(),
            disposition: wire::CatalogFetchDisposition::Success,
            body: b"not a listing".to_vec(),
        },
        NOW + super::LISTING_REFRESH_DUE_MS,
    );
    assert_eq!(verdict, ProviderListingVerdict::RejectedMalformed);
    assert_eq!(protocol.models().count(), 1);
}

#[test]
fn a_result_naming_no_flight_answers_nothing_that_was_asked() {
    let mut protocol = ProviderListingProtocol::default();
    let baseline = baseline();
    let verdict = protocol.deliver_fetch_result(
        &baseline,
        &merged(&baseline),
        &success(&[row("vendor/alpha")]),
        NOW,
    );
    assert_eq!(verdict, ProviderListingVerdict::UnexpectedResult);
    assert_eq!(protocol.models().count(), 0);
}

#[test]
fn a_confirmed_listing_is_not_asked_for_again_until_it_is_due() {
    let mut protocol = ProviderListingProtocol::default();
    round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    assert!(
        protocol
            .begin_refresh(&candidates(), GENERATION, NOW)
            .is_none(),
        "a list confirmed a moment ago is not stale"
    );
    assert!(protocol
        .begin_refresh(
            &candidates(),
            GENERATION,
            NOW + super::LISTING_REFRESH_DUE_MS
        )
        .is_some());
}

#[test]
fn how_many_were_kept_is_recorded_and_reachable() {
    let mut protocol = ProviderListingProtocol::default();
    let silent = r#"{"id":"vendor/silent"}"#;
    round_trip(
        &mut protocol,
        &success(&[silent.to_owned(), row("vendor/beta")]),
    );
    assert_eq!(
        protocol.kept_for(LISTED),
        Some((2, 1)),
        "a truncated list presented as the whole list is the failure the bound would cause"
    );
}

#[test]
fn which_models_survive_the_bound_is_identity_order_and_not_arrival_order() {
    let mut protocol = ProviderListingProtocol::default();
    // Sent highest identity first, so arrival order and identity order are
    // opposites and the rule under test is the one that decides.
    let mut rows: Vec<String> = (0..=MAX_LISTED_MODELS)
        .rev()
        .map(|index| row(&format!("vendor/m{index:05}")))
        .collect();
    rows.truncate(MAX_LISTED_MODELS + 1);
    let verdict = round_trip(&mut protocol, &success(&rows));
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: LISTED.to_owned(),
            offered: MAX_LISTED_MODELS + 1,
            kept: MAX_LISTED_MODELS,
        }
    );
    let kept: Vec<&str> = protocol
        .models()
        .map(|model| model.model_id.as_str())
        .collect();
    assert_eq!(kept.first().copied(), Some("vendor/m00000"));
    assert!(
        !kept.contains(&"vendor/m04096"),
        "the identity-highest row is the one the bound drops, on every device"
    );
}

#[test]
fn a_listing_goes_when_the_credential_it_was_fetched_with_does() {
    let mut protocol = ProviderListingProtocol::default();
    round_trip(&mut protocol, &success(&[row("vendor/alpha")]));
    assert!(protocol.forget(LISTED));
    assert_eq!(protocol.models().count(), 0);
    assert!(!protocol.forget(LISTED), "there is nothing left to forget");
    assert!(
        protocol
            .begin_refresh(&candidates(), GENERATION, NOW)
            .is_some(),
        "the next connection asks again rather than waiting out a stale confirmation"
    );
}
