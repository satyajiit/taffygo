// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The status contributors' own suite.
//!
//! Split from the contributors themselves rather than left beside them: the
//! file carries six projections and the cases that pin each one, and the two
//! halves are read at different times.

mod model_budget;
mod roster_projection;

use std::collections::BTreeMap;

use model_router::CredentialState;

use super::{
    ProductionAccountMethods, ProductionAskPrompts, ProductionEntitlementView,
    ProductionProviderProbes, ProductionProviderRoster,
};
use crate::account::AccountAuthMethod;
use crate::entitlement_refresh::EntitlementDisplay;
use crate::ports::{StatusContributionFacts, StatusContributorPort};
use crate::probe::ProbeVerdict;
use model_router::catalog::CatalogLayer;

use crate::provider::{
    ProviderAuthMethod, ProviderId, ProviderModelPreference, ProviderOrigin, ProviderPresentation,
    ProviderRefusal, ProviderRefusalKind, ProviderStatusProjection, ProviderView,
    StoredCredentialView,
};

fn fixture() -> core_api_types::CoreStatus {
    core_api_types::CoreStatus {
        availability: core_api_types::CoreAvailability::Ready,
        generation: 7,
        active_tasks: vec![core_api_types::TaskViewState {
            task_id: "task-1".to_owned(),
            revision: 3,
            phase: core_api_types::TaskPhase::WaitingForUser,
            progress_basis_points: 0,
            status_message_key: None,
            failure: None,
            goal: String::new(),
            template_id: core_api_types::TaskTemplateId::SummarizeEvidence,
            pending_action: None,
            workspace_id: None,
            pending_ask_prompt: None,
            pending_field_value_request: None,
            allowed_controls: Vec::new(),
            artifacts: Vec::new(),
            activity: Vec::new(),
        }],
        auth_state: Some(core_api_types::AuthViewState {
            phase: core_api_types::AuthPhase::SignedOut,
            account: None,
            pending_email: None,
            failure: None,
            methods: Vec::new(),
            entitlement: None,
        }),
        workspaces: Vec::new(),
        workspace_export: None,
        asset_delivery: None,
        provider_roster: Vec::new(),
        provider_probes: Vec::new(),
        provider_models: Vec::new(),
        assistant_configuration: core_api_types::AssistantConfigurationView {
            revision: 0,
            disabled_abilities: Vec::new(),
            preset: core_api_types::PersonalityPresetView::CarefulResearcher,
            pace: 0,
            length: 1,
            check_in: 0,
        },
        library: core_api_types::LibraryViewState {
            availability: core_api_types::LibraryAvailability::Available,
            revision: 0,
            entries: Vec::new(),
            search: None,
            refresh_previews: Vec::new(),
            refresh_results: Vec::new(),
        },
        library_export: None,
        memory: core_api_types::MemoryViewState {
            availability: core_api_types::MemoryAvailability::Available,
            revision: 0,
            records: Vec::new(),
            search: None,
        },
        saved_sign_ins: core_api_types::SavedSignInsView {
            availability: core_api_types::SavedDataAvailability::Loading,
            revision: 0,
            records: Vec::new(),
        },
        saved_details: core_api_types::SavedDetailsView {
            availability: core_api_types::SavedDataAvailability::Loading,
            revision: 0,
            people: Vec::new(),
        },
        site_skills: Vec::new(),
        builtin_skills: Vec::new(),
        projection_mode: core_api_types::CoreStatusProjectionMode::Complete,
        projection_omissions: Vec::new(),
    }
}

fn provider_id(raw: &str) -> ProviderId {
    ProviderId::new(raw).unwrap_or_else(|_| unreachable!())
}

fn roster() -> Vec<ProviderView> {
    vec![
        ProviderView {
            provider_id: provider_id("xai"),
            display_name: "xAI".to_owned(),
            origin: ProviderOrigin::Catalog,
            auth_methods: vec![ProviderAuthMethod::ApiKey, ProviderAuthMethod::Oauth],
            stored: Some(StoredCredentialView {
                auth_method: ProviderAuthMethod::Oauth,
                state: CredentialState::NeedsSignIn,
                subscription_backed: true,
            }),
            signing_in: false,
            enabled: true,
            endpoint_host: None,
            endpoint_base: None,
            last_refusal: Some(ProviderRefusal {
                kind: ProviderRefusalKind::RateLimit,
                at_monotonic_ms: 21_000,
            }),
            configurable: true,
            subscription: false,
            endpoint_changed: true,
            refused_endpoint_host: Some("api.grok.example.invalid".to_owned()),
            catalog_layer: CatalogLayer::RemoteOverlay,
            preference: ProviderModelPreference::default(),
            presentation: ProviderPresentation::default(),
        },
        ProviderView {
            provider_id: provider_id("my-gateway"),
            display_name: "My gateway".to_owned(),
            origin: ProviderOrigin::Custom,
            auth_methods: vec![ProviderAuthMethod::ApiKey],
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
            catalog_layer: CatalogLayer::UserOverride,
            preference: ProviderModelPreference::default(),
            presentation: ProviderPresentation::default(),
        },
    ]
}

fn provider_status(roster: &[ProviderView]) -> ProviderStatusProjection {
    ProviderStatusProjection::for_testing(roster, &[])
}

