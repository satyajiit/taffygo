// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The catalog this build actually ships.
//!
//! `catalog_document.rs` proves that *a* catalog document is decoded, judged
//! and merged correctly. This file proves something else, against different
//! authorities: that the document compiled into this build is the one the
//! decisions decided. Decision
//! `docs/decisions/0029-launch-provider-set.md` names the providers and the
//! terms each may be offered on, and decision
//! `docs/decisions/0028-credits-and-bundled-services.md` puts the metered
//! roster behind the managed route.
//!
//! Decision `docs/decisions/0094-the-provider-set-grows-by-catalog-row.md`
//! amends 0029: the set grows by catalog row, and a vendor is added once its
//! chat path is reachable — at first only where that path was the origin plus
//! what its wire family already compiles, and now also where the browser
//! carries a compiled prefix for the vendor, which is the "change to that
//! table and a row, in that order" 0094's consequences name. So this file pins
//! two things about the list rather than one — the eight rows 0029 decided,
//! which may not silently disappear, and the exact whole list, which may not
//! silently grow.
//!
//! Everything here reads the embedded baseline and nothing else. A generator
//! refuses to *emit* a baseline that breaks its source's rules
//! (`catalog/tools/generate_baseline.py`); these refuse to *ship* a build whose
//! baseline stopped saying what the decisions say. The two catch opposite
//! mistakes and neither substitutes for the other.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use model_router::catalog::{
    AuthMethod, MergedCatalog, ModelRole, ModelSource, PriceBasis, WireApi,
};
use model_router::embedded_baseline;

#[test]
fn embedded_baseline_parses_and_satisfies_every_invariant() {
    let (parsed, violations) = embedded_baseline().expect("the baseline is valid JSON");
    assert!(
        parsed.is_clean(),
        "the compiled-in baseline dropped entries: {:?}",
        parsed.defects
    );
    assert!(
        violations.is_empty(),
        "the compiled-in baseline breaks its own invariants: {violations:?}"
    );
    assert_eq!(parsed.document.header.schema_version, 1);
    assert!(!parsed.document.header.catalog_version.is_empty());
}

#[test]
fn one_descriptor_per_vendor_and_the_class_is_never_a_stored_flag() {
    // Decision 0029 item 2: a vendor reachable by both a key and a plan carries
    // both methods on one descriptor, because two descriptors would make the
    // provider class a stored flag — the thing decision 0015 forbids.
    //
    // OpenAI is the vendor that exercises the hard half. Its two ways in are
    // not the same request to the same place: a key reaches the platform's
    // responses family, and a subscription reaches a different family at a
    // different address, because each endpoint refuses what the other
    // requires. One row carries both and `wire_api_for`/`endpoint_for` decide
    // between them from the credential in play, which is exactly what "the
    // class is derived, never stored" has to mean to be worth anything. The
    // assertion this replaced said OpenAI offered a key alone; that was true
    // of the row and false of the product, because the binary had compiled the
    // subscription flow and the missing OAUTH made it unreachable.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    let by_id = |wanted: &str| {
        parsed
            .document
            .providers
            .iter()
            .find(|provider| provider.provider_id.as_str() == wanted)
            .expect("the provider is in the baseline")
    };
    for dual in ["anthropic", "openai", "xai"] {
        assert_eq!(
            by_id(dual).auth_methods,
            vec![AuthMethod::ApiKey, AuthMethod::Oauth],
            "{dual} is reachable both ways and must say so on one descriptor"
        );
    }
    assert!(by_id("xai").subscription);
    assert!(by_id("anthropic").subscription);

    let openai = by_id("openai");
    assert!(openai.subscription);
    assert_eq!(
        openai.wire_api_for(AuthMethod::ApiKey),
        WireApi::OpenAiResponses
    );
    assert_eq!(
        openai.wire_api_for(AuthMethod::Oauth),
        WireApi::OpenAiCodexResponses,
        "the subscription route is its own family, not a spelling of the key route"
    );
    assert_ne!(
        openai.endpoint_for(AuthMethod::ApiKey),
        openai.endpoint_for(AuthMethod::Oauth),
        "a key must never be spendable at the subscription address"
    );

    // The second address is an origin and nothing else. Decision 0049 keeps
    // every path in the browser process, so a path here would be the sandboxed
    // core naming where a request goes.
    let subscription = openai.endpoint_for(AuthMethod::Oauth).as_str();
    assert_eq!(
        subscription
            .strip_prefix("https://")
            .expect("every endpoint is https")
            .find('/'),
        None,
        "{subscription} carries a path the browser would not send"
    );

    // A vendor with one way in stays that way, and neither column appears.
    let anthropic = by_id("anthropic");
    assert_eq!(
        anthropic.wire_api_for(AuthMethod::Oauth),
        anthropic.wire_api_for(AuthMethod::ApiKey)
    );
    assert_eq!(
        anthropic.endpoint_for(AuthMethod::Oauth),
        anthropic.endpoint_for(AuthMethod::ApiKey)
    );
}

