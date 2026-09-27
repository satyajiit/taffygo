// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! An errand asked from a page holds that page and a blank tab at once.
//!
//! This is the ordinary shape and it could not work: a person on a page asks
//! Taffy to do something somewhere else, the composer binds the page they are
//! on as a consented source, and every gate on the discovery path then read
//! "holds a source" as "discovery is over". The blank tab was never prepared,
//! the search went out through the person's own tab, that tab left the origin
//! its consent named, and the consent was destroyed for good (decision 0224).
//!
//! The four proofs here are the four halves of the repair, and the third is
//! the one that cost a core: a discovery landing while a source is held used
//! to answer `ActionOutcomeMismatch`, which ends the core's publication and
//! every task in the profile with it.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{ActionId, FrameId, PageEpoch, TabId};
use task_engine::action::{BrowserIntent, OpaqueOperandKind, OpaqueOperandRef};
use task_engine::agent::{ModelToolCall, TurnPage, TurnResidency};
use task_engine::{
    ArgumentValue, BrowserSessionId, Command, Effect, ModelStopReason, SuppliedArgument,
};

use common::agent::{navigate_call, page, record_turn_in, Digest};
use common::errand::{
    discovered_outcome, dispatch_authorized, source, BROWSER_SESSION, DISCOVERY_TAB,
};

/// The page the person was on when they asked. `common::preview` binds it as
/// the task's one consented source, on `tab_1`, which is also the tab
/// `common::agent::page` renders from.
const PAGE_TAB: &str = "tab_1";

/// The shared errand seed names the moves a zero-source errand makes. The
/// rule under test is about every move that lands a tab, so the rest are
/// named here rather than left refused on sight.
const EVERY_MOVE: &[&str] = &["browser.back", "browser.forward", "browser.reload"];

/// An errand started from a page, with discovery granted beside it.
fn errand_from_a_page(cap: u32) -> common::Fixture {
    common::errand::errand_from_a_page(cap, EVERY_MOVE)
}

/// [`errand_from_a_page`] carried through consent to a running task that knows
/// its blank tab.
fn running_errand_from_a_page(cap: u32) -> common::Fixture {
    common::errand::running_errand_from_a_page(cap, EVERY_MOVE)
}

/// The projection a turn is composed from: the person's page, plus the blank
/// tab the task also holds.
fn page_and_blank_tab() -> TurnPage {
    let (page, _) = page();
    page.with_discovery_tab(Some(TabId::new(DISCOVERY_TAB)))
}

/// Records one reply against `fixture` and answers the residency it left.
fn recorded_turn(
    fixture: &mut common::Fixture,
    page: TurnPage,
    call: ModelToolCall,
) -> TurnResidency {
    record_turn_in(fixture, page, ModelStopReason::ToolCall, vec![call])
}

/// The tab the one call of `page` and `call` would be proposed against.
fn proposed_tab(call: ModelToolCall) -> TabId {
    let mut fixture = running_errand_from_a_page(8);
    let residency = recorded_turn(&mut fixture, page_and_blank_tab(), call);
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("the walk answers");
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected one proposal, got {next:?}");
    };
    proposal.tab_id().clone()
}

fn search_call(query: &str) -> ModelToolCall {
    ModelToolCall::new(
        "browser.search",
        vec![SuppliedArgument::new(
            "query",
            ArgumentValue::Text(query.to_owned()),
        )],
    )
}

/// Holding a page is not a reason to withhold the blank tab. The two grants
/// are separate sentences in the same sheet.
#[test]
fn consent_on_an_errand_started_from_a_page_still_prepares_the_blank_tab() {
    let mut fixture = errand_from_a_page(8);
    let accepted = fixture
        .apply(Command::AcceptInitialConsent(common::receipt()))
        .unwrap_or_else(|refusal| unreachable!("valid consent was refused: {refusal:?}"));
    assert_eq!(
        accepted.effects,
        vec![Effect::PrepareDiscoveryTab {
            browser_session_id: BrowserSessionId::new(BROWSER_SESSION)
                .unwrap_or_else(|_| unreachable!()),
            remaining_new_source_cap: 8,
        }],
        "an errand that holds a page was never offered a tab to look from"
    );
    assert_eq!(fixture.reducer.task().consented_sources().len(), 1);
}

