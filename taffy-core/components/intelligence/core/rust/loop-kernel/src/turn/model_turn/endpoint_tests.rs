// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The address one composed turn names, and the rule it asks the browser for.
//!
//! Two candidates that differ in one field — which layer of the merge named
//! their endpoint — must leave this composer as two different requests: one
//! trimmed to an origin and announced as a catalog address, one carried whole
//! and announced as the person's own (decision 0096 section 2). A composer
//! that got the pair the wrong way round would be refused on the other side,
//! and refused by a rule nobody meant to claim, so both halves are asserted
//! here rather than one.
//!
//! The suite next door owns the fixtures both use, because a route candidate
//! is the same thing to both of them.

use model_router::catalog::{CatalogLayer, Endpoint};
use model_router::route::{Recipient, RouteCandidate};

use crate::context::LivePage;

use super::tests::{candidate, facts, FoldDigest, NoCredentials, RecordingRouter};
use super::{compose_model_turn, shape, ComposedModelTurn, ModelTurnError};

/// The address decision 0096 was written for: a server on the machine in front
/// of the person, with a scheme, a port and a base path the catalog rule
/// refuses every one of.
const OWN_SERVER: &str = "http://localhost:11434/v1";

/// The same candidate, reached at an address the person typed themselves.
fn own_server_candidate() -> RouteCandidate {
    let mut theirs = candidate(true);
    theirs.endpoint = Endpoint::user_base_url(OWN_SERVER).expect("a server somebody runs");
    theirs.endpoint_layer = CatalogLayer::UserOverride;
    theirs
}

/// The turn one compose of `primary` produces.
fn composed(primary: RouteCandidate) -> ComposedModelTurn {
    let mut router = RecordingRouter::default();
    router.plans = Some(primary);
    let mut page = LivePage::default();
    compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &facts(),
        "model-task_1-1",
        &mut page,
        None,
    )
    .expect("a direct-route turn composes")
}

#[test]
fn an_address_a_person_typed_reaches_the_browser_whole_and_under_their_own_rule() {
    let request = composed(own_server_candidate()).request;

    // The port and the base path are the address. Trimming either one sends
    // the turn to a different server, or to the root of the right one.
    assert_eq!(request.endpoint, OWN_SERVER);
    assert_eq!(
        request.endpoint_kind,
        core_service_types::ModelEndpointKind::UserBaseUrl,
        "the browser is asked for the register rule, which is the only rule \
         this address can pass"
    );
}

#[test]
fn a_turn_to_a_server_that_needs_no_key_carries_no_credential_handle() {
    // The shape route selection now yields for the commonest self-hosted
    // server there is: a candidate on the person's own layer with no
    // attachment, because Ollama out of the box asks for nothing. The contract
    // calls an absent handle "an endpoint that needs no credential", and this
    // is the composer producing exactly that — rather than refusing the turn
    // for want of something nobody ever stored.
    let theirs = own_server_candidate();
    assert!(
        theirs.auth.is_none(),
        "a server that needs no key reaches composition with nothing attached"
    );
    let request = composed(theirs).request;

    assert_eq!(request.credential_handle, None);
    assert_eq!(request.endpoint, OWN_SERVER);
}

#[test]
fn a_catalog_address_is_still_trimmed_to_its_origin_and_still_says_so() {
    let mut vendor = candidate(true);
    vendor.endpoint = Endpoint::new("https://api.vendor.example/v1").expect("a catalog row");
    let request = composed(vendor).request;

    assert_eq!(
        request.endpoint, "https://api.vendor.example",
        "the path belongs to the wire table on the browser's side"
    );
    assert_eq!(
        request.endpoint_kind,
        core_service_types::ModelEndpointKind::CatalogOrigin
    );
}

#[test]
fn a_served_overlay_is_held_to_the_catalog_rule_exactly_as_the_baseline_is() {
    // The two catalog layers are one rule. An overlay that could name a port
    // would be a served document repointing a shipped provider, which is what
    // decision 0080's endpoint guard exists to prevent.
    let mut overlay = candidate(true);
    overlay.endpoint_layer = CatalogLayer::RemoteOverlay;
    overlay.endpoint = Endpoint::new("https://api.vendor.example:8443/v1").expect("a catalog row");

    assert_eq!(
        shape::direct_endpoint(&overlay),
        Err(ModelTurnError::EndpointUnusable)
    );
}

#[test]
fn a_port_the_catalog_named_is_refused_and_a_port_the_person_typed_is_not() {
    // The same bytes, judged by two rules, reaching two answers. If this ever
    // reads the same on both sides, one rule has been written in terms of the
    // other.
    let mut published = candidate(true);
    published.endpoint =
        Endpoint::new("https://api.vendor.example:8443/v1").expect("a catalog row");
    assert_eq!(
        shape::direct_endpoint(&published),
        Err(ModelTurnError::EndpointUnusable)
    );

    let mut theirs = published.clone();
    theirs.endpoint_layer = CatalogLayer::UserOverride;
    assert_eq!(
        shape::direct_endpoint(&theirs),
        Ok((
            "https://api.vendor.example:8443/v1".to_owned(),
            core_service_types::ModelEndpointKind::UserBaseUrl
        ))
    );
}

#[test]
fn a_person_s_address_is_refused_in_its_own_words_when_there_is_none() {
    // The floor beneath the register rule, and the twin of `IsBoundedAddress`
    // in the browser: an address-sized string is the one thing a party with no
    // register in reach can say. `Endpoint` already stands above it — neither
    // constructor makes an empty one — so this asserts the vocabulary rather
    // than a reachable path, and the vocabulary is the point. A refusal that
    // named the catalog rule would send whoever reads it to the wrong half of
    // the product.
    assert_eq!(shape::user_base_url_of(""), None);
    assert_ne!(
        ModelTurnError::OwnEndpointUnusable.label(),
        ModelTurnError::EndpointUnusable.label()
    );
}

#[test]
fn the_managed_endpoint_is_the_disclosed_worker_origin_or_nothing() {
    let mut with_worker = candidate(true);
    with_worker.disclosure.egress.recipients = vec![
        Recipient::EdgeWorker {
            host: "edge.taffygo.example".to_owned(),
        },
        Recipient::Gateway {
            host: "gateway.taffygo.example".to_owned(),
        },
        Recipient::Provider {
            display_name: "Anthropic".to_owned(),
            host: "api.anthropic.com".to_owned(),
        },
    ];
    assert_eq!(
        shape::managed_endpoint(&with_worker),
        Ok("https://edge.taffygo.example".to_owned())
    );

    // No disclosed worker — a default entitlement's empty hosts included —
    // composes nothing rather than naming a host nobody stated.
    let without = candidate(true);
    assert_eq!(
        shape::managed_endpoint(&without),
        Err(ModelTurnError::EndpointUnusable)
    );
}