#[test]
fn contributors_on_disjoint_fields_commute_and_contradict_nothing() {
    let available = vec![AccountAuthMethod::Google];
    let mut ask_prompts = BTreeMap::new();
    ask_prompts.insert("task-1".to_owned(), "Which account?".to_owned());
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

    let mut one_way = fixture();
    ProductionAccountMethods.contribute(&facts, &mut one_way);
    ProductionAskPrompts.contribute(&facts, &mut one_way);
    ProductionProviderRoster.contribute(&facts, &mut one_way);

    let mut other_way = fixture();
    ProductionProviderRoster.contribute(&facts, &mut other_way);
    ProductionAskPrompts.contribute(&facts, &mut other_way);
    ProductionAccountMethods.contribute(&facts, &mut other_way);

    assert_eq!(one_way, other_way);

    // The runtime's own facts are untouched: availability, generation, and
    // the task set survive decoration; only the decorated fields moved.
    let before = fixture();
    assert_eq!(one_way.availability, before.availability);
    assert_eq!(one_way.generation, before.generation);
    assert_eq!(one_way.active_tasks.len(), before.active_tasks.len());
    let decorated = one_way
        .active_tasks
        .first()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        decorated.pending_ask_prompt.as_deref(),
        Some("Which account?")
    );
    let auth = one_way
        .auth_state
        .as_ref()
        .unwrap_or_else(|| unreachable!());
    assert!(auth.methods.iter().any(|method| {
        method.provider == core_api_types::AuthProvider::Google
            && method.availability == core_api_types::AuthMethodAvailability::Available
    }));
}

#[test]
fn the_entitlement_contributor_fills_only_a_present_auth_state() {
    let available = vec![AccountAuthMethod::Google];
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::default();
    let display = EntitlementDisplay {
        plan_id: "plan-standard".to_owned(),
        credits_granted: 1_000,
        credits_remaining: 964,
        next_renewal_epoch_seconds: 1_788_912_000,
        valid_until_epoch_seconds: 0,
    };
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &available,
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: Some(&display),
        provider_probes: &[],
    };

    let mut status = fixture();
    ProductionEntitlementView.contribute(&facts, &mut status);
    let entitlement = status
        .auth_state
        .as_ref()
        .and_then(|auth| auth.entitlement.as_ref())
        .unwrap_or_else(|| unreachable!("a held display decorates the auth state"));
    assert_eq!(entitlement.plan_id, "plan-standard");
    assert_eq!(entitlement.credits_granted, 1_000);
    assert_eq!(entitlement.credits_remaining, 964);
    assert_eq!(entitlement.next_renewal_epoch_seconds, 1_788_912_000);
    assert_eq!(entitlement.valid_until_epoch_seconds, 0);

    // No auth state, nothing decorated — and nothing invented to hang it on.
    let mut absent = fixture();
    absent.auth_state = None;
    ProductionEntitlementView.contribute(&facts, &mut absent);
    assert!(absent.auth_state.is_none());

    // And no held display clears a stale one rather than keeping it.
    let bare = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &available,
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    ProductionEntitlementView.contribute(&bare, &mut status);
    assert!(status
        .auth_state
        .as_ref()
        .is_some_and(|auth| auth.entitlement.is_none()));
}

#[test]
fn the_probe_contributor_projects_every_verdict_and_needs_no_auth_state() {
    let available = Vec::new();
    let ask_prompts = BTreeMap::new();
    let provider_status = ProviderStatusProjection::default();
    let probes = vec![
        crate::probe::ProbeDisplay {
            provider_id: "anthropic".to_owned(),
            verdict: ProbeVerdict::Usable,
            at_monotonic_ms: 9_000,
            endpoint: None,
        },
        crate::probe::ProbeDisplay {
            provider_id: "xai".to_owned(),
            verdict: ProbeVerdict::Auth,
            at_monotonic_ms: 12_000,
            endpoint: None,
        },
        // Filed without a flight: a provider with nothing to probe with.
        crate::probe::ProbeDisplay {
            provider_id: "openrouter".to_owned(),
            verdict: ProbeVerdict::NoModelListed,
            at_monotonic_ms: 15_000,
            endpoint: None,
        },
    ];
    let facts = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &available,
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &probes,
    };

    // A person probes a key signed out, so the auth block is deliberately
    // absent here and the verdicts must land anyway.
    let mut status = fixture();
    status.auth_state = None;
    ProductionProviderProbes.contribute(&facts, &mut status);
    assert_eq!(status.provider_probes.len(), 3);
    let first = status
        .provider_probes
        .first()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(first.provider_id, "anthropic");
    assert_eq!(
        first.verdict,
        core_api_types::ProviderProbeVerdictView::Usable
    );
    assert_eq!(first.at_monotonic_ms, 9_000);
    let second = status
        .provider_probes
        .get(1)
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        second.verdict,
        core_api_types::ProviderProbeVerdictView::Auth
    );
    let third = status
        .provider_probes
        .get(2)
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        third.verdict,
        core_api_types::ProviderProbeVerdictView::NoModelListed
    );
    assert!(third.endpoint.is_none());

    // No held verdicts clears a stale projection rather than keeping it.
    let bare = StatusContributionFacts {
        builtin_skills: &[],
        available_account_methods: &available,
        ask_prompts: &ask_prompts,
        provider_status: &provider_status,
        entitlement: None,
        provider_probes: &[],
    };
    ProductionProviderProbes.contribute(&bare, &mut status);
    assert!(status.provider_probes.is_empty());
}
