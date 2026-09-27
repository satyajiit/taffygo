// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A move that lands its own tab may say where it landed.
//!
//! Three layers name that set of moves, and for a while they did not name the
//! same one. The browser offers a source on a navigate, a search, a back, a
//! forward, a reload, a link open and a form submit
//! (`TaskActionOperationSettlesItsOwnTab`); the core's terminal decoder admits
//! a source on exactly those seven (`decode_discovered_source`); the reducer
//! gated on the action's *class* and so refused the three that carry
//! `ControlTab`. That refusal is `ActionOutcomeMismatch`, which ends the
//! core's publication and every task in the profile with it. Decision 0163.

mod common;

use bip_types::identity::{ActionId, TabId};
use task_engine::action::BrowserIntent;
use task_engine::transition::RefusalReason;
use task_engine::Command;

use common::errand::{
    discovered_outcome, dispatch_authorized, prepared_errand_admitting, source, DISCOVERY_TAB,
};

fn navigate(address: &str) -> BrowserIntent {
    BrowserIntent::Navigate {
        tab: TabId::new(DISCOVERY_TAB),
        address: address.to_owned(),
        new_tab: false,
    }
}

/// Binds `https://myaadhaar.uidai.gov.in` to the discovery tab, the way the
/// errand on the phone does, and answers the fixture.
fn errand_on_its_first_site(tool: &str) -> common::Fixture {
    let mut fixture = prepared_errand_admitting(4, tool);
    let (action, dispatch) =
        dispatch_authorized(&mut fixture, navigate("https://myaadhaar.uidai.gov.in/"), 0);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(40, "https://myaadhaar.uidai.gov.in"),
        )),
    });
    fixture
}

fn origins(fixture: &common::Fixture) -> Vec<String> {
    fixture
        .reducer
        .task()
        .consented_sources()
        .iter()
        .map(|held| held.normalized_origin.clone())
        .collect()
}

/// The case that ended a task on the phone. A press had already moved the tab
/// to `tathya.uidai.gov.in` — a press settles no tab, so nothing said so — and
/// the reload that followed is the browser noticing and offering a source for
/// where the tab actually is.
#[test]
fn a_reload_of_a_tab_that_moved_binds_where_it_landed() {
    let mut fixture = errand_on_its_first_site("browser.reload");
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Reload {
            tab: TabId::new(DISCOVERY_TAB),
        },
        1,
    );
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(41, "https://tathya.uidai.gov.in"),
        )),
    });
    assert_eq!(origins(&fixture), vec!["https://tathya.uidai.gov.in"]);
}

/// The same for the other two moves of that class: a back and a forward each
/// land their own tab, and each may say so.
#[test]
fn a_back_that_lands_elsewhere_binds_where_it_landed() {
    let mut fixture = errand_on_its_first_site("browser.back");
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::HistoryBack {
            tab: TabId::new(DISCOVERY_TAB),
        },
        1,
    );
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(41, "https://tathya.uidai.gov.in"),
        )),
    });
    assert_eq!(origins(&fixture), vec!["https://tathya.uidai.gov.in"]);
}

/// A reload that stayed put costs the sites budget nothing: the browser
/// re-offers the source the tab already holds and the task recognises it.
#[test]
fn a_reload_that_stayed_put_binds_nothing_new() {
    let mut fixture = errand_on_its_first_site("browser.reload");
    let remaining = fixture.reducer.task().snapshot().remaining_new_source_cap;
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Reload {
            tab: TabId::new(DISCOVERY_TAB),
        },
        1,
    );
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(40, "https://myaadhaar.uidai.gov.in"),
        )),
    });
    assert_eq!(origins(&fixture), vec!["https://myaadhaar.uidai.gov.in"]);
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        remaining
    );
}

/// And the bound the set still has. Stopping a load lands nowhere, so a source
/// offered on one is an envelope no reader can check — the browser does not
/// offer one and the terminal decoder refuses one, and the reducer says the
/// same thing rather than a wider thing.
#[test]
fn a_stopped_load_may_not_say_where_the_tab_is() {
    let mut fixture = errand_on_its_first_site("browser.stop_loading");
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::StopLoading {
            tab: TabId::new(DISCOVERY_TAB),
        },
        1,
    );
    let refused = fixture.apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(41, "https://tathya.uidai.gov.in"),
        )),
    });
    assert_eq!(
        refused.err().map(|refusal| refusal.reason),
        Some(RefusalReason::ActionOutcomeMismatch)
    );
    assert_eq!(origins(&fixture), vec!["https://myaadhaar.uidai.gov.in"]);
}