/// Every move happens in Taffy's own tab; reading stays on the person's page.
///
/// The split is the whole rule. A move that lands a tab the *person* owns on
/// a new site can never be admitted — a source is issued only for a tab the
/// task owns — so the ledger reads it as the person carrying the tab away and
/// destroys the consent. Measured twice on a phone, once through the search
/// and once through the navigate.
#[test]
fn every_move_acts_in_the_tasks_own_tab_and_reading_stays_on_the_page() {
    for call in [
        search_call("download eaadhaar"),
        navigate_call("https://example.test/next"),
        ModelToolCall::new("browser.back", vec![]),
        ModelToolCall::new("browser.forward", vec![]),
        ModelToolCall::new("browser.reload", vec![]),
    ] {
        let name = call.tool_name.clone();
        assert_eq!(
            proposed_tab(call),
            TabId::new(DISCOVERY_TAB),
            "{name} landed a tab the task does not own"
        );
    }
    assert_eq!(
        proposed_tab(ModelToolCall::new("browser.dom.read", vec![])),
        TabId::new(PAGE_TAB),
        "with nothing standing in the blank tab there is only the page to read"
    );
}

/// The read that follows the search. Once the task's own tab is holding a
/// document, "read the page" means that one — the site the task went and
/// found — and not the page the errand was asked from.
///
/// This is the half of decision 0224 the phone found missing. Routing the
/// move without routing the read left the task opening a results page in its
/// own tab and then reading the person's page instead, fourteen times, byte
/// for byte identical, while the results it had just landed were read once
/// and never again (decision 0225).
#[test]
fn a_read_follows_the_task_into_the_tab_it_landed() {
    let mut fixture = running_errand_from_a_page(8);
    let (page, _) = page();
    let landed = page
        .with_discovery_tab(Some(TabId::new(DISCOVERY_TAB)))
        .with_current_documents(vec![
            task_engine::CurrentDocument {
                tab: TabId::new(PAGE_TAB),
                frame: FrameId::new("frame_1"),
                page_epoch: PageEpoch::new("epoch_1"),
            },
            task_engine::CurrentDocument {
                tab: TabId::new(DISCOVERY_TAB),
                frame: FrameId::new("frame_2"),
                page_epoch: PageEpoch::new("epoch_2"),
            },
        ]);
    let residency = recorded_turn(
        &mut fixture,
        landed,
        ModelToolCall::new("browser.dom.read", vec![]),
    );
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("the walk answers");
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected one proposal, got {next:?}");
    };
    assert_eq!(proposal.tab_id(), &TabId::new(DISCOVERY_TAB));
}

/// With no blank tab to name — a task restored without one — a search stays
/// where it always was, on the page the turn was composed from.
#[test]
fn a_search_with_no_blank_tab_still_acts_on_the_page() {
    let mut fixture = running_errand_from_a_page(8);
    let (page, _) = page();
    let residency = recorded_turn(&mut fixture, page, search_call("anything"));
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("the walk answers");
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected one proposal, got {next:?}");
    };
    assert_eq!(proposal.tab_id(), &TabId::new(PAGE_TAB));
}

/// The landing that used to take the core down.
///
/// A verified discovery whose tab is the blank tab, while the task holds a
/// page source, reached `discovered_source_matches_action` through the
/// held-sources branch, found the blank tab in no source, and answered
/// `ActionOutcomeMismatch` — which is not a refused action but a refused
/// publication, and ends every task in the profile (decision 0163).
#[test]
fn the_first_discovery_is_admitted_while_the_page_source_is_held() {
    let mut fixture = running_errand_from_a_page(8);
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Search {
            tab: TabId::new(DISCOVERY_TAB),
            query: OpaqueOperandRef::for_call(0, 0, OpaqueOperandKind::SearchQuery, [3_u8; 32]),
        },
        0,
    );
    let landed = source(20, "https://search.example");
    fixture
        .apply(Command::RecordActionOutcome {
            action_id: ActionId::new(action),
            outcome: Box::new(discovered_outcome(dispatch, landed.clone())),
        })
        .unwrap_or_else(|refusal| unreachable!("the first discovery was refused: {refusal:?}"));
    let held = fixture.reducer.task().consented_sources().to_vec();
    assert_eq!(held.len(), 2, "the page and the site it went and found");
    assert!(held.contains(&landed));
    assert!(held.iter().any(|source| source.tab_id.as_str() == PAGE_TAB));
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        7,
        "and the site it found is what the cap paid for"
    );
}

/// The blank tab is good for one landing, and the cap pays for it.
#[test]
fn the_landing_in_the_blank_tab_spends_one_site() {
    let mut fixture = running_errand_from_a_page(1);
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Search {
            tab: TabId::new(DISCOVERY_TAB),
            query: OpaqueOperandRef::for_call(0, 0, OpaqueOperandKind::SearchQuery, [5_u8; 32]),
        },
        0,
    );
    fixture
        .apply(Command::RecordActionOutcome {
            action_id: ActionId::new(action),
            outcome: Box::new(discovered_outcome(
                dispatch,
                source(21, "https://search.example"),
            )),
        })
        .unwrap_or_else(|refusal| unreachable!("the first discovery was refused: {refusal:?}"));
    assert_eq!(
        fixture.reducer.task().snapshot().remaining_new_source_cap,
        0
    );
}
