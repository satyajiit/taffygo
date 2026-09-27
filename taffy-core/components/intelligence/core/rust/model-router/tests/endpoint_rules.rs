// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The two endpoint rules, and the wall between them.
//!
//! One type holds an address and two constructors decide what may become one,
//! and decision 0096 section 2 requires that they never be written in terms of
//! each other:
//!
//!   * `Endpoint::new` is the catalog rule. A served document may name an
//!     `https` origin and nothing else, and decision 0080's endpoint guard is
//!     written on top of that staying true.
//!   * `Endpoint::user_base_url` is the rule for the one address a person
//!     typed for a server they run themselves. The port and the base path are
//!     theirs, and plain http is not refused here at all — which addresses it
//!     may reach is the browser's answer, not this crate's.
//!
//! A suite that asserted only one of them would let the other be quietly
//! widened into it, so both are here and the case in the middle —
//! `the_two_rules_disagree_and_that_is_the_design` — names addresses each one
//! accepts and the other refuses. If that case ever finds no disagreement, the
//! two rules have become one.
//!
//! The browser holds the same wall in the same shape, in
//! `taffy-core/browser/core_api/core_api_command_factory_provider_endpoint_unittest.cc`.
//! Neither suite can see the other, so each states its own half.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use model_router::catalog::types::MAX_ENDPOINT_LEN;
use model_router::catalog::{
    parse_document, DefectLocation, DefectReason, Endpoint, EndpointError, UserEndpointError,
};

/// The address decision 0096 was written for: a person's own server, on the
/// machine in front of them, with a port and a base path and no certificate.
const OWN_SERVER: &str = "http://localhost:11434/v1";

/// Every address a person might reasonably type for a server they run, and the
/// host each one is disclosed as reaching.
///
/// The second column is written out rather than derived, because deriving it
/// is what the code under test does and a test that repeated the derivation
/// would agree with any answer.
const REGISTRABLE: [(&str, &str); 6] = [
    (OWN_SERVER, "localhost:11434"),
    ("http://192.168.1.9:11434/v1", "192.168.1.9:11434"),
    (
        "http://[fd12:3456:789a::1]:8000/v1",
        "[fd12:3456:789a::1]:8000",
    ),
    ("http://tower.local:1234", "tower.local:1234"),
    ("https://gateway.example.com/v1", "gateway.example.com"),
    (
        "https://gateway.example.com:8443/openai/v1",
        "gateway.example.com:8443",
    ),
];

fn document_with(providers: &str, models: &str) -> String {
    format!(
        r#"{{"schema_version": 1, "catalog_version": "endpoint-rules-1",
            "generated_at": "2027-01-01T00:00:00Z",
            "providers": [{providers}], "models": [{models}]}}"#
    )
}

fn provider_with(endpoint: &str, extra: &str) -> String {
    format!(
        r#"{{"schema_version": 1, "provider_id": "own-server",
            "display_name": "My server", "wire_api": "OPEN_AI_COMPLETIONS",
            "default_endpoint": "{endpoint}", {extra}
            "auth_methods": ["API_KEY"], "subscription": false,
            "model_source": "STATIC_CATALOG", "enabled": true}}"#
    )
}

fn model_with(endpoint: &str) -> String {
    format!(
        r#"{{"schema_version": 1, "model_id": "local-large",
            "provider_id": "own-server", "display_name": "Local Large",
            "endpoint": "{endpoint}",
            "roles": ["PRIMARY_REASONING"], "input_modalities": ["TEXT"],
            "reasoning": false, "tool_calling": true,
            "context_window": 8192, "max_output_tokens": 2048,
            "cost": {{"snapshot_version": "2027-01-01", "currency": "USD",
              "basis": "METERED", "input_micros_per_million": 0,
              "output_micros_per_million": 0,
              "cache_read_micros_per_million": 0,
              "cache_write_micros_per_million": 0}},
            "enabled": true}}"#
    )
}

// ---------------------------------------------------------------------------
// The catalog rule
// ---------------------------------------------------------------------------