/// Every row whose models are served rather than shipped, and the origin each
/// one is served from.
///
/// The origin is here rather than only in the row because it is the half a
/// listing route is composed under, and the two are compiled in different
/// languages in different processes: this list and
/// `ProfileProviderListingFetcher::RouteFor` in
/// `//taffy/browser/model/profile_provider_listing_fetcher.cc` have to name
/// the same vendor at the same address or the fetch reaches nowhere. Nothing
/// automated compares them — the browser table is C++ and this is a host test
/// — so the pairing is written down on both sides and each says to look at the
/// other.
const LISTED_MODEL_ROWS: [(&str, &str); 5] = [
    ("chutes", "https://llm.chutes.ai"),
    ("kilocode", "https://api.kilo.ai"),
    ("novita", "https://api.novita.ai"),
    ("openrouter", "https://openrouter.ai"),
    ("venice", "https://api.venice.ai"),
];

#[test]
fn a_listing_row_carries_only_the_authoritative_route_facts() {
    // A row whose models are served carries three facts and no models, and
    // each of the three is load-bearing somewhere else: the origin is what a
    // listing path is composed under, the family is half the key the browser's
    // route table is looked up by, and the source is what makes the core plan
    // a fetch at all. Shipping a model beside them would be the failure this
    // pins: a listed provider with a model in the baseline offers that one
    // model before any credential exists, and a person would see a roster
    // entry for a provider they have not connected.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    for (provider_id, origin) in LISTED_MODEL_ROWS {
        let provider = parsed
            .document
            .providers
            .iter()
            .find(|provider| provider.provider_id.as_str() == provider_id)
            .expect("the listed provider is in the baseline");
        assert_eq!(provider.default_endpoint.as_str(), origin, "{provider_id}");
        assert_eq!(
            provider.wire_api,
            WireApi::OpenAiCompletions,
            "{provider_id}"
        );
        assert_eq!(
            provider.model_source,
            ModelSource::DynamicListing,
            "{provider_id}"
        );
        assert!(provider.enabled, "{provider_id}");
        assert!(
            !parsed
                .document
                .models
                .iter()
                .any(|model| model.provider_id.as_str() == provider_id),
            "{provider_id} serves its own list, so the baseline ships none of its models"
        );
    }
    // And the other direction, which is the half that catches a row switched
    // to a served list without anybody adding it here or to the browser's
    // route table.
    for provider in &parsed.document.providers {
        if provider.model_source == ModelSource::DynamicListing {
            assert!(
                LISTED_MODEL_ROWS
                    .iter()
                    .any(|(listed, _)| *listed == provider.provider_id.as_str()),
                "{} serves its own list and is not named above, so no route was compiled for it",
                provider.provider_id.as_str()
            );
        }
    }
}

#[test]
fn every_subscription_row_is_reachable_and_priced_as_a_plan() {
    // The rows added for a plan rather than a key. Each is OAUTH-only,
    // which is what a vendor that issues no pasteable key looks like, and each
    // prices its models IMPLIED — the basis that says the provider bills
    // nothing per request and the rates exist so a plan-backed request still
    // has a value to spend against a budget. `catalog::validate` refuses
    // IMPLIED under a provider that is not subscription-backed, so these two
    // facts are one fact and this pins both halves of it.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    for provider_id in ["github-copilot", "kimi-coding"] {
        let provider = parsed
            .document
            .providers
            .iter()
            .find(|entry| entry.provider_id.as_str() == provider_id)
            .expect("the subscription provider is in the baseline");
        assert_eq!(provider.auth_methods, vec![AuthMethod::Oauth]);
        assert!(provider.subscription, "{provider_id} is plan-backed");
        assert!(provider.enabled);
        let models: Vec<_> = parsed
            .document
            .models
            .iter()
            .filter(|model| model.provider_id.as_str() == provider_id)
            .collect();
        assert!(
            models.len() >= 2,
            "{provider_id} ships fewer models than the roles it is offered for"
        );
        for model in models {
            assert_eq!(
                model.cost.basis,
                PriceBasis::Implied,
                "{} is reached on a plan and must not claim it is metered",
                model.model_id
            );
            assert!(
                model.cost.input_micros_per_million > 0 && model.cost.output_micros_per_million > 0,
                "{} imputes nothing, so a plan-backed turn would spend no budget",
                model.model_id
            );
        }
    }
}

