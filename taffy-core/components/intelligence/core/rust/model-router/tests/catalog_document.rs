// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Catalog parsing, validation, and merging, for any catalog document.
//!
//! The rules here hold whatever layer a document arrived on, which is why
//! they are driven by a synthetic fixture that names no real vendor. What the
//! *committed* baseline says — which providers ship, on what terms, and what
//! each role resolves to — is a different question with different authorities,
//! and it lives in `embedded_baseline.rs`.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::{
    parse_document, validate, CatalogLayer, DefectReason, LookupError, MergedCatalog, ModelRole,
    ViolationRule,
};
use model_router::EMBEDDED_BASELINE_CATALOG;

#[test]
fn the_reader_and_serde_agree_on_every_committed_document() {
    // The crate ships its own reader because the dependency tree stays near
    // zero. That reader could drift away from the serde shape the rest of the
    // system binds to, so both decode the same bytes and the results have to
    // match.
    for text in [EMBEDDED_BASELINE_CATALOG, common::FIXTURE_CATALOG] {
        let ours = parse_document(text).expect("reader accepts the document");
        assert!(ours.is_clean(), "defects: {:?}", ours.defects);
        let theirs: model_router::CatalogDocument =
            serde_json::from_str(text).expect("serde accepts the document");
        assert_eq!(ours.document, theirs);
    }
}

#[test]
fn a_document_round_trips_through_serde() {
    let document = common::fixture_document();
    let text = serde_json::to_string(&document).expect("serializes");
    let back: model_router::CatalogDocument = serde_json::from_str(&text).expect("deserializes");
    assert_eq!(document, back);
}

fn one_provider(body: &str) -> String {
    format!(
        r#"{{"schema_version":1,"catalog_version":"t","generated_at":"2026-01-01T00:00:00Z",
            "providers":[{body}],"models":[]}}"#
    )
}

#[test]
fn an_unknown_protocol_family_drops_the_entry_and_keeps_the_document() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"TELEPATHY",
            "default_endpoint":"https://a.example","auth_methods":["API_KEY"],
            "subscription":false,"model_source":"STATIC_CATALOG","enabled":true,
            "schema_version":1}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
    assert_eq!(parsed.defects.len(), 1);
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::UnknownEnumValue {
            field: "wire_api",
            ..
        })
    ));
}

#[test]
fn an_unknown_schema_version_drops_the_entry() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"https://a.example","auth_methods":["API_KEY"],
            "subscription":false,"model_source":"STATIC_CATALOG","enabled":true,
            "schema_version":99}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::UnsupportedSchemaVersion { declared: 99 })
    ));
}

#[test]
fn a_missing_kill_switch_drops_the_entry() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"https://a.example","auth_methods":["API_KEY"],
            "subscription":false,"model_source":"STATIC_CATALOG","schema_version":1}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::MissingField { field: "enabled" })
    ));
}

#[test]
fn a_provider_with_no_auth_method_is_refused() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"https://a.example","auth_methods":[],
            "subscription":false,"model_source":"STATIC_CATALOG","enabled":true,
            "schema_version":1}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::Invalid {
            field: "auth_methods",
            ..
        })
    ));
}

#[test]
fn a_static_header_with_a_credential_name_is_refused() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"https://a.example","auth_methods":["API_KEY"],
            "subscription":false,"static_headers":{"Authorization":"public-value"},
            "model_source":"STATIC_CATALOG","enabled":true,"schema_version":1}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::Invalid {
            field: "static_headers",
            ..
        })
    ));
}

#[test]
fn a_static_header_with_a_secret_shaped_value_is_refused() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"https://a.example","auth_methods":["API_KEY"],
            "subscription":false,"static_headers":{"x-client-flavor":"Bearer not-a-secret"},
            "model_source":"STATIC_CATALOG","enabled":true,"schema_version":1}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::Invalid {
            field: "static_headers",
            ..
        })
    ));
}

#[test]
fn an_endpoint_that_is_not_https_is_refused() {
    let text = one_provider(
        r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"http://a.example","auth_methods":["API_KEY"],
            "subscription":false,"model_source":"STATIC_CATALOG","enabled":true,
            "schema_version":1}"#,
    );
    let parsed = parse_document(&text).expect("still valid JSON");
    assert!(parsed.document.providers.is_empty());
}

#[test]
fn a_repeated_provider_id_keeps_the_first_and_reports_the_second() {
    let entry = r#"{"provider_id":"p","display_name":"P","wire_api":"OPEN_AI_RESPONSES",
            "default_endpoint":"https://a.example","auth_methods":["API_KEY"],
            "subscription":false,"model_source":"STATIC_CATALOG","enabled":true,
            "schema_version":1}"#;
    let text = one_provider(&format!("{entry},{entry}"));
    let parsed = parse_document(&text).expect("still valid JSON");
    assert_eq!(parsed.document.providers.len(), 1);
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::DuplicateEntry)
    ));
}

#[test]
fn a_duplicate_json_key_refuses_the_whole_document() {
    let text = r#"{"schema_version":1,"schema_version":2,"catalog_version":"t",
        "generated_at":"2026-01-01T00:00:00Z","providers":[],"models":[]}"#;
    assert!(parse_document(text).is_err());
}

#[test]
fn a_model_without_its_provider_is_a_violation() {
    let text = r#"{"schema_version":1,"catalog_version":"t","generated_at":"2026-01-01T00:00:00Z",
        "providers":[],"models":[{"model_id":"m","provider_id":"ghost","display_name":"M",
        "roles":["PRIMARY_REASONING"],"input_modalities":["TEXT"],"reasoning":true,
        "tool_calling":true,"context_window":1000,"max_output_tokens":100,
        "cost":{"snapshot_version":"s","currency":"USD","basis":"METERED",
        "input_micros_per_million":1,"output_micros_per_million":1,
        "cache_read_micros_per_million":1,"cache_write_micros_per_million":1},
        "enabled":true,"schema_version":1}]}"#;
    let parsed = parse_document(text).expect("valid JSON");
    let violations = validate(&parsed.document);
    assert!(matches!(
        violations.first().map(|violation| &violation.rule),
        Some(ViolationRule::UnknownProvider { .. })
    ));
}

