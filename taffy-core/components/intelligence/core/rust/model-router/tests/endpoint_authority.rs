// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which authority named the address a plan carries.
//!
//! `endpoint_rules.rs` next door is about the two constructors — what may
//! become an address. This is about the question asked afterwards, and it is a
//! different question: *who* named the one a request is about to be sent to.
//! Decision 0096 section 2 has the browser apply a different rule to each, so
//! the composer has to say which it is claiming, and the only honest source
//! for that is the layer of the merge the address came from.
//!
//! The case that decides the shape of the answer is
//! `a_model_listed_onto_a_persons_layer_is_still_reached_at_the_published_address`.
//! A model's layer is not the address's layer: an aggregator's listing lands
//! every model it names on the person's own layer while the address they are
//! reached at stays the published provider's. A composer that read the model's
//! layer would send a vendor's endpoint under the register rule and have it
//! refused, for every model a person ever connected an aggregator to.
//!
//! The same fact settles a second question, and the tests for it are here
//! rather than beside the other credential refusals because it is the same
//! question asked once: **may this candidate be sent with nothing attached?**
//! A vendor's API never may. A server somebody runs may, and usually must —
//! Ollama out of the box wants no key at all, so a rule that refused it would
//! make the commonest self-hosted server the one selection cannot reach.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use model_router::catalog::types::CatalogDocument;
use model_router::catalog::{parse_document, CatalogLayer, Endpoint, InputModality, ModelRole};
use model_router::credential::{AuthType, CredentialState};
use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
use model_router::route::{
    DisclosureClass, EndpointClassifier, ModelPolicy, Recipient, Route, RoutePreference,
    RouteRequest, RouteSelector,
};
use model_router::{MergedCatalog, ModelKey, RoutePlan, RouteRefusal, TaskId, ThinkingLevel};

/// The address decision 0096 was written for, with the port and base path a
/// server on somebody's desk actually has.
const OWN_SERVER: &str = "http://localhost:11434/v1";

/// The address a published catalog names for the same identity.
const PUBLISHED: &str = "https://api.vendor.example/v1";

/// A classifier that recognizes exactly the addresses it was given.
///
/// The production one answers the same way — by membership in the list of
/// endpoints the person registered — because whether an address is a server
/// somebody runs is not a fact about how it is spelled.
struct Registered(Vec<Endpoint>);

impl EndpointClassifier for Registered {
    fn is_local_endpoint(&self, endpoint: &Endpoint) -> bool {
        self.0.contains(endpoint)
    }
}

fn document(providers: &str, models: &str) -> CatalogDocument {
    let text = format!(
        r#"{{"schema_version": 1, "catalog_version": "endpoint-authority-1",
            "generated_at": "2027-01-01T00:00:00Z",
            "providers": [{providers}], "models": [{models}]}}"#
    );
    let parsed = parse_document(&text).expect("well-formed JSON");
    assert!(parsed.defects.is_empty(), "{:?}", parsed.defects);
    parsed.document
}

fn provider_row() -> String {
    format!(
        r#"{{"schema_version": 1, "provider_id": "own-server",
            "display_name": "My server", "wire_api": "OPEN_AI_COMPLETIONS",
            "default_endpoint": "{PUBLISHED}",
            "auth_methods": ["API_KEY"], "subscription": false,
            "model_source": "STATIC_CATALOG", "enabled": true}}"#
    )
}

/// A model row that states no address of its own, which is what both halves of
/// the person's layer produce: a saved endpoint files its models under the
/// provider, and a fetched listing has no address to state.
fn model_row() -> String {
    r#"{"schema_version": 1, "model_id": "local-large",
        "provider_id": "own-server", "display_name": "Local Large",
        "roles": ["PRIMARY_REASONING"], "input_modalities": ["TEXT"],
        "reasoning": false, "tool_calling": true,
        "context_window": 8192, "max_output_tokens": 2048,
        "cost": {"snapshot_version": "2027-01-01", "currency": "USD",
          "basis": "METERED", "input_micros_per_million": 0,
          "output_micros_per_million": 0,
          "cache_read_micros_per_million": 0,
          "cache_write_micros_per_million": 0},
        "enabled": true}"#
        .to_owned()
}