#[test]
fn a_catalog_endpoint_is_an_absolute_https_url_with_a_host() {
    for accepted in [
        "https://api.vendor.example",
        "https://api.vendor.example/v1",
        "https://api.vendor.example:8443/v1",
    ] {
        assert!(
            Endpoint::new(accepted).is_ok(),
            "{accepted} is what a catalog row looks like"
        );
    }

    assert_eq!(
        Endpoint::new("http://api.vendor.example"),
        Err(EndpointError::NotHttps),
        "cleartext is not something a served document gets to ask for"
    );
    assert_eq!(
        Endpoint::new("api.vendor.example/v1"),
        Err(EndpointError::NotHttps)
    );
    assert_eq!(Endpoint::new("https://"), Err(EndpointError::NoHost));
    assert_eq!(
        Endpoint::new("https://user:secret@api.vendor.example"),
        Err(EndpointError::NoHost)
    );
    let too_long = format!("https://{}", "a".repeat(MAX_ENDPOINT_LEN));
    assert_eq!(Endpoint::new(&too_long), Err(EndpointError::TooLong));
}

// ---------------------------------------------------------------------------
// The person's rule
// ---------------------------------------------------------------------------

#[test]
fn an_address_a_person_typed_keeps_the_port_and_the_path_their_server_has() {
    for (accepted, _) in REGISTRABLE {
        let endpoint = Endpoint::user_base_url(accepted)
            .unwrap_or_else(|error| panic!("{accepted} is a server somebody runs: {error}"));
        assert_eq!(
            endpoint.as_str(),
            accepted,
            "the address is held as the bytes the person typed, since the \
             browser matches a request against the register by equality"
        );
    }
}

#[test]
fn a_person_s_address_is_refused_for_the_three_things_a_base_url_may_not_carry() {
    assert_eq!(
        Endpoint::user_base_url("http://user:secret@localhost:11434/v1"),
        Err(UserEndpointError::CarriesCredentials)
    );
    assert_eq!(
        Endpoint::user_base_url("http://localhost:11434/v1?api-key=abcdef"),
        Err(UserEndpointError::CarriesQuery)
    );
    assert_eq!(
        Endpoint::user_base_url("http://localhost:11434/v1#models"),
        Err(UserEndpointError::CarriesFragment)
    );
    assert_eq!(
        Endpoint::user_base_url("http://localhost?api-key=abcdef"),
        Err(UserEndpointError::CarriesQuery),
        "a query typed straight onto the host is a query, not part of the name"
    );
}

#[test]
fn a_person_s_address_is_refused_when_it_is_not_an_address_at_all() {
    for refused in [
        "ftp://localhost/v1",
        "file:///etc/passwd",
        "localhost:11434",
        "http://",
        "http:///v1",
        "https://",
    ] {
        assert_eq!(
            Endpoint::user_base_url(refused),
            Err(UserEndpointError::NotAnAddress),
            "{refused}"
        );
    }
    let too_long = format!("http://localhost/{}", "a".repeat(MAX_ENDPOINT_LEN));
    assert_eq!(
        Endpoint::user_base_url(&too_long),
        Err(UserEndpointError::TooLong)
    );
}

#[test]
fn which_addresses_cleartext_may_reach_is_not_answered_in_this_crate() {
    // Plain http to a public name is accepted *here*, and that is the design
    // rather than a hole. Decision 0096 section 1 puts the register in the
    // browser process and checks a model request against it by byte equality,
    // so the core cannot name a host a person did not type however lax this
    // constructor is; decision 0096 section 3's cleartext rule is applied by
    // `custom_provider_endpoint_policy.cc` with GURL and net::IPAddress, on a
    // host it has already canonicalized.
    //
    // The failure modes are not symmetric, which is why the line is drawn
    // here. A second address policy written in this crate could only ever be a
    // second opinion, and on the day it disagreed with the authority it would
    // refuse an address the browser had already accepted and registered — a
    // person's own server unreachable, for a reason nothing surfaces.
    assert!(Endpoint::user_base_url("http://api.vendor.example/v1").is_ok());
    assert!(Endpoint::user_base_url("http://169.254.169.254/latest").is_ok());
}

// ---------------------------------------------------------------------------
// The wall between them
// ---------------------------------------------------------------------------

