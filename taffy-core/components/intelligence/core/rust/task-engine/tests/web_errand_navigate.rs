// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A typed navigate is the second move a zero-source errand may make from its
//! discovery tab (decision 0106 section 2): the address is a lead, and the
//! browser going there is what makes the site a source — admitted, counted and
//! bound exactly as the search's destination is.

mod common;

use bip_types::identity::{ActionId, TabId};
use task_engine::action::BrowserIntent;
use task_engine::transition::RefusalReason;
use task_engine::{BudgetKind, Command, EventKind};

use common::errand::{
    discovered_outcome, dispatch_authorized, prepared_errand, source, DISCOVERY_TAB,
};

fn navigate(tab: &str, address: &str) -> BrowserIntent {
    BrowserIntent::Navigate {
        tab: TabId::new(tab),
        address: address.to_owned(),
        new_tab: false,
    }
}

#[test]
fn a_typed_navigate_from_the_discovery_tab_binds_the_first_source() {
    let mut fixture = prepared_errand(4);
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        navigate(DISCOVERY_TAB, "https://myaadhaar.uidai.gov.in/"),
        0,
    );
    let admitted = source(40, "https://myaadhaar.uidai.gov.in");
    let accepted = fixture
        .apply(Command::RecordActionOutcome {
            action_id: ActionId::new(action),
            outcome: Box::new(discovered_outcome(dispatch, admitted.clone())),
        })
        .unwrap_or_else(|refusal| unreachable!("a verified navigate was refused: {refusal:?}"));
    let kinds: Vec<_> = accepted.events.iter().map(|event| event.kind).collect();
    assert!(kinds.contains(&EventKind::ActionVerificationCompleted));
    assert!(kinds.contains(&EventKind::SourceScopeSet));
    assert!(kinds.contains(&EventKind::BudgetCharged));
    assert_eq!(
        fixture.reducer.task().consented_sources(),
        std::slice::from_ref(&admitted)
    );
    assert_eq!(
        fixture.reducer.task().scope().included(),
        &[admitted.source_id]
    );
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(BudgetKind::MaxSources),
        1
    );
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        3
    );
}

#[test]
fn a_navigate_source_must_come_from_the_prepared_discovery_tab() {
    let mut fixture = prepared_errand(4);
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        navigate(DISCOVERY_TAB, "https://destination.example/start"),
        0,
    );
    let mut elsewhere = source(41, "https://destination.example");
    elsewhere.tab_id = TabId::new("another-task-tab");
    let refused = fixture.apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(dispatch, elsewhere)),
    });
    assert_eq!(
        refused.err().map(|refusal| refusal.reason),
        Some(RefusalReason::ActionOutcomeMismatch)
    );
    assert!(fixture.reducer.task().consented_sources().is_empty());
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        4
    );
}

/// The reducer charges the cap only when a source comes back. A navigate the
/// browser completed with none binds nothing and spends nothing — which is why
/// the browser refuses such a completion rather than reporting it verified.
#[test]
fn a_verified_navigate_with_no_source_binds_nothing_and_spends_nothing() {
    let mut fixture = prepared_errand(2);
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        navigate(DISCOVERY_TAB, "https://destination.example/start"),
        0,
    );
    let mut outcome = discovered_outcome(dispatch, source(42, "https://destination.example"));
    outcome.discovered_source = None;
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(outcome),
    });
    assert!(fixture.reducer.task().consented_sources().is_empty());
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(BudgetKind::MaxSources),
        0
    );
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        2
    );
}