/// The person's own layer: the provider row with the address they typed.
///
/// Built by swapping the endpoint after the document is decoded, because the
/// decoder applies the catalog rule and would refuse `http://` — which is
/// exactly right, and exactly why the person's layer is projected from the
/// typed contract rather than parsed (`user_catalog.rs`).
fn own_layer(models: &str) -> CatalogDocument {
    let mut layer = document(&provider_row(), models);
    let provider = layer.providers.first_mut().expect("one provider");
    provider.default_endpoint = Endpoint::user_base_url(OWN_SERVER).expect("a server they run");
    layer
}

fn key() -> ModelKey {
    common::key("own-server", "local-large")
}

fn request() -> RouteRequest {
    RouteRequest {
        task_id: TaskId::from_bytes([3; 16]),
        role: ModelRole::PrimaryReasoning,
        preference: RoutePreference::ByoDirect,
        purpose: RequestPurpose::Planning,
        context: ContextManifest {
            source_ids: Vec::new(),
            classes: vec![DataSensitivity::Personal],
            item_count: 1,
            estimated_input_tokens: 100,
        },
        required_modalities: vec![InputModality::Text],
        requires_tool_calling: true,
        thinking: ThinkingLevel::Off,
        answer_tokens: 512,
        estimated_output_tokens: 512,
        pinned_model: None,
    }
}

#[test]
fn a_persons_own_endpoint_is_reported_as_named_by_their_own_layer() {
    let baseline = document(&provider_row(), &model_row());
    let merged = MergedCatalog::build(&baseline, None, Some(&own_layer(&model_row())));
    let resolved = merged.resolve(&key()).expect("their own model resolves");

    assert_eq!(resolved.endpoint.as_str(), OWN_SERVER, "whole, not trimmed");
    assert_eq!(resolved.endpoint_layer, CatalogLayer::UserOverride);
}

#[test]
fn a_model_listed_onto_a_persons_layer_is_still_reached_at_the_published_address() {
    // The aggregator shape: a published provider, and the models it listed for
    // this person's account arriving on the layer that is theirs. The address
    // is the vendor's and no part of it was ever typed, so nothing about it is
    // in the browser's register — and the composer has to ask for the catalog
    // rule or the request is refused.
    let baseline = document(&provider_row(), "");
    let listed = document("", &model_row());
    let merged = MergedCatalog::build(&baseline, None, Some(&listed));
    let resolved = merged.resolve(&key()).expect("a listed model resolves");

    assert_eq!(
        merged.model_layer(&key()),
        Some(CatalogLayer::UserOverride),
        "the model is on the person's layer"
    );
    assert_eq!(
        resolved.endpoint_layer,
        CatalogLayer::EmbeddedBaseline,
        "and its address is not"
    );
    assert_eq!(resolved.endpoint.as_str(), PUBLISHED);
}

/// Selection over a merge whose address came from the person's own layer.
fn select_their_own_server(
    credentials: &common::FixedCredentials,
) -> Result<RoutePlan, RouteRefusal> {
    let baseline = document(&provider_row(), &model_row());
    let merged = MergedCatalog::build(&baseline, None, Some(&own_layer(&model_row())));
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let registered = Registered(vec![
        Endpoint::user_base_url(OWN_SERVER).expect("registrable")
    ]);
    let selector = RouteSelector::new(&merged, credentials, &entitlement, &policy, &registered);
    selector.select(&request(), &common::open_ledger())
}

/// Selection over the same model at the address the published document names.
///
/// The one difference from the function above is which layer supplied the
/// address, which is the whole of what these two rules are chosen between by.
fn select_the_published_address(
    credentials: &common::FixedCredentials,
) -> Result<RoutePlan, RouteRefusal> {
    let merged = MergedCatalog::from_baseline(&document(&provider_row(), &model_row()));
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let registered = Registered(Vec::new());
    let selector = RouteSelector::new(&merged, credentials, &entitlement, &policy, &registered);
    selector.select(&request(), &common::open_ledger())
}

/// One plan for a model behind the address the person typed, on their key.
fn plan_for_their_own_server() -> RoutePlan {
    select_their_own_server(&common::FixedCredentials::empty().usable(
        "own-server",
        AuthType::ApiKey,
        false,
    ))
    .expect("a person's own server routes like any other")
}

#[test]
fn a_plan_carries_the_layer_that_named_the_address_it_chose() {
    let plan = plan_for_their_own_server();

    assert_eq!(plan.route, Route::ByoDirect);
    assert_eq!(plan.primary.endpoint.as_str(), OWN_SERVER);
    assert_eq!(
        plan.primary.endpoint_layer,
        CatalogLayer::UserOverride,
        "the fact survives selection, so the composer need not ask again"
    );
}