#[test]
fn the_two_rules_disagree_and_that_is_the_design() {
    // Registrable, and not a catalog endpoint: cleartext, to a machine on the
    // person's own network or the one in front of them.
    let mut registrable_only = 0;
    for (endpoint, _) in REGISTRABLE {
        if !endpoint.starts_with("http://") {
            continue;
        }
        assert!(Endpoint::user_base_url(endpoint).is_ok(), "{endpoint}");
        assert!(Endpoint::new(endpoint).is_err(), "{endpoint}");
        registrable_only += 1;
    }
    assert!(
        registrable_only > 0,
        "if nothing a person may register is refused as a catalog endpoint, \
         the catalog rule has been widened to admit a person's own server"
    );

    // A catalog endpoint the register would not hold: the catalog rule reads
    // only the scheme, the host and the length, so a query survives it — and
    // an address a person is asked to type may not carry one.
    let with_a_query = "https://api.vendor.example/v1?beta=1";
    assert!(Endpoint::new(with_a_query).is_ok());
    assert_eq!(
        Endpoint::user_base_url(with_a_query),
        Err(UserEndpointError::CarriesQuery),
        "if this stops disagreeing, the person's rule has been narrowed to the \
         catalog's shape"
    );

    // The rules overlap, and they are simply not the same rule. What is
    // asserted above is only that the overlap is not everything.
    assert!(Endpoint::new("https://gateway.example.com/v1").is_ok());
    assert!(Endpoint::user_base_url("https://gateway.example.com/v1").is_ok());
}

// ---------------------------------------------------------------------------
// The decoder never asks the person's question
// ---------------------------------------------------------------------------

#[test]
fn a_served_document_is_never_held_to_the_person_s_rule() {
    // Three fields in a served document carry an address, and every one of
    // them drops its entry rather than admitting cleartext. A published
    // catalog that could name `http://` could name a host with no certificate
    // to check, which is the whole reason the catalog rule does not move.
    for (providers, models, field) in [
        (
            provider_with(OWN_SERVER, ""),
            String::new(),
            "default_endpoint",
        ),
        (
            provider_with(
                "https://api.vendor.example",
                r#""oauth_endpoint": "http://localhost:11434/v1","#,
            ),
            String::new(),
            "oauth_endpoint",
        ),
        (
            provider_with("https://api.vendor.example", ""),
            model_with(OWN_SERVER),
            "endpoint",
        ),
    ] {
        let parsed = parse_document(&document_with(&providers, &models)).expect("well-formed JSON");
        let defect = parsed
            .defects
            .first()
            .unwrap_or_else(|| panic!("{field} carrying {OWN_SERVER} is a defect"));
        assert_eq!(
            defect.reason,
            DefectReason::Invalid {
                field,
                detail: EndpointError::NotHttps.to_string(),
            },
            "{field} was refused for the wrong reason"
        );
        match field {
            "endpoint" => assert!(matches!(defect.location, DefectLocation::Model { .. })),
            _ => assert!(matches!(defect.location, DefectLocation::Provider { .. })),
        }
    }
}

#[test]
fn deserializing_an_endpoint_applies_the_catalog_rule() {
    // The type's serde bound goes through `Endpoint::new`, which is what keeps
    // the decoder honest at the one seam that does not name a constructor: a
    // document read through serde rather than through `parse_document` reaches
    // the same rule.
    assert!(serde_json::from_str::<Endpoint>(r#""https://api.vendor.example/v1""#).is_ok());
    assert!(serde_json::from_str::<Endpoint>(&format!("\"{OWN_SERVER}\"")).is_err());
}

// ---------------------------------------------------------------------------
// What a person is shown
// ---------------------------------------------------------------------------

#[test]
fn a_host_is_a_host_whichever_scheme_precedes_it() {
    // Reading only `https://` left the person's own address answering `http:`
    // — not their host, not even a host — in the roster row that names their
    // server and in the sentence that says where their page went.
    for (address, host) in REGISTRABLE {
        let endpoint = Endpoint::user_base_url(address).expect("registrable");
        assert_eq!(endpoint.host(), host, "{address}");
    }

    // The catalog side of the same function, unchanged: every verdict it gave
    // an `https` address before both schemes were read is the verdict it gives
    // now.
    assert_eq!(
        Endpoint::new("https://api.vendor.example/v1")
            .expect("a catalog row")
            .host(),
        "api.vendor.example"
    );
    assert_eq!(
        Endpoint::new("https://api.vendor.example")
            .expect("a catalog row")
            .host(),
        "api.vendor.example"
    );
}
