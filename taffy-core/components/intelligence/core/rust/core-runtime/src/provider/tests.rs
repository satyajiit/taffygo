// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider plane's own rules, driven through its public surface.

use model_router::catalog::CatalogLayer;
use model_router::catalog::Endpoint;
use model_router::CredentialState;

mod custom;
mod status_projection;

use super::{
    CatalogProvider, CredentialHandle, CustomProvider, ProviderAuthMethod, ProviderDisplayName,
    ProviderEffect, ProviderError, ProviderId, ProviderOrigin, ProviderPresentation,
    ProviderProtocol, ProviderWireApi, SavedCustomProvider, MAX_PENDING_SIGN_IN_FLOWS,
};

/// One save with nothing behind it, for the tests that are about the record's
/// own rules rather than about what it carries.
fn saved(
    provider_id: ProviderId,
    display_name: ProviderDisplayName,
    endpoint: Endpoint,
    wire_api: ProviderWireApi,
    credential: Option<CredentialHandle>,
) -> SavedCustomProvider {
    SavedCustomProvider {
        provider_id,
        display_name,
        endpoint,
        wire_api,
        credential,
        models: Vec::new(),
        detected_server: None,
    }
}

fn id(value: &str) -> ProviderId {
    ProviderId::new(value).expect("valid provider identity")
}

fn handle(value: &str) -> CredentialHandle {
    CredentialHandle::new(value).expect("valid handle")
}

fn endpoint(value: &str) -> Endpoint {
    Endpoint::new(value).expect("valid endpoint")
}

fn name(value: &str) -> ProviderDisplayName {
    ProviderDisplayName::new(value).expect("valid name")
}

/// Three catalog providers standing for the three shapes that exist: key-only,
/// key-or-subscription, and one shipped switched off.
fn plane() -> ProviderProtocol {
    ProviderProtocol::new(
        [
            CatalogProvider {
                provider_id: id("anthropic"),
                display_name: "Anthropic".to_owned(),
                auth_methods: vec![ProviderAuthMethod::ApiKey],
                enabled: true,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            },
            CatalogProvider {
                provider_id: id("xai"),
                display_name: "xAI".to_owned(),
                auth_methods: vec![ProviderAuthMethod::ApiKey, ProviderAuthMethod::Oauth],
                enabled: true,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            },
            CatalogProvider {
                provider_id: id("switched-off-vendor"),
                display_name: "Switched-Off Vendor".to_owned(),
                auth_methods: vec![ProviderAuthMethod::Oauth],
                enabled: false,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            },
        ],
        [],
    )
}

#[test]
fn a_saved_credential_is_visible_to_the_router() {
    let mut plane = plane();
    assert_eq!(plane.credentials().count(), 0);
    let effect = plane
        .save_credential(id("anthropic"), ProviderAuthMethod::ApiKey, handle("h-1"))
        .expect("anthropic offers a key");
    assert_eq!(effect, ProviderEffect::None);
    let stored: Vec<_> = plane.credentials().collect();
    assert_eq!(stored.len(), 1);
    assert_eq!(stored[0].provider_id, id("anthropic"));
    assert_eq!(stored[0].state, CredentialState::Usable);
}

#[test]
fn replacing_a_credential_releases_the_handle_it_replaced() {
    // The browser owns the material behind a handle. Once this plane stops
    // naming it, nothing else ever will, so the replaced one has to come back
    // out or the store keeps a key for a credential no component references.
    let mut plane = plane();
    plane
        .save_credential(id("anthropic"), ProviderAuthMethod::ApiKey, handle("old"))
        .expect("first save");
    let effect = plane
        .save_credential(id("anthropic"), ProviderAuthMethod::ApiKey, handle("new"))
        .expect("second save");
    assert_eq!(effect, ProviderEffect::ReleaseHandle(handle("old")));
    assert_eq!(plane.credentials().count(), 1);
}

#[test]
fn saving_the_same_handle_twice_releases_nothing() {
    let mut plane = plane();
    plane
        .save_credential(id("anthropic"), ProviderAuthMethod::ApiKey, handle("h"))
        .expect("first save");
    let effect = plane
        .save_credential(id("anthropic"), ProviderAuthMethod::ApiKey, handle("h"))
        .expect("idempotent save");
    assert_eq!(
        effect,
        ProviderEffect::None,
        "a live handle must not be released"
    );
}