#[test]
fn every_compiled_row_ships_open_and_the_one_the_terms_forbid_says_why() {
    // Decision 0029 item 4 ships this vendor against its own recommendation.
    // The kill switch is still what makes that survivable — it is what held the
    // row shut from 0029 through decision 0113 section 4, which dated the row
    // and recorded that turning it on is a separate act only the owner makes —
    // and decision 0125 is that act. So the assertion inverts rather than
    // disappearing: what it pins now is that nothing ships shut by accident,
    // and that the one row whose terms are argued still names the record that
    // argued them.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    for provider in &parsed.document.providers {
        assert!(
            provider.enabled,
            "{} ships shut, and a compiled row nobody can reach is worse than no row",
            provider.provider_id
        );
    }
}

#[test]
fn every_wire_family_the_kernel_speaks_has_a_provider_that_speaks_it() {
    // Decision 0029 item 1 claims the launch set covers every family the
    // kernel implements. An adapter with no provider behind it is an adapter
    // nobody can exercise — which is what this catches, and it is the only
    // thing that catches it: a dialect row, a browser route and three enum
    // spellings all compile perfectly with no catalog row naming them.
    //
    // One family is spoken by nobody, on record. Cloud Code Assist was built
    // for the one row decision 0125 switched on, and decision 0134 withdrew
    // that row; the family stays because a contract enumeration member
    // cannot be removed (the generators refuse removal), so it is named here
    // with the record that emptied it. The allowance is checked both ways: a
    // row that speaks the family again must take it off this list, or the
    // list would be describing a product that no longer exists.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    let spoken_by_nobody = [(WireApi::GoogleCloudCodeAssist, "decision 0134")];
    for family in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage,
        WireApi::GoogleCloudCodeAssist,
    ] {
        let spoken = parsed
            .document
            .providers
            .iter()
            .any(|provider| provider.wire_api == family);
        match spoken_by_nobody.iter().find(|(empty, _)| *empty == family) {
            Some((_, record)) => assert!(
                !spoken,
                "a provider speaks {family:?} again; take it off the list {record} put it on"
            ),
            None => assert!(spoken, "no provider speaks {family:?}"),
        }
    }
}

#[test]
fn every_answerable_role_has_a_candidate_and_retrieval_has_none() {
    // One completeness claim and one deliberate hole, and the hole is the
    // reason this test is written as two halves rather than a loop.
    //
    // The managed roster left with the managed route. This test used to
    // assert that the `cloudflare-workers-ai` subset covered every role by
    // itself, because a gap there surfaced as `NoCandidate` on a route a
    // person had paid for. Decision 0200 deleted that route, nothing was ever
    // bought, and no account ever held an entitlement, so there is no roster
    // left to be complete and the claim is not weakened here — its subject is
    // gone.
    //
    // What its removal takes with it is retrieval. `cloudflare-workers-ai`
    // carried every EMBEDDING row in the catalog, so the role now resolves to
    // nothing at all. That is the owner's decision of 2026-09-20 — remove the
    // row, and plan on-device embeddings — and it is asserted rather than
    // skipped on purpose. A loop that quietly excused EMBEDDING would read to
    // the next person as drift somebody did not finish; a check that *fails
    // when a row appears* says a design was owed and forces this comment to be
    // read before one is added.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    let catalog = MergedCatalog::from_baseline(&parsed.document);
    for role in [
        ModelRole::PrimaryReasoning,
        ModelRole::FastBrowsing,
        ModelRole::Vision,
    ] {
        let candidates = catalog.candidates_for_role(role);
        assert!(!candidates.is_empty(), "{role:?} has no candidate");
    }
    assert!(
        catalog.candidates_for_role(ModelRole::Embedding).is_empty(),
        "an EMBEDDING row is in the catalog. Retrieval is uncatalogued on \
         purpose pending an on-device embedding design (owner, 2026-09-20); \
         adding a row means that design exists, so revisit this test and the \
         decision behind it rather than deleting the assertion"
    );
    // The managed reasoning failover order was pinned here, as the five
    // `@cf/...` rows in the order the managed route would try them. It is
    // deleted rather than rewritten against another provider: failover order
    // mattered because one route paid for a disclosure class and could not
    // leave it, and no route here does that any more.
}

