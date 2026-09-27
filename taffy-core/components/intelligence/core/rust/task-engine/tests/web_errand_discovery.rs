// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Atomic zero-source bootstrap and discovered-source admission proofs.

mod common;

use bip_types::identity::{DispatchId, TabId};
use task_engine::action::{BrowserIntent, OpaqueOperandKind, OpaqueOperandRef};
use task_engine::{
    BrowserSessionId, BudgetKind, Command, ConsentedSource, Effect, EventKind, EventSubject,
    Reducer,
};

use common::errand::{
    discovered_outcome, dispatch_authorized, errand_seed, prepared_errand, source, started_errand,
    BROWSER_SESSION, DISCOVERY_TAB,
};

fn dispatch_search(fixture: &mut common::Fixture, ordinal: u64) -> (String, DispatchId) {
    dispatch_authorized(
        fixture,
        BrowserIntent::Search {
            tab: TabId::new(DISCOVERY_TAB),
            query: OpaqueOperandRef::for_call(
                ordinal,
                0,
                OpaqueOperandKind::SearchQuery,
                [u8::try_from(ordinal).unwrap_or(u8::MAX); 32],
            ),
        },
        ordinal,
    )
}

#[test]
fn zero_source_consent_commits_only_a_bounded_tab_bootstrap() {
    let mut fixture = started_errand(4);
    let accepted = fixture
        .apply(Command::AcceptInitialConsent(common::receipt()))
        .unwrap_or_else(|refusal| unreachable!("valid consent was refused: {refusal:?}"));
    assert_eq!(
        accepted.effects,
        vec![Effect::PrepareDiscoveryTab {
            browser_session_id: BrowserSessionId::new(BROWSER_SESSION)
                .unwrap_or_else(|_| unreachable!()),
            remaining_new_source_cap: 4,
        }]
    );
    assert!(fixture.reducer.task().scope().included().is_empty());
    assert!(fixture.reducer.task().consented_sources().is_empty());
    assert!(fixture.reducer.task().snapshot().discovery_tab_id.is_none());
}

#[test]
fn the_bootstrap_terminal_requires_the_frozen_session_and_is_not_a_source() {
    let mut fixture = started_errand(3);
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    let wrong_session = fixture.apply(Command::RecordDiscoveryTab {
        discovery_tab_id: TabId::new(DISCOVERY_TAB),
        browser_session_id: BrowserSessionId::new("another-session")
            .unwrap_or_else(|_| unreachable!()),
    });
    assert_eq!(
        wrong_session.err().map(|refusal| refusal.reason),
        Some(task_engine::transition::RefusalReason::DiscoveryAuthorityMismatch)
    );
    let accepted = fixture
        .apply(Command::RecordDiscoveryTab {
            discovery_tab_id: TabId::new(DISCOVERY_TAB),
            browser_session_id: BrowserSessionId::new(BROWSER_SESSION)
                .unwrap_or_else(|_| unreachable!()),
        })
        .unwrap_or_else(|refusal| unreachable!("exact terminal was refused: {refusal:?}"));
    assert!(accepted.events.iter().any(|event| {
        event.kind == EventKind::DiscoveryTabPrepared
            && event.subject == Some(EventSubject::DiscoveryTab(TabId::new(DISCOVERY_TAB)))
    }));
    assert!(accepted.effects.is_empty());
    assert!(fixture.reducer.task().scope().included().is_empty());
    assert!(fixture.reducer.task().consented_sources().is_empty());
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        3
    );
}

#[test]
fn a_verified_discovery_terminal_binds_source_and_budget_atomically() {
    let mut fixture = prepared_errand(4);
    let (action, dispatch) = dispatch_search(&mut fixture, 0);
    let admitted = source(20, "https://search.example");
    let accepted = fixture
        .apply(Command::RecordActionOutcome {
            action_id: bip_types::identity::ActionId::new(action),
            outcome: Box::new(discovered_outcome(dispatch, admitted.clone())),
        })
        .unwrap_or_else(|refusal| unreachable!("verified source was refused: {refusal:?}"));
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
fn the_first_source_must_be_the_exact_prepared_search_tab() {
    let mut fixture = prepared_errand(4);
    let (action, dispatch) = dispatch_search(&mut fixture, 0);
    let mut wrong_tab = source(27, "https://search.example");
    wrong_tab.tab_id = TabId::new("different-task-tab");
    let refused = fixture.apply(Command::RecordActionOutcome {
        action_id: bip_types::identity::ActionId::new(action),
        outcome: Box::new(discovered_outcome(dispatch, wrong_tab)),
    });
    assert_eq!(
        refused.err().map(|refusal| refusal.reason),
        Some(task_engine::transition::RefusalReason::ActionOutcomeMismatch)
    );
    assert!(fixture.reducer.task().consented_sources().is_empty());
    assert!(fixture.reducer.task().scope().included().is_empty());
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
        4
    );
}