#[test]
fn forgetting_a_credential_releases_it_and_keeps_the_provider() {
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
        .forget_credential(&id("my-gateway"))
        .expect("held one");
    assert_eq!(effect, ProviderEffect::ReleaseHandle(handle("k")));
    assert_eq!(
        plane.custom_providers().count(),
        1,
        "revoking a key must not discard what the person set up"
    );
    assert_eq!(plane.credentials().count(), 0);
}

#[test]
fn forgetting_a_credential_nobody_holds_is_refused() {
    let mut plane = plane();
    assert_eq!(
        plane.forget_credential(&id("anthropic")),
        Err(ProviderError::UnknownProvider)
    );
}

#[test]
fn a_credential_is_refused_for_a_method_the_provider_does_not_offer() {
    let mut plane = plane();
    assert_eq!(
        plane.save_credential(id("anthropic"), ProviderAuthMethod::Oauth, handle("h")),
        Err(ProviderError::MethodNotOffered),
        "a record no route can spend must not be filed"
    );
}

#[test]
fn a_provider_the_catalog_ships_switched_off_holds_no_credential() {
    // A synthetic row, and deliberately so since decision 0125 turned the last
    // compiled row on: nothing that ships is switched off any more, so this
    // rule is stated here or nowhere. The plane refuses rather than letting a
    // dormant row become a working flow.
    let mut plane = plane();
    assert_eq!(
        plane.save_credential(
            id("switched-off-vendor"),
            ProviderAuthMethod::Oauth,
            handle("h")
        ),
        Err(ProviderError::ProviderDisabled)
    );
}

#[test]
fn the_roster_reports_every_catalog_provider_and_the_persons_own() {
    let mut plane = plane();
    plane
        .save_credential(id("anthropic"), ProviderAuthMethod::ApiKey, handle("h"))
        .expect("saved");
    plane
        .save_custom_provider(saved(
            id("my-gateway"),
            name("My gateway"),
            endpoint("https://gateway.example/v1"),
            ProviderWireApi::OpenAiCompletions,
            None,
        ))
        .expect("defined");
    let roster = plane.roster();
    assert_eq!(
        roster.len(),
        4,
        "three catalog entries and one of the person's"
    );

    let anthropic = roster
        .iter()
        .find(|view| view.provider_id == id("anthropic"))
        .expect("present");
    let stored = anthropic.stored.as_ref().expect("a credential is stored");
    assert_eq!(stored.state, CredentialState::Usable);
    assert_eq!(stored.auth_method, ProviderAuthMethod::ApiKey);
    assert!(!stored.subscription_backed);
    assert_eq!(anthropic.origin, ProviderOrigin::Catalog);
    assert!(anthropic.enabled);
    assert_eq!(anthropic.endpoint_host, None);

    let shut = roster
        .iter()
        .find(|view| view.provider_id == id("switched-off-vendor"))
        .expect("present even though it is switched off");
    assert_eq!(
        shut.stored, None,
        "a vendor with no credential reports nothing stored rather than vanishing"
    );
    assert!(
        !shut.enabled,
        "the row's own switch is a roster fact, not a reason to hide the row"
    );

    let custom = roster
        .iter()
        .find(|view| view.provider_id == id("my-gateway"))
        .expect("present");
    assert_eq!(custom.origin, ProviderOrigin::Custom);
    assert_eq!(custom.auth_methods, vec![ProviderAuthMethod::ApiKey]);
    assert!(custom.enabled);
    assert_eq!(
        custom.endpoint_host.as_deref(),
        Some("gateway.example"),
        "the roster shows a host and never a path"
    );
}

#[test]
fn a_subscription_credential_is_reported_as_plan_backed() {
    let mut plane = plane();
    plane
        .save_credential(id("xai"), ProviderAuthMethod::Oauth, handle("h"))
        .expect("xai offers a subscription");
    let view = plane
        .roster()
        .into_iter()
        .find(|view| view.provider_id == id("xai"))
        .expect("present");
    let stored = view.stored.as_ref().expect("a credential is stored");
    assert!(stored.subscription_backed);
    // Never a stored flag per vendor: it is read back from the credential in
    // use, which is what decisions 0015 and 0029 require.
    assert_eq!(stored.auth_method, ProviderAuthMethod::Oauth);
}