#[test]
fn a_turn_to_a_persons_own_server_is_disclosed_as_theirs_and_names_their_host() {
    let plan = plan_for_their_own_server();

    // An address somebody runs themselves is not a vendor's API, and the class
    // says so rather than filing it under a general third-party one
    // (decision 0096 section 6).
    assert_eq!(
        plan.disclosure_class(),
        DisclosureClass::LocalEndpointDirect
    );
    // The host, with its port and without its path: the sentence a person is
    // shown about where their page went. It read `http:` for every cleartext
    // address until `Endpoint::host` was taught the second scheme, which is
    // the whole of what this asserts.
    assert_eq!(
        plan.primary.disclosure.egress.recipients,
        vec![Recipient::Provider {
            display_name: "My server".to_owned(),
            host: "localhost:11434".to_owned(),
        }]
    );
}

#[test]
fn a_server_that_needs_no_key_is_reached_with_nothing_attached() {
    // The commonest self-hosted server there is: Ollama, freshly installed,
    // asking for nothing. The person saved it with no credential handle, which
    // the contract admits on purpose, and until this rule existed selection
    // refused the one address the rest of decision 0096 was built to reach.
    let plan = select_their_own_server(&common::FixedCredentials::empty())
        .expect("a server that wants no key is still a server");

    assert_eq!(plan.primary.endpoint_layer, CatalogLayer::UserOverride);
    assert!(
        plan.primary.auth.is_none(),
        "no attachment, so the composed request carries no credential handle"
    );
}

#[test]
fn a_published_address_with_nothing_stored_is_refused_exactly_as_before() {
    // The other half of the pair, and the reason it is a pair: the same model
    // and the same empty directory, differing only in which layer named the
    // address. Admitting a server somebody runs must not admit a vendor's API,
    // where nothing stored means nothing to send and a round trip would be
    // spent learning what the device already knew.
    assert!(matches!(
        select_the_published_address(&common::FixedCredentials::empty()),
        Err(RouteRefusal::CredentialMissing { .. })
    ));
}

#[test]
fn a_key_that_stopped_working_refuses_their_own_server_rather_than_dropping_it() {
    // The failure this rule is most dangerous without. A credential that
    // exists and does not work is not "this endpoint needs none": reading it
    // that way would send the person's pages to their own server
    // unauthenticated, with no refusal to see and nothing said. Both states a
    // stored credential can go wrong in are walked, so a third one added later
    // has to be given an answer here rather than falling into the admitting
    // arm.
    for state in [CredentialState::NeedsSignIn, CredentialState::RefreshFailed] {
        let credentials =
            common::FixedCredentials::empty().broken("own-server", AuthType::ApiKey, state);
        assert_eq!(
            select_their_own_server(&credentials).err(),
            Some(RouteRefusal::CredentialNeedsAttention {
                provider_id: model_router::ProviderId::new("own-server").expect("valid"),
                state,
            }),
            "a stored credential that is not usable is refused, never dropped"
        );
    }
}

#[test]
fn a_key_they_did_supply_for_their_own_server_is_still_attached() {
    // The other direction of the same rule: admitting an absent credential
    // must not stop attaching a present one. A person running vLLM behind a
    // key saved that key, and a request that left it off would be refused by
    // their own server.
    let plan = plan_for_their_own_server();
    let attachment = plan
        .primary
        .auth
        .as_ref()
        .expect("a key they supplied is spent");

    assert_eq!(attachment.credential.state, CredentialState::Usable);
    assert!(
        !attachment.secret_header_names.is_empty(),
        "the adapter is told which header to fill from the store"
    );
}

#[test]
fn an_unauthenticated_turn_to_their_own_server_names_no_credential_at_all() {
    // What the person is shown about a request that goes out with nothing.
    // The class stays the one decision 0096 section 6 gives their own machine,
    // and the disclosure names no credential — the two classes beside it each
    // say a plan or a key stands behind the access, and neither is true here.
    let plan = select_their_own_server(&common::FixedCredentials::empty())
        .expect("a server that wants no key is still a server");

    assert_eq!(
        plan.disclosure_class(),
        DisclosureClass::LocalEndpointDirect
    );
    assert_eq!(plan.primary.disclosure.credential, None);
    assert_eq!(
        plan.primary.disclosure.egress.recipients,
        vec![Recipient::Provider {
            display_name: "My server".to_owned(),
            host: "localhost:11434".to_owned(),
        }],
        "still their host, said the same way as when a key is in play"
    );
}
