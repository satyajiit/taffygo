// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Secret containment, budget refusal, and the cost report.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::{ModelRole, PriceBasis};
use model_router::cost::{
    price, BudgetRefusal, CallKind, TaskBudget, TaskLedger, TaskOutcome, TokenUsage, UsageRecord,
};
use model_router::credential::{AuthType, CredentialRef, CredentialState, SecretMaterial};
use model_router::ids::ProviderId;
use model_router::money::{Currency, Micros};
use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
use model_router::route::{
    ModelPolicy, NoLocalEndpoints, RoutePreference, RouteRequest, RouteSelector,
};
use model_router::{TaskId, ThinkingLevel};

const TOKEN: &str = "sk-do-not-log-me-0123456789";

#[test]
fn secret_material_reveals_nothing_by_accident() {
    let secret = SecretMaterial::new(TOKEN.as_bytes().to_vec());
    let rendered = format!("{secret:?}");
    assert!(!rendered.contains(TOKEN), "Debug printed the secret");
    assert_eq!(rendered, "SecretMaterial(redacted)");
    assert_eq!(secret.len(), TOKEN.len());
    // The only way out is one conspicuously named method.
    assert_eq!(secret.expose_for_request(), TOKEN.as_bytes());
}

#[test]
fn a_credential_reference_carries_no_material_anywhere_it_is_written() {
    let reference = CredentialRef {
        provider_id: ProviderId::new("effort-vendor").expect("key"),
        auth_type: AuthType::ApiKey,
        state: CredentialState::Usable,
        subscription_backed: false,
        created_at: None,
        rotated_at: None,
    };
    let json = serde_json::to_string(&reference).expect("serializes");
    assert!(!json.contains(TOKEN));
    assert!(!format!("{reference:?}").contains(TOKEN));

    // The same has to hold of everything the reference is embedded in: a plan,
    // a disclosure, and anything derived from them are all things that get
    // logged, audited, or rendered.
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
            &RouteRequest {
                task_id: TaskId::from_bytes([2; 16]),
                role: ModelRole::PrimaryReasoning,
                preference: RoutePreference::ByoDirect,
                purpose: RequestPurpose::Planning,
                context: ContextManifest {
                    source_ids: Vec::new(),
                    classes: vec![DataSensitivity::Personal],
                    item_count: 1,
                    estimated_input_tokens: 100,
                },
                required_modalities: Vec::new(),
                requires_tool_calling: true,
                thinking: ThinkingLevel::Low,
                answer_tokens: 500,
                estimated_output_tokens: 100,
                pinned_model: None,
            },
            &common::open_ledger(),
        )
        .expect("routes");

    let plan_json = serde_json::to_string(&plan).expect("serializes");
    assert!(!plan_json.contains(TOKEN));
    assert!(!format!("{plan:?}").contains(TOKEN));
    let disclosure_json = serde_json::to_string(&plan.primary.disclosure).expect("serializes");
    assert!(!disclosure_json.contains(TOKEN));
    // What it does carry is the header name the adapter will fill.
    assert!(plan_json.contains("authorization"));
}

fn usd() -> Currency {
    Currency::new("USD").expect("currency")
}

fn budget(max: u64) -> TaskBudget {
    TaskBudget {
        currency: usd(),
        max_micros: Micros::new(max),
        max_calls: 8,
        max_retries: 2,
    }
}

fn snapshot_of(model: &str) -> model_router::catalog::PriceSnapshot {
    common::fixture_document()
        .models
        .into_iter()
        .find(|entry| entry.model_id.as_str() == model)
        .expect("fixture model")
        .cost
}

#[test]
fn pricing_rounds_up_and_selects_the_long_context_tier() {
    let snapshot = snapshot_of("reasoner-one");
    let small = price(
        &snapshot,
        TokenUsage {
            input: 1_000,
            output: 0,
            cache_read: 0,
            cache_write: 0,
        },
    )
    .expect("prices");
    assert_eq!(small.micros, Micros::new(3_000));

    // One token is charged as a whole micro-unit rather than dropped.
    let sliver = price(
        &snapshot,
        TokenUsage {
            input: 1,
            output: 0,
            cache_read: 0,
            cache_write: 0,
        },
    )
    .expect("prices");
    assert_eq!(sliver.micros, Micros::new(3));

    // Past the tier threshold the tier rate applies to the whole request.
    let large = price(
        &snapshot,
        TokenUsage {
            input: 200_000,
            output: 0,
            cache_read: 0,
            cache_write: 0,
        },
    )
    .expect("prices");
    assert_eq!(large.micros, Micros::new(1_200_000));
}

#[test]
fn a_budget_refuses_the_call_that_would_pass_it() {
    let snapshot = snapshot_of("reasoner-one");
    let mut ledger = TaskLedger::new(TaskId::from_bytes([3; 16]), budget(10_000));
    let usage = TokenUsage {
        input: 1_000,
        output: 0,
        cache_read: 0,
        cache_write: 0,
    };
    let estimate = ledger.estimate(&snapshot, usage).expect("prices");

    for _ in 0..3 {
        ledger.admit(CallKind::Initial, estimate).expect("fits");
        ledger.record(&UsageRecord {
            model: common::key("effort-vendor", "reasoner-one"),
            role: ModelRole::PrimaryReasoning,
            kind: CallKind::Initial,
            tokens: usage,
            cost: estimate,
        });
    }
    assert_eq!(ledger.committed(), Micros::new(9_000));

    // The fourth call would cost 3,000 against 1,000 remaining.
    let refusal = ledger
        .admit(CallKind::Initial, estimate)
        .expect_err("the budget refuses");
    assert_eq!(
        refusal,
        BudgetRefusal::CostExceeded {
            budget: Micros::new(10_000),
            committed: Micros::new(9_000),
            requested: Micros::new(3_000),
        }
    );
}