#[test]
fn an_exact_already_bound_source_spends_nothing_twice() {
    let mut fixture = prepared_errand(3);
    let admitted = source(21, "https://search.example");
    let (first_action, first_dispatch) = dispatch_search(&mut fixture, 0);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: bip_types::identity::ActionId::new(first_action),
        outcome: Box::new(discovered_outcome(first_dispatch, admitted.clone())),
    });
    let (second_action, second_dispatch) = dispatch_search(&mut fixture, 1);
    let accepted = fixture
        .apply(Command::RecordActionOutcome {
            action_id: bip_types::identity::ActionId::new(second_action),
            outcome: Box::new(discovered_outcome(second_dispatch, admitted.clone())),
        })
        .unwrap_or_else(|refusal| unreachable!("exact replay was refused: {refusal:?}"));
    assert!(!accepted.events.iter().any(|event| matches!(
        event.kind,
        EventKind::SourceScopeSet | EventKind::BudgetCharged
    )));
    assert_eq!(fixture.reducer.task().consented_sources(), &[admitted]);
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
        2
    );
}

#[test]
fn a_same_tab_cross_origin_terminal_replaces_instead_of_aliasing_authority() {
    let mut fixture = prepared_errand(3);
    let old = source(22, "https://search.example");
    let (first_action, first_dispatch) = dispatch_search(&mut fixture, 0);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: bip_types::identity::ActionId::new(first_action),
        outcome: Box::new(discovered_outcome(first_dispatch, old.clone())),
    });
    let new = source(23, "https://destination.example");
    let (second_action, second_dispatch) = dispatch_search(&mut fixture, 1);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: bip_types::identity::ActionId::new(second_action),
        outcome: Box::new(discovered_outcome(second_dispatch, new.clone())),
    });
    assert_eq!(
        fixture.reducer.task().consented_sources(),
        std::slice::from_ref(&new)
    );
    assert_eq!(fixture.reducer.task().scope().included(), &[new.source_id]);
    assert!(fixture
        .reducer
        .task()
        .scope()
        .excluded()
        .contains(&old.source_id));
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(BudgetKind::MaxSources),
        2
    );
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        1
    );
}

#[test]
fn changed_binding_or_exhausted_cap_cannot_mutate_the_source_register() {
    let mut fixture = prepared_errand(1);
    let first = source(24, "https://search.example");
    let (first_action, first_dispatch) = dispatch_search(&mut fixture, 0);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: bip_types::identity::ActionId::new(first_action),
        outcome: Box::new(discovered_outcome(first_dispatch, first.clone())),
    });

    let (changed_action, changed_dispatch) = dispatch_search(&mut fixture, 1);
    let changed_identity = ConsentedSource {
        source_id: first.source_id,
        tab_id: first.tab_id.clone(),
        normalized_origin: "https://changed.example".to_owned(),
        canonical_locator: None,
    };
    assert!(fixture
        .apply(Command::RecordActionOutcome {
            action_id: bip_types::identity::ActionId::new(changed_action),
            outcome: Box::new(discovered_outcome(changed_dispatch, changed_identity)),
        })
        .is_err());

    let (exhausted_action, exhausted_dispatch) = dispatch_search(&mut fixture, 2);
    assert!(fixture
        .apply(Command::RecordActionOutcome {
            action_id: bip_types::identity::ActionId::new(exhausted_action),
            outcome: Box::new(discovered_outcome(
                exhausted_dispatch,
                source(25, "https://next.example"),
            )),
        })
        .is_err());
    assert_eq!(fixture.reducer.task().consented_sources(), &[first]);
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
        0
    );
}

#[test]
fn replay_reconstructs_the_tab_source_scope_and_remaining_cap() {
    let mut fixture = prepared_errand(2);
    let admitted = source(26, "https://search.example");
    let (action, dispatch) = dispatch_search(&mut fixture, 0);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: bip_types::identity::ActionId::new(action),
        outcome: Box::new(discovered_outcome(dispatch, admitted.clone())),
    });
    let rebuilt = Reducer::replay(
        errand_seed(),
        common::defaults(),
        task_engine::ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        fixture.reducer.journal(),
    );
    let Ok((rebuilt, _)) = rebuilt else {
        unreachable!("a discovery journal written by this reducer must replay")
    };
    assert_eq!(
        rebuilt.task().snapshot().discovery_tab_id.as_ref(),
        Some(&TabId::new(DISCOVERY_TAB))
    );
    assert_eq!(
        rebuilt.task().consented_sources(),
        std::slice::from_ref(&admitted)
    );
    assert_eq!(rebuilt.task().scope().included(), &[admitted.source_id]);
    assert_eq!(rebuilt.task().ledger().spent(BudgetKind::MaxSources), 1);
    assert_eq!(rebuilt.task().snapshot().remaining_new_source_cap, 1);
}
