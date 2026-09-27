// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Route eligibility, disclosure, failover order, and every refusal.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing,
    clippy::match_same_arms
)]

mod common;

use model_router::catalog::{InputModality, ModelRole};
use model_router::cost::CallKind;
use model_router::credential::{AuthType, CredentialState};
use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
use model_router::route::{
    DisclosureClass, ManagedEntitlement, ModelPolicy, NoLocalEndpoints, Recipient, Route,
    RoutePreference, RouteRefusal, RouteRequest, RouteSelector,
};
use model_router::{ModelKey, TaskId, ThinkingLevel};

fn request(role: ModelRole, preference: RoutePreference) -> RouteRequest {
    RouteRequest {
        task_id: TaskId::from_bytes([1; 16]),
        role,
        preference,
        purpose: RequestPurpose::Planning,
        context: ContextManifest {
            source_ids: Vec::new(),
            classes: vec![DataSensitivity::Public],
            item_count: 3,
            estimated_input_tokens: 1_000,
        },
        required_modalities: vec![InputModality::Text],
        requires_tool_calling: true,
        thinking: ThinkingLevel::Medium,
        answer_tokens: 2_000,
        estimated_output_tokens: 500,
        pinned_model: None,
    }
}

/// The same request with no tool vocabulary.
///
/// Kept beside the tool-carrying one so a managed case can be about what it is
/// about: the two differ in one field, and a test that reads the same on both
/// is saying the tools made no difference to it.
fn prose_request(role: ModelRole, preference: RoutePreference) -> RouteRequest {
    RouteRequest {
        requires_tool_calling: false,
        ..request(role, preference)
    }
}

#[test]
fn the_direct_route_attaches_a_reference_and_names_only_the_provider() {
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let mut policy = ModelPolicy::new();
    policy.set(
        ModelRole::PrimaryReasoning,
        vec![common::key("effort-vendor", "reasoner-one")],
    );
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect("a configured provider routes");

    assert_eq!(plan.route, Route::ByoDirect);
    assert_eq!(plan.disclosure_class(), DisclosureClass::ApiKeyDirect);
    assert_eq!(
        plan.primary.disclosure.egress.recipients,
        vec![Recipient::Provider {
            display_name: "Effort Vendor".to_owned(),
            host: "api.effort-vendor.example".to_owned(),
        }]
    );
    let auth = plan
        .primary
        .auth
        .as_ref()
        .expect("direct routes carry auth");
    assert_eq!(auth.secret_header_names, vec!["authorization".to_owned()]);
    assert_eq!(auth.credential.state, CredentialState::Usable);
}

#[test]
fn a_plan_of_subscription_access_says_so() {
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("budget-vendor", AuthType::Oauth, true);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect("a signed-in plan routes");
    assert_eq!(plan.disclosure_class(), DisclosureClass::SubscriptionDirect);
    assert_eq!(
        plan.primary.disclosure.price_basis,
        model_router::catalog::PriceBasis::Implied
    );
}

#[test]
fn the_managed_route_names_every_hop_and_attaches_no_credential() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty();
    let entitlement = common::entitlement(&[common::key("effort-vendor", "reasoner-one")]);
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(
            &prose_request(ModelRole::PrimaryReasoning, RoutePreference::Managed),
            &common::open_ledger(),
        )
        .expect("an entitled model routes");

    assert_eq!(plan.route, Route::Managed);
    assert_eq!(plan.disclosure_class(), DisclosureClass::Managed);
    assert!(plan.primary.auth.is_none());
    assert_eq!(plan.primary.disclosure.egress.recipients.len(), 3);
    assert!(matches!(
        plan.primary.disclosure.egress.recipients.first(),
        Some(Recipient::EdgeWorker { .. })
    ));
    assert!(matches!(
        plan.primary.disclosure.egress.recipients.get(1),
        Some(Recipient::Gateway { .. })
    ));
}

