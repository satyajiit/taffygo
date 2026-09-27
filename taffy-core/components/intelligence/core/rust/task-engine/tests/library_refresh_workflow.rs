// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The approved Library manifest runs locally and never invents destinations.

#[path = "reviewed_workflow/common.rs"]
mod common;

use bip_types::identity::{ApprovalReceiptReference, DispatchId, MonotonicMillis, TabId};
use bip_types::ActionResultCode;
use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{
    ActionOutcome, Authorization, BrowserSessionId, BudgetKind, Command, ConsentedSource, Effect,
    LibraryRefreshContext, LibraryRefreshSource, Milestone, ProposalDecision, ProviderRouteId,
    ScopePreview, SourceId, SourceScope, TaskBudgets, TaskState, TaskTemplateId, WorkspaceId,
};

const TAB: &str = "library-refresh-tab";

fn source(byte: u8, host: &str, path: &str) -> LibraryRefreshSource {
    LibraryRefreshSource {
        source_id: SourceId::from_bytes([byte; 16]),
        title: format!("Saved source {byte}"),
        host: host.to_owned(),
        canonical_locator: format!("https://{host}/{path}"),
        original_content_digest: [byte; 32],
    }
}

fn started() -> common::Driver {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.provider_route = ProviderRouteId::new("no_model_required").ok();
    seed.snapshot.tool_allowlist =
        vec!["browser.navigate".to_owned(), "browser.dom.read".to_owned()];
    seed.snapshot.milestone = Milestone::M7;
    seed.snapshot.library_refresh = Some(LibraryRefreshContext {
        preview_id: "ab".repeat(32),
        library_revision: 7,
        collection_id: WorkspaceId::from_bytes([9; 16]),
        source_workspace_revision: 11,
        sources: vec![
            source(10, "first.example", "article"),
            source(11, "second.example", "report"),
        ],
    });
    let mut driver = common::Driver::from_seed(seed);
    driver.apply(Command::StartTask(ScopePreview {
        scope: SourceScope::new(),
        sources: Vec::new(),
        source_discovery_enabled: true,
        new_source_cap: 2,
        provider_route: ProviderRouteId::new("no_model_required").ok(),
        budgets: TaskBudgets::none()
            .with(BudgetKind::MaxSources, 2)
            .with(BudgetKind::MaxModelRequests, 0),
    }));
    let consent = driver.apply(Command::AcceptInitialConsent(
        ApprovalReceiptReference::new("refresh-consent"),
    ));
    assert!(matches!(
        consent.effects.as_slice(),
        [Effect::PrepareDiscoveryTab { .. }]
    ));
    driver.apply(Command::RecordDiscoveryTab {
        discovery_tab_id: TabId::new(TAB),
        browser_session_id: BrowserSessionId::new("browser-session-1")
            .unwrap_or_else(|_| unreachable!()),
    });
    driver
}

fn propose(driver: &mut common::Driver) -> bip_types::identity::ActionId {
    let accepted = driver.apply_next();
    let [Effect::AskPolicy { action_id }] = accepted.effects.as_slice() else {
        unreachable!("reviewed action must ask the compiled policy")
    };
    action_id.clone()
}

fn dispatch(
    driver: &mut common::Driver,
    action_id: bip_types::identity::ActionId,
    ordinal: u8,
) -> DispatchId {
    let dispatch = DispatchId::new(format!("refresh-dispatch-{ordinal}"));
    driver.apply(Command::RecordPolicyDecision {
        action_id,
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: task_engine::CapabilityId::new(format!("refresh-capability-{ordinal}")),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    dispatch
}

fn finish_navigation(
    driver: &mut common::Driver,
    action_id: bip_types::identity::ActionId,
    dispatch_id: DispatchId,
    byte: u8,
    address: &str,
) {
    let origin_end = address
        .get("https://".len()..)
        .and_then(|rest| rest.find('/').map(|index| "https://".len() + index))
        .unwrap_or(address.len());
    driver.apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(dispatch_id),
            observed_at: MonotonicMillis(2_000),
            observation: None,
            discovered_source: Some(ConsentedSource {
                source_id: SourceId::from_bytes([byte; 16]),
                tab_id: TabId::new(TAB),
                normalized_origin: address.get(..origin_end).unwrap_or(address).to_owned(),
                canonical_locator: Some(address.to_owned()),
            }),
        }),
    });
}

