// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A zero-source Web errand driven to its prepared discovery tab, shared by the
//! discovery suites: the search the tab was opened for and the typed navigate
//! that may take its place both start from here.

use bip_types::identity::{DispatchId, MonotonicMillis, TabId};
use bip_types::ActionResultCode;
use task_engine::action::{ActionOutcome, BrowserIntent};
use task_engine::{
    Authorization, BrowserSessionId, BudgetKind, CapabilityId, Command, ConsentedSource, Milestone,
    ProposalDecision, ProviderRouteId, SourceScope, TaskBudgets, TaskTemplateId,
};

pub const DISCOVERY_TAB: &str = "discovery-tab-1";
pub const BROWSER_SESSION: &str = "browser_session_1";

pub fn errand_seed() -> task_engine::TaskSeed {
    let mut seed = super::seed();
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![
        "browser.search".to_owned(),
        "browser.navigate".to_owned(),
        "browser.dom.read".to_owned(),
        "browser.tabs.open".to_owned(),
        "browser.form.submit".to_owned(),
        "browser.download.start".to_owned(),
    ];
    seed.snapshot.milestone = Milestone::M5;
    seed
}

pub fn started_errand(cap: u32) -> super::Fixture {
    started_errand_from(errand_seed(), cap)
}

fn started_errand_from(seed: task_engine::TaskSeed, cap: u32) -> super::Fixture {
    let mut fixture = super::draft_from(seed);
    let mut preview = super::preview();
    preview.scope = SourceScope::new();
    preview.sources.clear();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = cap;
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.budgets = TaskBudgets::none()
        .with(BudgetKind::MaxSources, u64::from(cap))
        .with(BudgetKind::MaxModelRequests, 64);
    fixture.must_apply(Command::StartTask(preview));
    fixture
}

pub fn prepared_errand(cap: u32) -> super::Fixture {
    prepare(started_errand(cap))
}

/// An errand started from a page, with discovery granted beside it, whose
/// allowlist also admits `tools`.
///
/// The page is `common::preview`'s one consented source, on `tab_1`, which is
/// also the tab `common::agent::page` renders from: the page the person was
/// on when they asked.
pub fn errand_from_a_page(cap: u32, tools: &[&str]) -> super::Fixture {
    let mut seed = errand_seed();
    seed.snapshot
        .tool_allowlist
        .extend(tools.iter().map(|tool| (*tool).to_owned()));
    let mut fixture = super::draft_from(seed);
    let mut preview = super::preview();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = cap;
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.budgets = TaskBudgets::none()
        .with(BudgetKind::MaxSources, 1 + u64::from(cap))
        .with(BudgetKind::MaxModelRequests, 64);
    fixture.must_apply(Command::StartTask(preview));
    fixture
}

/// [`errand_from_a_page`] carried through consent to a running task that
/// knows its blank tab.
pub fn running_errand_from_a_page(cap: u32, tools: &[&str]) -> super::Fixture {
    prepare(errand_from_a_page(cap, tools))
}

/// A prepared errand whose allowlist also admits `tool`, for the moves the
/// shared errand seed does not name.
pub fn prepared_errand_admitting(cap: u32, tool: &str) -> super::Fixture {
    let mut seed = errand_seed();
    seed.snapshot.tool_allowlist.push(tool.to_owned());
    prepare(started_errand_from(seed, cap))
}

/// [`prepared_errand_admitting`], for several tools at once.
pub fn prepared_errand_admitting_each(cap: u32, tools: &[&str]) -> super::Fixture {
    let mut seed = errand_seed();
    seed.snapshot
        .tool_allowlist
        .extend(tools.iter().map(|tool| (*tool).to_owned()));
    prepare(started_errand_from(seed, cap))
}

fn prepare(mut fixture: super::Fixture) -> super::Fixture {
    fixture.must_apply(Command::AcceptInitialConsent(super::receipt()));
    fixture.must_apply(Command::RecordDiscoveryTab {
        discovery_tab_id: TabId::new(DISCOVERY_TAB),
        browser_session_id: BrowserSessionId::new(BROWSER_SESSION)
            .unwrap_or_else(|_| unreachable!()),
    });
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

/// Proposes `intent` as turn `ordinal`'s only call, authorizes it and begins
/// its dispatch, answering the action and dispatch identities the outcome must
/// name.
pub fn dispatch_authorized(
    fixture: &mut super::Fixture,
    intent: BrowserIntent,
    ordinal: u64,
) -> (String, DispatchId) {
    let key = format!("turn-{ordinal}-call-0");
    let proposal = super::proposal_for(intent, &key);
    fixture.must_apply(Command::ProposeAction(Box::new(proposal)));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map_or_else(
            || unreachable!("the proposal must mint one action"),
            |action| action.action_id().clone(),
        );
    let dispatch = DispatchId::new(format!("dispatch-{ordinal}"));
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new(format!("capability-{ordinal}")),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    (action_id.as_str().to_owned(), dispatch)
}

pub fn source(seed: u8, origin: &str) -> ConsentedSource {
    ConsentedSource {
        source_id: super::source_id(seed),
        tab_id: TabId::new(DISCOVERY_TAB),
        normalized_origin: origin.to_owned(),
        canonical_locator: None,
    }
}

pub fn discovered_outcome(dispatch: DispatchId, source: ConsentedSource) -> ActionOutcome {
    ActionOutcome {
        code: ActionResultCode::Verified,
        dispatch_id: Some(dispatch),
        observed_at: MonotonicMillis(2_000),
        observation: None,
        discovered_source: Some(source),
    }
}