#[test]
fn every_task_role_is_routable_on_a_bring_your_own_key() {
    // The property the baseline-0003 rows exist for. With no bring-your-own
    // model in the catalog, `Route::ByoDirect` answered `NoCandidate` for
    // every role no matter how many keys a person stored — a dead end wearing
    // a configuration screen. With them, the worst answer for a person with no
    // key is `CredentialMissing`, which names the thing they can do about it.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    let catalog = MergedCatalog::from_baseline(&parsed.document);
    for role in [
        ModelRole::PrimaryReasoning,
        ModelRole::FastBrowsing,
        ModelRole::Vision,
    ] {
        let candidates = catalog.candidates_for_role(role);
        let byo: Vec<_> = candidates
            .iter()
            .filter(|resolved| resolved.provider.provider_id.as_str() != "cloudflare-workers-ai")
            .collect();
        assert!(
            !byo.is_empty(),
            "{role:?} has no bring-your-own candidate, so a direct route dead-ends"
        );
        for resolved in &byo {
            assert!(
                resolved.provider.enabled && resolved.model.enabled,
                "{role:?} bring-your-own candidate {} is switched off",
                resolved.model.model_id.as_str()
            );
            assert!(
                resolved.model.tool_calling,
                "{role:?} bring-your-own candidate {} cannot call tools",
                resolved.model.model_id.as_str()
            );
        }
    }
    // Retrieval used to be asserted here as "every EMBEDDING row belongs to
    // the managed roster". With that roster gone the set is empty, and `all`
    // over an empty set is true — so the check would have passed forever
    // while proving nothing, which is the shape decision 0212 is about. The
    // claim now lives once, as an emptiness assertion with its reason, in
    // `every_answerable_role_has_a_candidate_and_retrieval_has_none`.
}

/// The per-model route override, proved on the row that needs it rather than
/// on a fixture.
///
/// This vendor's chat models are served on the completions path its provider
/// row names, and `grok-build-0.1` is served on the Responses path instead. The
/// catalog has carried `Model::wire_api` for that case all along; what it could
/// not do until this change was *say* so — neither the source auditor nor the
/// generator knew the key, so a row that stated the override was accepted by
/// the audit and then dropped on the way out. The failure was silent in the
/// worst direction: the baseline looked complete, the decoder read no override,
/// and every call to this model went to the wrong path with a body the vendor
/// answers as an error nobody attributes to a missing column.
///
/// So this asserts the whole chain, not the field: the provider still names the
/// completions family, the model overrides it, and the override is what the
/// router resolves for both kinds of credential — because the override is not a
/// property of how a person signed in.
#[test]
fn a_model_may_name_a_wire_family_its_provider_does_not() {
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    let catalog = MergedCatalog::from_baseline(&parsed.document);

    let provider = parsed
        .document
        .providers
        .iter()
        .find(|provider| provider.provider_id.as_str() == "xai")
        .expect("the xai row is in the baseline");
    assert_eq!(
        provider.wire_api,
        WireApi::OpenAiCompletions,
        "the provider row still names the family every other Grok model takes"
    );

    let resolved = catalog
        .candidates_for_role(ModelRole::FastBrowsing)
        .into_iter()
        .find(|resolved| resolved.model.model_id.as_str() == "grok-build-0.1")
        .expect("grok-build-0.1 is an enabled FAST_BROWSING candidate");
    assert_eq!(
        resolved.model.wire_api,
        Some(WireApi::OpenAiResponses),
        "the override survived the generator and the decoder"
    );
    for method in [AuthMethod::ApiKey, AuthMethod::Oauth] {
        assert_eq!(
            resolved.wire_api_for(method),
            WireApi::OpenAiResponses,
            "the override decides the family whichever credential is spent"
        );
    }

    // Its address is not overridden: the provider's origin joined with what the
    // Responses family already compiles is exactly where the vendor serves it,
    // and an endpoint here would be a second place to keep that right.
    assert!(
        resolved.model.endpoint.is_none(),
        "grok-build-0.1 takes its provider's address"
    );

    // The neighbouring row is the control: it states no family and takes the
    // provider's, so a change that made the override unconditional would fail
    // here rather than silently repointing every Grok model.
    let neighbour = catalog
        .candidates_for_role(ModelRole::FastBrowsing)
        .into_iter()
        .find(|resolved| resolved.model.model_id.as_str() == "grok-4.3")
        .expect("grok-4.3 is an enabled FAST_BROWSING candidate");
    assert_eq!(neighbour.model.wire_api, None);
    assert_eq!(
        neighbour.wire_api_for(AuthMethod::ApiKey),
        WireApi::OpenAiCompletions
    );
}
