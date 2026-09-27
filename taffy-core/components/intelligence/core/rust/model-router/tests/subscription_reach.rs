// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One vendor, two ways in, and what each of them changes.
//!
//! Decision 0029 section 2 keeps a vendor reachable by both a key and a
//! subscription to a single catalog row and derives the class from the
//! credential. Everything here is the consequence of that sentence: which
//! family is spoken, which address it is spoken to, what the request carries
//! that the other way in refuses, and what comes back that must never be read.
//!
//! The fixture is local rather than shared. The vendors here exist to have two
//! ways in, and adding one to the shared catalog would change candidate order
//! for every test that walks it.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::{
    AuthMethod, CatalogDocument, InputModality, MergedCatalog, ModelRole, WireApi,
};
use model_router::credential::AuthType;
use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
use model_router::route::{
    ModelPolicy, NoLocalEndpoints, RoutePreference, RouteRequest, RouteSelector,
};
use model_router::{TaskId, ThinkingLevel};

/// A vendor with two ways in, and one with only the ordinary way.
const TWO_WAYS_IN: &str = r#"
{
  "schema_version": 1,
  "catalog_version": "reach-0001",
  "generated_at": "2026-08-10T12:00:00Z",
  "providers": [
    {
      "provider_id": "dual-vendor",
      "display_name": "Dual Vendor",
      "wire_api": "OPEN_AI_RESPONSES",
      "default_endpoint": "https://api.dual-vendor.example/v1",
      "oauth_wire_api": "OPEN_AI_CODEX_RESPONSES",
      "oauth_endpoint": "https://chat.dual-vendor.example/backend-api/codex",
      "auth_methods": ["API_KEY", "OAUTH"],
      "subscription": true,
      "model_source": "STATIC_CATALOG",
      "enabled": true,
      "schema_version": 1
    },
    {
      "provider_id": "one-way-vendor",
      "display_name": "One Way Vendor",
      "wire_api": "ANTHROPIC_MESSAGES",
      "default_endpoint": "https://api.one-way-vendor.example",
      "auth_methods": ["API_KEY", "OAUTH"],
      "subscription": true,
      "model_source": "STATIC_CATALOG",
      "enabled": true,
      "schema_version": 1
    }
  ],
  "models": [
    {
      "model_id": "dual-one",
      "provider_id": "dual-vendor",
      "display_name": "Dual One",
      "roles": ["PRIMARY_REASONING"],
      "input_modalities": ["TEXT"],
      "reasoning": true,
      "tool_calling": true,
      "context_window": 200000,
      "max_output_tokens": 32000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "METERED",
        "input_micros_per_million": 3000000,
        "output_micros_per_million": 15000000,
        "cache_read_micros_per_million": 300000,
        "cache_write_micros_per_million": 3750000
      },
      "enabled": true,
      "schema_version": 1
    },
    {
      "model_id": "one-way-one",
      "provider_id": "one-way-vendor",
      "display_name": "One Way One",
      "roles": ["FAST_BROWSING"],
      "input_modalities": ["TEXT"],
      "reasoning": true,
      "tool_calling": true,
      "context_window": 200000,
      "max_output_tokens": 32000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "METERED",
        "input_micros_per_million": 3000000,
        "output_micros_per_million": 15000000,
        "cache_read_micros_per_million": 300000,
        "cache_write_micros_per_million": 3750000
      },
      "enabled": true,
      "schema_version": 1
    }
  ]
}
"#;

fn document() -> CatalogDocument {
    let parsed = model_router::parse_document(TWO_WAYS_IN).expect("valid JSON");
    assert!(parsed.is_clean(), "fixture defects: {:?}", parsed.defects);
    let violations = model_router::validate(&parsed.document);
    assert!(violations.is_empty(), "fixture violations: {violations:?}");
    parsed.document
}

fn catalog() -> MergedCatalog {
    MergedCatalog::from_baseline(&document())
}