#[test]
fn restore_installs_durable_state_without_emitting_anything() {
    let mut plane = plane();
    plane.restore(
        [super::ProviderCredential {
            provider_id: id("anthropic"),
            auth_method: ProviderAuthMethod::ApiKey,
            handle: handle("restored"),
            state: CredentialState::Usable,
        }],
        [CustomProvider {
            provider_id: id("my-gateway"),
            display_name: name("My gateway"),
            endpoint: endpoint("https://gateway.example/v1"),
            wire_api: ProviderWireApi::OpenAiCompletions,
            credential: None,
            models: Vec::new(),
            detected_server: None,
        }],
    );
    assert_eq!(plane.credentials().count(), 1);
    assert_eq!(plane.custom_providers().count(), 1);
    assert_eq!(plane.roster().len(), 4);
}

#[test]
fn the_catalog_identities_are_reserved() {
    let plane = plane();
    let reserved = plane.catalog_ids();
    assert!(reserved.contains(&id("anthropic")));
    assert!(reserved.contains(&id("switched-off-vendor")));
    assert!(!reserved.contains(&id("my-gateway")));
}

#[test]
fn an_admitted_sign_in_is_the_rosters_signing_in_and_its_terminal_clears_it() {
    let mut plane = plane();
    plane
        .begin_sign_in(id("xai"), "flow-1".to_owned(), "binding-1".to_owned())
        .expect("xai offers OAUTH and ships enabled");
    let row = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "xai")
        .expect("the row exists");
    assert!(row.signing_in);
    plane
        .finish_sign_in("flow-1", "binding-1")
        .expect("the recorded pair clears the flow");
    let row = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "xai")
        .expect("the row exists");
    assert!(!row.signing_in);
}

#[test]
fn a_sign_in_for_a_switched_off_vendor_is_refused_before_any_record() {
    // The fixture offers OAUTH and the catalog holds it shut, so the row's own
    // switch is what answers — not the method check, and not a pending marker
    // that would say a flow ran. It named Google Antigravity until decision
    // 0125 turned that row on; no compiled row ships shut now, which is why
    // this vendor is invented rather than borrowed.
    let mut plane = plane();
    assert_eq!(
        plane.begin_sign_in(
            id("switched-off-vendor"),
            "flow-1".to_owned(),
            "binding-1".to_owned(),
        ),
        Err(ProviderError::ProviderDisabled)
    );
    assert!(plane.roster().iter().all(|row| !row.signing_in));
}

#[test]
fn a_flow_identity_may_not_be_reused_across_providers() {
    let mut plane = plane();
    plane.replace_catalog([oauth_vendor("vendor-a"), oauth_vendor("vendor-b")], []);
    plane
        .begin_sign_in(id("vendor-a"), "flow-1".to_owned(), "binding-1".to_owned())
        .expect("admitted");
    assert_eq!(
        plane.begin_sign_in(id("vendor-b"), "flow-1".to_owned(), "binding-2".to_owned()),
        Err(ProviderError::FlowAlreadyRunning),
        "a browser that reuses a flow identity is a bug, not a second flow"
    );
}

#[test]
fn pending_sign_ins_are_bounded() {
    let mut plane = plane();
    plane.replace_catalog(
        (0..=MAX_PENDING_SIGN_IN_FLOWS).map(|index| oauth_vendor(&format!("vendor-{index}"))),
        [],
    );
    for index in 0..MAX_PENDING_SIGN_IN_FLOWS {
        plane
            .begin_sign_in(
                id(&format!("vendor-{index}")),
                format!("flow-{index}"),
                "binding-1".to_owned(),
            )
            .expect("within the bound");
    }
    assert_eq!(
        plane.begin_sign_in(
            id(&format!("vendor-{MAX_PENDING_SIGN_IN_FLOWS}")),
            "flow-over".to_owned(),
            "binding-1".to_owned(),
        ),
        Err(ProviderError::TooManyPendingFlows)
    );
}

fn oauth_vendor(provider_id: &str) -> CatalogProvider {
    CatalogProvider {
        provider_id: id(provider_id),
        display_name: provider_id.to_owned(),
        auth_methods: vec![ProviderAuthMethod::Oauth],
        enabled: true,
        layer: CatalogLayer::EmbeddedBaseline,
        configurable: false,
        subscription: false,
        endpoint_changed: false,
        refused_endpoint_host: None,
        presentation: ProviderPresentation::default(),
    }
}