fn finish_observation(
    driver: &mut common::Driver,
    action_id: bip_types::identity::ActionId,
    dispatch_id: DispatchId,
    origin: &str,
) {
    let mut evidence = common::evidence(task_engine::ObservationCompleteness::Complete);
    evidence.tab_id = TabId::new(TAB);
    origin.clone_into(&mut evidence.normalized_origin);
    driver.apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(dispatch_id),
            observed_at: MonotonicMillis(2_100),
            observation: Some(evidence),
            discovered_source: None,
        }),
    });
}

#[test]
fn exact_manifest_is_navigated_and_observed_without_a_model_turn() {
    let mut driver = started();
    driver.apply_next(); // executor starts
    driver.apply_next(); // deterministic plan

    for (ordinal, (address, origin)) in [
        ("https://first.example/article", "https://first.example"),
        ("https://second.example/report", "https://second.example"),
    ]
    .into_iter()
    .enumerate()
    {
        driver.apply_next(); // navigation step starts
        let navigation = propose(&mut driver);
        let intent = driver
            .reducer
            .actions()
            .find(|action| action.action_id() == &navigation)
            .map_or_else(
                || unreachable!("proposal is resident"),
                |action| action.proposal().intent().clone(),
            );
        assert!(matches!(
            intent,
            ActionIntent::Browser(BrowserIntent::Navigate {
                address: proposed,
                new_tab: false,
                ..
            }) if proposed == address
        ));
        let byte = u8::try_from(20 + ordinal).unwrap_or(u8::MAX);
        let navigation_dispatch = dispatch(&mut driver, navigation.clone(), byte);
        finish_navigation(&mut driver, navigation, navigation_dispatch, byte, address);
        driver.apply_next(); // navigation succeeds
        driver.apply_next(); // observation starts
        let observation = propose(&mut driver);
        let observation_dispatch =
            dispatch(&mut driver, observation.clone(), byte.saturating_add(40));
        finish_observation(&mut driver, observation, observation_dispatch, origin);
        driver.apply_next(); // observation succeeds
    }
    driver.apply_next(); // result candidate
    let terminal = driver.apply_next(); // result validated
    assert_eq!(driver.reducer.task().state(), TaskState::Completed);
    assert!(matches!(
        terminal.effects.as_slice(),
        [Effect::ReleaseTaskTabs]
    ));
    assert_eq!(
        driver
            .reducer
            .task()
            .terminal_result()
            .map(|result| result.source_count),
        Some(2)
    );
}

#[test]
fn an_address_not_equal_to_the_approved_manifest_is_refused() {
    let mut driver = started();
    driver.apply_next();
    driver.apply_next();
    driver.apply_next();
    let navigation = propose(&mut driver);
    let dispatch_id = dispatch(&mut driver, navigation.clone(), 1);
    let refused = driver.reducer.apply(task_engine::CommandEnvelope::new(
        task_engine::IdempotencyKey::new("wrong-address"),
        driver.reducer.task().revision(),
        task_engine::TraceId::new("wrong-address"),
        Command::RecordActionOutcome {
            action_id: navigation,
            outcome: Box::new(ActionOutcome {
                code: ActionResultCode::Verified,
                dispatch_id: Some(dispatch_id),
                observed_at: MonotonicMillis(2_000),
                observation: None,
                discovered_source: Some(ConsentedSource {
                    source_id: SourceId::from_bytes([20; 16]),
                    tab_id: TabId::new(TAB),
                    normalized_origin: "https://first.example".to_owned(),
                    canonical_locator: Some("https://first.example/other".to_owned()),
                }),
            }),
        },
    ));
    assert!(refused.is_err());
}