/// Every combination of what the user chose and what the device has.
///
/// The row that matters most is the one that is missing: there is no
/// combination in which a direct request produces a managed plan.
#[test]
fn the_route_matrix_holds() {
    let catalog = common::fixture_catalog();
    let policy = ModelPolicy::new();
    let entitled = common::entitlement(&[common::key("effort-vendor", "reasoner-one")]);
    let unentitled = common::no_entitlement();

    let configured = common::FixedCredentials::empty()
        .usable("effort-vendor", AuthType::ApiKey, false)
        .usable("budget-vendor", AuthType::ApiKey, false);
    let unconfigured = common::FixedCredentials::empty();
    let broken = common::FixedCredentials::empty()
        .broken(
            "effort-vendor",
            AuthType::Oauth,
            CredentialState::RefreshFailed,
        )
        .broken(
            "budget-vendor",
            AuthType::Oauth,
            CredentialState::NeedsSignIn,
        );

    let cases: Vec<(
        &str,
        RoutePreference,
        &dyn model_router::credential::CredentialDirectory,
        &ManagedEntitlement,
    )> = vec![
        (
            "direct, configured",
            RoutePreference::ByoDirect,
            &configured,
            &unentitled,
        ),
        (
            "direct, nothing stored",
            RoutePreference::ByoDirect,
            &unconfigured,
            &entitled,
        ),
        (
            "direct, needs attention",
            RoutePreference::ByoDirect,
            &broken,
            &entitled,
        ),
        (
            "managed, entitled",
            RoutePreference::Managed,
            &unconfigured,
            &entitled,
        ),
        (
            "managed, no entitlement",
            RoutePreference::Managed,
            &configured,
            &unentitled,
        ),
    ];

    for (name, preference, credentials, entitlement) in cases {
        let selector = RouteSelector::new(
            &catalog,
            credentials,
            entitlement,
            &policy,
            &NoLocalEndpoints,
        );
        // Prose on both routes: the matrix is about credentials and
        // entitlement, and a tool vocabulary would fail the managed rows for
        // the wire's own reason instead.
        let outcome = selector.select(
            &prose_request(ModelRole::PrimaryReasoning, preference),
            &common::open_ledger(),
        );
        match (name, outcome) {
            ("direct, configured", Ok(plan)) => {
                assert_eq!(plan.route, Route::ByoDirect);
            }
            ("direct, nothing stored", Err(RouteRefusal::CredentialMissing { .. })) => {}
            ("direct, needs attention", Err(RouteRefusal::CredentialNeedsAttention { .. })) => {}
            ("managed, entitled", Ok(plan)) => {
                assert_eq!(plan.route, Route::Managed);
            }
            ("managed, no entitlement", Err(RouteRefusal::ManagedUnavailable)) => {}
            (name, other) => panic!("{name} produced {other:?}"),
        }
    }
}

#[test]
fn a_failed_credential_never_becomes_a_managed_request() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty()
        .broken(
            "effort-vendor",
            AuthType::ApiKey,
            CredentialState::RefreshFailed,
        )
        .broken(
            "budget-vendor",
            AuthType::ApiKey,
            CredentialState::RefreshFailed,
        );
    // Managed access is fully available and would work. It is still not used.
    let entitlement = common::entitlement(&[
        common::key("effort-vendor", "reasoner-one"),
        common::key("budget-vendor", "reasoner-two"),
    ]);
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let refusal = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect_err("a broken credential refuses");
    assert!(matches!(
        refusal,
        RouteRefusal::CredentialNeedsAttention { .. }
    ));
}

#[test]
fn failover_order_is_the_same_every_time_and_follows_configuration_first() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty()
        .usable("effort-vendor", AuthType::ApiKey, false)
        .usable("budget-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let mut policy = ModelPolicy::new();
    policy.set(
        ModelRole::PrimaryReasoning,
        vec![common::key("effort-vendor", "reasoner-one")],
    );
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );

    let order = |plan: &model_router::RoutePlan| -> Vec<ModelKey> {
        let mut keys = vec![plan.primary.model.clone()];
        keys.extend(plan.failover.iter().map(|c| c.model.clone()));
        keys
    };

    let first = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect("routes");
    let second = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect("routes");
    assert_eq!(order(&first), order(&second));
    // Configuration first, then catalog key order — not key order alone, which
    // would have put the budget vendor ahead.
    assert_eq!(
        order(&first),
        vec![
            common::key("effort-vendor", "reasoner-one"),
            common::key("budget-vendor", "reasoner-two"),
        ]
    );
    assert!(!first.crosses_disclosure_boundary());
}

#[test]
fn failover_never_leaves_the_disclosure_class_it_started_in() {
    let catalog = common::fixture_catalog();
    // One provider is signed in on a plan, the other is on a metered key. The
    // two are different disclosure classes, so one can never substitute for the
    // other however well it would serve the role.
    let credentials = common::FixedCredentials::empty()
        .usable("budget-vendor", AuthType::Oauth, true)
        .usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect("routes");
    assert_eq!(plan.disclosure_class(), DisclosureClass::SubscriptionDirect);
    assert!(plan.failover.is_empty());
    assert!(!plan.crosses_disclosure_boundary());
}