fn request(role: ModelRole) -> RouteRequest {
    RouteRequest {
        task_id: TaskId::from_bytes([3; 16]),
        role,
        preference: RoutePreference::ByoDirect,
        purpose: RequestPurpose::Planning,
        context: ContextManifest {
            source_ids: Vec::new(),
            classes: vec![DataSensitivity::Public],
            item_count: 1,
            estimated_input_tokens: 1_000,
        },
        required_modalities: vec![InputModality::Text],
        requires_tool_calling: false,
        thinking: ThinkingLevel::Medium,
        answer_tokens: 2_000,
        estimated_output_tokens: 500,
        pinned_model: None,
    }
}

/// The candidate one credential resolves to, as family and host.
fn reached(provider: &str, model: &str, role: ModelRole, method: AuthType) -> (WireApi, String) {
    let catalog = catalog();
    let credentials = common::FixedCredentials::empty().usable(
        provider,
        method,
        matches!(method, AuthType::Oauth),
    );
    let entitlement = common::no_entitlement();
    let mut policy = ModelPolicy::new();
    policy.set(role, vec![common::key(provider, model)]);
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(&request(role), &common::open_ledger())
        .expect("a configured provider routes");
    (
        plan.primary.wire_api,
        plan.primary.endpoint.host().to_owned(),
    )
}

// --- credential-aware selection ----------------------------------------------

#[test]
fn a_key_reaches_the_key_family_at_the_key_address() {
    let (family, host) = reached(
        "dual-vendor",
        "dual-one",
        ModelRole::PrimaryReasoning,
        AuthType::ApiKey,
    );
    assert_eq!(family, WireApi::OpenAiResponses);
    assert_eq!(host, "api.dual-vendor.example");
}

#[test]
fn a_subscription_reaches_the_other_family_at_the_other_address() {
    let (family, host) = reached(
        "dual-vendor",
        "dual-one",
        ModelRole::PrimaryReasoning,
        AuthType::Oauth,
    );
    assert_eq!(family, WireApi::OpenAiCodexResponses);
    assert_eq!(
        host, "chat.dual-vendor.example",
        "a subscription spent at the key endpoint is a request the vendor refuses"
    );
}

#[test]
fn a_row_with_one_way_in_answers_the_same_however_it_is_authenticated() {
    for method in [AuthType::ApiKey, AuthType::Oauth] {
        let (family, host) = reached(
            "one-way-vendor",
            "one-way-one",
            ModelRole::FastBrowsing,
            method,
        );
        assert_eq!(family, WireApi::AnthropicMessages, "{method:?}");
        assert_eq!(host, "api.one-way-vendor.example", "{method:?}");
    }
}

#[test]
fn the_provider_row_answers_the_same_question_without_a_router() {
    // The predicate the selector reads, asserted where it lives: a row is
    // still one descriptor, and the second answer is reached by asking with a
    // method rather than by looking up a second row.
    let document = document();
    let dual = &document.providers[0];
    assert_eq!(
        dual.wire_api_for(AuthMethod::ApiKey),
        WireApi::OpenAiResponses
    );
    assert_eq!(
        dual.wire_api_for(AuthMethod::Oauth),
        WireApi::OpenAiCodexResponses
    );
    let one_way = &document.providers[1];
    assert_eq!(
        one_way.wire_api_for(AuthMethod::Oauth),
        one_way.wire_api_for(AuthMethod::ApiKey)
    );
    assert_eq!(
        one_way.endpoint_for(AuthMethod::Oauth),
        &one_way.default_endpoint
    );
}

#[test]
fn a_second_address_that_is_not_https_drops_the_row_rather_than_falling_back() {
    let broken = TWO_WAYS_IN.replace(
        "https://chat.dual-vendor.example/backend-api/codex",
        "http://chat.dual-vendor.example/backend-api/codex",
    );
    let parsed = model_router::parse_document(&broken).expect("valid JSON");
    assert!(
        !parsed.is_clean(),
        "an address a subscription would be spent at is not a field to sanitize"
    );
    assert!(
        parsed
            .document
            .providers
            .iter()
            .all(|provider| provider.provider_id.as_str() != "dual-vendor"),
        "the row is dropped, so nothing resolves to the key endpoint by accident"
    );
}