#[test]
fn an_imputed_price_without_a_plan_behind_it_is_a_violation() {
    let mut document = common::fixture_document();
    for provider in &mut document.providers {
        provider.subscription = false;
    }
    let violations = validate(&document);
    assert!(violations
        .iter()
        .any(|violation| violation.rule == ViolationRule::ImpliedPriceWithoutSubscription));
}

#[test]
fn unordered_price_tiers_are_a_violation() {
    let mut document = common::fixture_document();
    for model in &mut document.models {
        for tier in &mut model.cost.long_context_tiers {
            tier.min_input_tokens = 0;
        }
    }
    let violations = validate(&document);
    assert!(violations
        .iter()
        .any(|violation| violation.rule == ViolationRule::UnorderedPriceTiers));
}

#[test]
fn an_unknown_provider_or_model_fails_closed() {
    let catalog = common::fixture_catalog();
    assert_eq!(
        catalog.resolve(&common::key("ghost", "reasoner-one")),
        Err(LookupError::UnknownProvider {
            provider_id: model_router::ProviderId::new("ghost").expect("key"),
        })
    );
    assert_eq!(
        catalog.resolve(&common::key("effort-vendor", "ghost")),
        Err(LookupError::UnknownModel {
            model_id: model_router::ModelId::new("ghost").expect("key"),
        })
    );
}

#[test]
fn both_kill_switches_close_the_door() {
    let catalog = common::fixture_catalog();
    assert!(matches!(
        catalog.resolve(&common::key("retired-vendor", "orphan-one")),
        Err(LookupError::ProviderDisabled { .. })
    ));
    assert!(matches!(
        catalog.resolve(&common::key("budget-vendor", "withdrawn-one")),
        Err(LookupError::ModelDisabled { .. })
    ));
    let names: Vec<String> = catalog
        .candidates_for_role(ModelRole::PrimaryReasoning)
        .iter()
        .map(|resolved| resolved.model.key().to_string())
        .collect();
    assert_eq!(
        names,
        vec![
            "budget-vendor/reasoner-two".to_owned(),
            "effort-vendor/reasoner-one".to_owned()
        ]
    );
}

#[test]
fn a_model_inherits_its_providers_protocol_and_endpoint() {
    let catalog = common::fixture_catalog();
    let resolved = catalog
        .resolve(&common::key("effort-vendor", "reasoner-one"))
        .expect("resolves");
    assert_eq!(
        resolved.wire_api,
        model_router::catalog::WireApi::OpenAiResponses
    );
    assert_eq!(
        resolved.endpoint.as_str(),
        "https://api.effort-vendor.example/v1"
    );
    assert_eq!(resolved.endpoint.host(), "api.effort-vendor.example");
}

fn overlay(generated_at: &str, display_name: &str) -> model_router::CatalogDocument {
    let text = format!(
        r#"{{"schema_version":1,"catalog_version":"overlay","generated_at":"{generated_at}",
            "providers":[{{"provider_id":"effort-vendor","display_name":"{display_name}",
            "wire_api":"OPEN_AI_RESPONSES","default_endpoint":"https://api.effort-vendor.example/v1",
            "auth_methods":["API_KEY"],"subscription":false,"model_source":"STATIC_CATALOG",
            "enabled":true,"schema_version":1}}],"models":[]}}"#
    );
    parse_document(&text).expect("valid JSON").document
}

#[test]
fn a_newer_overlay_wins_and_a_staler_one_is_ignored() {
    let baseline = common::fixture_document();
    let newer = overlay("2026-09-01T00:00:00Z", "Renamed Vendor");
    let older = overlay("2026-01-01T00:00:00Z", "Stale Vendor");

    let merged = MergedCatalog::build(&baseline, Some(&newer), None);
    assert!(merged.overlay_applied());
    assert_eq!(
        merged
            .provider(&model_router::ProviderId::new("effort-vendor").expect("key"))
            .map(|provider| provider.display_name.as_str()),
        Some("Renamed Vendor")
    );
    assert_eq!(
        merged.model_layer(&common::key("effort-vendor", "reasoner-one")),
        Some(CatalogLayer::EmbeddedBaseline)
    );

    let merged = MergedCatalog::build(&baseline, Some(&older), None);
    assert!(!merged.overlay_applied());
    assert_eq!(
        merged
            .provider(&model_router::ProviderId::new("effort-vendor").expect("key"))
            .map(|provider| provider.display_name.as_str()),
        Some("Effort Vendor")
    );
}

#[test]
fn user_overrides_are_merged_last() {
    let baseline = common::fixture_document();
    let newer = overlay("2026-09-01T00:00:00Z", "Renamed Vendor");
    let user = overlay("2020-01-01T00:00:00Z", "My Own Endpoint");
    let merged = MergedCatalog::build(&baseline, Some(&newer), Some(&user));
    assert_eq!(
        merged
            .provider(&model_router::ProviderId::new("effort-vendor").expect("key"))
            .map(|provider| provider.display_name.as_str()),
        Some("My Own Endpoint")
    );
    assert_eq!(
        merged
            .providers()
            .find(|entry| entry.provider.display_name == "My Own Endpoint")
            .map(|entry| entry.layer),
        Some(CatalogLayer::UserOverride)
    );
}
