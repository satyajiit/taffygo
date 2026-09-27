// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Discovery starts and their exact pre-model source boundary.

mod common;

use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{
    BudgetKind, Command, PreModelObservation, ProviderRouteId, SourceScope, TaskBudgets,
    TaskTemplateId, REVIEWED_OBSERVATION_TOOL,
};

use common::agent::Digest;

fn running_errand_task(with_initial_source: bool) -> common::Fixture {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![REVIEWED_OBSERVATION_TOOL.to_owned()];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = 4;
    preview.budgets = TaskBudgets::none().with(
        BudgetKind::MaxSources,
        if with_initial_source { 5 } else { 4 },
    );
    if !with_initial_source {
        preview.scope = SourceScope::new();
        preview.sources.clear();
    }
    fixture.must_apply(Command::StartTask(preview));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

#[test]
fn accepted_initial_sources_are_charged_and_seed_the_durable_authority_register() {
    let fixture = running_errand_task(true);
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(BudgetKind::MaxSources),
        1
    );
    assert_eq!(fixture.reducer.task().consented_sources().len(), 1);
    assert_eq!(
        fixture.reducer.task().consented_sources(),
        fixture.reducer.task().snapshot().consented_sources
    );
}

#[test]
fn an_initial_source_that_exceeds_its_source_budget_is_never_frozen() {
    let mut fixture = common::draft();
    let mut preview = common::preview();
    preview.budgets = TaskBudgets::none().with(BudgetKind::MaxSources, 0);
    let envelope = fixture.envelope(Command::StartTask(preview));
    let Err(refusal) = fixture.reducer.apply(envelope) else {
        unreachable!("the initial source must fit the accepted source budget")
    };
    assert_eq!(
        refusal.reason,
        task_engine::transition::RefusalReason::BudgetExhausted
    );
    assert!(fixture.reducer.task().consented_sources().is_empty());
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(BudgetKind::MaxSources),
        0
    );
}

#[test]
fn zero_source_errand_waits_for_the_exact_discovery_tab_before_the_first_turn() {
    let mut fixture = running_errand_task(false);
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("zero-source discovery is valid: {error:?}")),
        PreModelObservation::Waiting
    );
    fixture.must_apply(Command::RecordDiscoveryTab {
        discovery_tab_id: task_engine::deps::TabId::new("discovery-tab-1"),
        browser_session_id: task_engine::BrowserSessionId::new("browser_session_1")
            .unwrap_or_else(|_| unreachable!()),
    });
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("zero-source discovery is valid: {error:?}")),
        PreModelObservation::Ready
    );
}

#[test]
fn errand_with_an_initial_source_observes_that_exact_source_first() {
    let fixture = running_errand_task(true);
    let PreModelObservation::Command(Command::ProposeAction(proposal)) = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("one-source discovery is valid: {error:?}"))
    else {
        unreachable!("the accepted initial source must be observed")
    };
    assert_eq!(
        proposal.intent(),
        &ActionIntent::Browser(BrowserIntent::DomRead {
            tab: task_engine::deps::TabId::new("tab_1"),
            target: None,
        })
    );
}

#[test]
fn errand_without_the_accepted_discovery_shape_never_opens_a_model_turn() {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![REVIEWED_OBSERVATION_TOOL.to_owned()];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    preview.scope = SourceScope::new();
    preview.sources.clear();
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.source_discovery_enabled = false;
    let result = fixture.apply(Command::StartTask(preview));
    assert!(result.is_err());
}

#[test]
fn discovery_never_widens_a_selected_page_template() {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::BuildSourceTable;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![REVIEWED_OBSERVATION_TOOL.to_owned()];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = 1;
    preview.budgets = TaskBudgets::none().with(BudgetKind::MaxSources, 2);
    assert!(fixture.apply(Command::StartTask(preview)).is_err());
}
