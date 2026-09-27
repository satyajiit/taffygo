// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The roster contributor's own cases.
//!
//! Split out of the suite beside it because this contributor is the one that
//! projects a whole plane record field for field, so its expectation is a
//! literal as long as the type — and a type that gains a field grows this file
//! and nothing else. The fixtures stay in the parent: three of them are shared
//! with the contributors that decorate other fields, and a second copy here
//! would be a second roster to keep in step.

use std::collections::BTreeMap;

use model_router::CredentialState;

use super::{fixture, provider_status, roster};
use crate::adapters::crosscutting::status::ProductionProviderRoster;
use crate::ports::{StatusContributionFacts, StatusContributorPort};
use crate::provider::{ProviderAuthMethod, StoredCredentialView};

#[test]
fn the_roster_contributor_projects_the_plane_view_field_for_field() {
    let available = Vec::new();
    let ask_prompts = BTreeMap::new();
    let provider_roster = roster();
    let provider_status = provider_status(&provider_roster);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &available,
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    let mut status = fixture();
    ProductionProviderRoster.contribute(&facts, &mut status);

    assert_eq!(
        status.provider_roster,
        vec![
            core_api_types::ProviderRosterEntry {
                provider_id: "xai".to_owned(),
                display_name: "xAI".to_owned(),
                origin: core_api_types::ProviderOriginView::Catalog,
                auth_methods: vec![
                    core_api_types::ProviderAuthMethodView::ApiKey,
                    core_api_types::ProviderAuthMethodView::Oauth,
                ],
                stored: Some(core_api_types::StoredCredentialView {
                    auth_method: core_api_types::ProviderAuthMethodView::Oauth,
                    state: core_api_types::ProviderCredentialStateView::NeedsSignIn,
                    subscription_backed: true,
                    account_label: None,
                    plan_label: None,
                }),
                signing_in: false,
                enabled: true,
                endpoint_host: None,
                endpoint_base: None,
                last_refusal: Some(core_api_types::ProviderRefusalStateView {
                    refusal: core_api_types::ProviderRefusalView::RateLimit,
                    at_monotonic_ms: 21_000,
                }),
                configurable: true,
                subscription: false,
                endpoint_changed: true,
                refused_endpoint_host: Some("api.grok.example.invalid".to_owned()),
                catalog_layer: core_api_types::CatalogLayerView::RemoteOverlay,
                selected_model_id: None,
                thinking: None,
                presentation: None,
                model_count: 0,
            },
            core_api_types::ProviderRosterEntry {
                provider_id: "my-gateway".to_owned(),
                display_name: "My gateway".to_owned(),
                origin: core_api_types::ProviderOriginView::Custom,
                auth_methods: vec![core_api_types::ProviderAuthMethodView::ApiKey],
                stored: None,
                signing_in: false,
                enabled: true,
                endpoint_host: Some("gateway.example".to_owned()),
                endpoint_base: Some("http://gateway.example:11434/v1".to_owned()),
                last_refusal: None,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                catalog_layer: core_api_types::CatalogLayerView::UserOverride,
                selected_model_id: None,
                thinking: None,
                presentation: None,
                model_count: 0,
            },
        ],
    );
}

#[test]
fn a_stored_view_claiming_absent_projects_no_stored_record() {
    // No write path produces this shape, but the type admits it, and the
    // wire enum is closed at the three states a record can hold. The
    // contributor must answer with absence rather than invent a member.
    let available = Vec::new();
    let ask_prompts = BTreeMap::new();
    let mut provider_roster = roster();
    if let Some(entry) = provider_roster.first_mut() {
        entry.stored = Some(StoredCredentialView {
            auth_method: ProviderAuthMethod::ApiKey,
            state: CredentialState::Absent,
            subscription_backed: false,
        });
    }
    let provider_status = provider_status(&provider_roster);
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &available,
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    let mut status = fixture();
    ProductionProviderRoster.contribute(&facts, &mut status);
    let first = status
        .provider_roster
        .first()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(first.stored, None);
}