#[test]
fn a_pinned_model_is_used_alone() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty()
        .usable("effort-vendor", AuthType::ApiKey, false)
        .usable("budget-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let mut pinned = request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect);
    pinned.pinned_model = Some(common::key("budget-vendor", "reasoner-two"));
    let plan = selector
        .select(&pinned, &common::open_ledger())
        .expect("routes");
    assert_eq!(
        plan.primary.model,
        common::key("budget-vendor", "reasoner-two")
    );
    assert!(plan.failover.is_empty());
}

#[test]
fn a_pinned_model_that_does_not_exist_fails_closed() {
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let mut pinned = request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect);
    pinned.pinned_model = Some(common::key("ghost-vendor", "ghost-model"));
    assert!(matches!(
        selector.select(&pinned, &common::open_ledger()),
        Err(RouteRefusal::Lookup(_))
    ));
}

#[test]
fn prohibited_material_is_refused_before_anything_else() {
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::entitlement(&[common::key("effort-vendor", "reasoner-one")]);
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    for preference in [RoutePreference::ByoDirect, RoutePreference::Managed] {
        let mut prohibited = request(ModelRole::PrimaryReasoning, preference);
        prohibited.context.classes.push(DataSensitivity::Prohibited);
        assert_eq!(
            selector.select(&prohibited, &common::open_ledger()),
            Err(RouteRefusal::ProhibitedMaterial)
        );
    }
}

#[test]
fn a_request_that_needs_images_only_reaches_a_model_that_takes_them() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty()
        .usable("effort-vendor", AuthType::ApiKey, false)
        .usable("budget-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let mut visual = request(ModelRole::Vision, RoutePreference::ByoDirect);
    visual.required_modalities = vec![InputModality::Text, InputModality::Image];
    let plan = selector
        .select(&visual, &common::open_ledger())
        .expect("routes");
    assert_eq!(
        plan.primary.model,
        common::key("budget-vendor", "reasoner-two")
    );
    assert!(plan.failover.is_empty());
}

#[test]
fn a_used_up_entitlement_refuses_instead_of_calling() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty();
    let mut entitlement = common::entitlement(&[common::key("effort-vendor", "reasoner-one")]);
    entitlement.remaining_calls = Some(0);
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    assert!(matches!(
        selector.select(
            &prose_request(ModelRole::PrimaryReasoning, RoutePreference::Managed),
            &common::open_ledger()
        ),
        Err(RouteRefusal::QuotaExhausted { .. })
    ));
}

/// The managed wire carries tools from schema version 2 (decision 0092), so a
/// tool-carrying call is served on either route and the two answers are the
/// same model on the same terms. The refusal that stood here is not deleted —
/// it is read off the wire's own version predicate, and what it does below
/// that version is asserted beside the site that raises it, in `route/mod.rs`.
#[test]
fn a_tool_carrying_call_is_served_on_either_route() {
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::entitlement(&[common::key("effort-vendor", "reasoner-one")]);
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let managed = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::Managed),
            &common::open_ledger(),
        )
        .expect("a tool-carrying call routes managed");
    assert_eq!(managed.route, Route::Managed);
    assert!(managed.primary.tool_calling);

    let direct = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::open_ledger(),
        )
        .expect("the same tools route directly");
    assert_eq!(direct.route, Route::ByoDirect);
    assert_eq!(managed.primary.model, direct.primary.model);

    // The model's own limit is untouched, and it still names the model: a
    // descriptor that cannot call tools refuses on either route, which is the
    // refusal `ManagedToolCallingUnsupported` was never about.
    let prose = selector
        .select(
            &prose_request(ModelRole::PrimaryReasoning, RoutePreference::Managed),
            &common::open_ledger(),
        )
        .expect("a prose call routes managed");
    assert_eq!(prose.route, Route::Managed);
}

#[test]
fn a_task_budget_that_cannot_cover_the_call_refuses_it() {
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let refusal = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &common::tight_ledger(),
        )
        .expect_err("a spent budget refuses");
    assert!(matches!(refusal, RouteRefusal::Budget(_)));
}

#[test]
fn an_exhausted_call_budget_refuses_before_a_retry_is_planned() {
    let mut ledger = common::open_ledger();
    let catalog = common::fixture_catalog();
    let credentials =
        common::FixedCredentials::empty().usable("effort-vendor", AuthType::ApiKey, false);
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &ledger,
        )
        .expect("routes");
    for _ in 0..32 {
        ledger.record(&model_router::cost::UsageRecord {
            model: plan.primary.model.clone(),
            role: ModelRole::PrimaryReasoning,
            kind: CallKind::Initial,
            tokens: model_router::TokenUsage::default(),
            cost: plan.estimate,
        });
    }
    assert!(matches!(
        selector.select(
            &request(ModelRole::PrimaryReasoning, RoutePreference::ByoDirect),
            &ledger
        ),
        Err(RouteRefusal::Budget(_))
    ));
}