#[test]
fn retries_and_failovers_spend_the_same_budget_as_the_call_they_repeat() {
    let snapshot = snapshot_of("reasoner-one");
    let mut ledger = TaskLedger::new(TaskId::from_bytes([4; 16]), budget(10_000));
    let usage = TokenUsage {
        input: 1_000,
        output: 0,
        cache_read: 0,
        cache_write: 0,
    };
    let estimate = ledger.estimate(&snapshot, usage).expect("prices");
    for kind in [CallKind::Initial, CallKind::Retry, CallKind::Failover] {
        ledger.record(&UsageRecord {
            model: common::key("effort-vendor", "reasoner-one"),
            role: ModelRole::PrimaryReasoning,
            kind,
            tokens: usage,
            cost: estimate,
        });
    }
    let report = ledger.report();
    assert_eq!(report.actual, Micros::new(9_000));
    assert_eq!(report.calls, 3);
    assert_eq!(report.retries, 1);
    assert_eq!(report.failovers, 1);
    // One role, one line: nothing was accounted somewhere else.
    assert_eq!(report.per_role.len(), 1);
}

#[test]
fn the_retry_limit_refuses_before_the_cost_limit_does() {
    let snapshot = snapshot_of("skimmer-one");
    let mut ledger = TaskLedger::new(TaskId::from_bytes([5; 16]), budget(10_000_000));
    let usage = TokenUsage {
        input: 10,
        output: 0,
        cache_read: 0,
        cache_write: 0,
    };
    let estimate = ledger.estimate(&snapshot, usage).expect("prices");
    for _ in 0..2 {
        ledger.record(&UsageRecord {
            model: common::key("effort-vendor", "skimmer-one"),
            role: ModelRole::FastBrowsing,
            kind: CallKind::Retry,
            tokens: usage,
            cost: estimate,
        });
    }
    assert_eq!(
        ledger.admit(CallKind::Retry, estimate),
        Err(BudgetRefusal::RetriesExhausted { limit: 2 })
    );
    // An initial call is still allowed: the retry limit is about retries.
    assert!(ledger.admit(CallKind::Initial, estimate).is_ok());
}

#[test]
fn a_price_in_another_currency_is_refused_rather_than_converted() {
    let mut snapshot = snapshot_of("reasoner-one");
    snapshot.currency = Currency::new("EUR").expect("currency");
    let ledger = TaskLedger::new(TaskId::from_bytes([6; 16]), budget(10_000));
    assert!(matches!(
        ledger.estimate(&snapshot, TokenUsage::default()),
        Err(BudgetRefusal::CurrencyMismatch { .. })
    ));
}

#[test]
fn imputed_spend_is_reported_as_imputed() {
    let snapshot = snapshot_of("reasoner-two");
    assert_eq!(snapshot.basis, PriceBasis::Implied);
    let mut ledger = TaskLedger::new(TaskId::from_bytes([8; 16]), budget(10_000_000));
    let usage = TokenUsage {
        input: 1_000,
        output: 100,
        cache_read: 0,
        cache_write: 0,
    };
    let estimate = ledger.estimate(&snapshot, usage).expect("prices");
    ledger.record(&UsageRecord {
        model: common::key("budget-vendor", "reasoner-two"),
        role: ModelRole::PrimaryReasoning,
        kind: CallKind::Initial,
        tokens: usage,
        cost: estimate,
    });
    ledger.finish(TaskOutcome::VerifiedComplete);
    let report = ledger.report();
    assert_eq!(report.outcome, TaskOutcome::VerifiedComplete);
    assert_eq!(report.imputed, report.actual);
    assert!(!report.over_budget);
    // The report is content-free, so it is safe to write next to a benchmark
    // result.
    let json = serde_json::to_string(&report).expect("serializes");
    assert!(!json.contains(TOKEN));
}

#[test]
fn spend_past_the_budget_is_recorded_and_then_refuses_everything_after() {
    let snapshot = snapshot_of("reasoner-one");
    let mut ledger = TaskLedger::new(TaskId::from_bytes([10; 16]), budget(1_000));
    let usage = TokenUsage {
        input: 1_000,
        output: 0,
        cache_read: 0,
        cache_write: 0,
    };
    let estimate = ledger.estimate(&snapshot, usage).expect("prices");
    assert!(ledger.admit(CallKind::Initial, estimate).is_err());
    // A call that happened anyway is still recorded: accounting does not get to
    // pretend spend did not occur.
    ledger.record(&UsageRecord {
        model: common::key("effort-vendor", "reasoner-one"),
        role: ModelRole::PrimaryReasoning,
        kind: CallKind::Initial,
        tokens: usage,
        cost: estimate,
    });
    assert!(ledger.is_over_budget());
    assert_eq!(ledger.remaining(), Micros::ZERO);
    assert!(ledger.admit(CallKind::Initial, estimate).is_err());
}
