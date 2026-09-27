// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! How an errand ends when it will not act, in the words that are true.
//!
//! Split from `web_errand_finish.rs`, which is about what finishing an errand
//! requires. This file is about the other side: three prose replies, or four
//! arrivals somewhere the task had already been, and then an ending. There are
//! three of them, because there are three different things that happen, and
//! the product reported all of them as "Taffy could not read enough to
//! answer" — a phone's journal had seven tasks end that way, and in every one
//! the pages had been read perfectly well.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{ActionId, TabId};
use bip_types::ActionResultCode;
use task_engine::action::{BrowserIntent, OpaqueOperandKind, OpaqueOperandRef};
use task_engine::authority::{Denial, ProposalDecision};
use task_engine::{
    Command, FailureReason, TaskState, MAX_FRUITLESS_ERRAND_ARRIVALS,
    MAX_UNPRODUCTIVE_ERRAND_REPLIES,
};

use common::agent::{record_turn, request_turn, Digest};
use common::errand::{
    discovered_outcome, dispatch_authorized, prepared_errand, source, DISCOVERY_TAB,
};

/// A prepared zero-source errand on its discovery tab.
fn errand() -> common::Fixture {
    prepared_errand(4)
}

fn prose_reply(fixture: &mut common::Fixture) {
    fixture.must_apply(request_turn(fixture));
    fixture.must_apply(record_turn(fixture));
}

#[test]
fn past_the_bound_the_errand_fails_as_unable_to_reach_its_sources() {
    let mut fixture = errand();
    for _ in 0..=MAX_UNPRODUCTIVE_ERRAND_REPLIES {
        prose_reply(&mut fixture);
    }
    assert_eq!(
        fixture.reducer.unproductive_replies(),
        MAX_UNPRODUCTIVE_ERRAND_REPLIES + 1
    );
    let next = fixture
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert_eq!(
        next,
        Some(Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        })
    );
    fixture.must_apply(Command::FailTask {
        reason: FailureReason::SourcesUnavailable,
    });
    assert_eq!(fixture.state(), TaskState::Failed);
}

/// Binds one site by navigating to it, the way an errand reaches its first.
fn reach(fixture: &mut common::Fixture, ordinal: u64, seed: u8, origin: &str) -> ActionId {
    let (navigate, dispatch) = dispatch_authorized(
        fixture,
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: format!("{origin}/"),
            new_tab: false,
        },
        ordinal,
    );
    let action_id = ActionId::new(navigate);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: action_id.clone(),
        outcome: Box::new(discovered_outcome(dispatch, source(seed, origin))),
    });
    action_id
}

#[test]
fn past_the_bound_an_errand_that_read_pages_is_partly_done_rather_than_blamed() {
    let mut fixture = errand();
    reach(&mut fixture, 0, 7, "https://myaadhaar.uidai.gov.in");
    for _ in 0..=MAX_UNPRODUCTIVE_ERRAND_REPLIES {
        prose_reply(&mut fixture);
    }
    // It read a page and could not act on it. That is a partial result, not a
    // claim that it could not read — which is the sentence seven of twelve
    // tasks on a phone ended on, every one of them after reading perfectly
    // well.
    // Through the ordinary completing door, not straight to a result: a result
    // command from `RUNNING` is refused, because validation is its own step.
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(None, &Digest)
            .unwrap_or_else(|error| panic!("{error:?}")),
        Some(Command::ResultCandidateReady)
    );
    fixture.must_apply(Command::ResultCandidateReady);
    let next = fixture
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    let Some(Command::PartialResultValidated(result)) = next else {
        panic!("a task that read pages ends partly done: {next:?}");
    };
    assert_eq!(result.source_count, 1);
    assert_eq!(result.unmet.len(), 1);
    assert_eq!(result.unmet[0].subject, "errand outcome");
    fixture.must_apply(Command::PartialResultValidated(result));
    assert_eq!(fixture.state(), TaskState::Partial);
}

#[test]
fn an_errand_whose_moves_were_all_refused_says_so() {
    let mut fixture = errand();
    // Exactly what the phone recorded: a typed navigate to the official site,
    // refused before the core was ever asked, so the model searched again.
    let proposal = common::proposal_for(
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: "https://uidai.gov.in/".to_owned(),
            new_tab: false,
        },
        "turn-0-call-0",
    );
    fixture.must_apply(Command::ProposeAction(Box::new(proposal)));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == "turn-0-call-0")
        .map_or_else(
            || unreachable!("the proposal must mint one action"),
            |action| action.action_id().clone(),
        );
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id,
        decision: Box::new(ProposalDecision::Deny(Denial::new(
            ActionResultCode::Unsupported,
        ))),
        dispatch_id: None,
    });
    assert!(fixture.reducer.moves_were_refused());
    assert!(!fixture.reducer.sources_were_read());
    for _ in 0..=MAX_UNPRODUCTIVE_ERRAND_REPLIES {
        prose_reply(&mut fixture);
    }
    // `FailureReason::PolicyRefused` is declared, persisted, projected and
    // carries shipped copy, and nothing anywhere constructed it before this.
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(None, &Digest)
            .unwrap_or_else(|error| panic!("{error:?}")),
        Some(Command::FailTask {
            reason: FailureReason::PolicyRefused,
        })
    );
}

#[test]
fn arriving_where_the_task_already_was_is_counted_and_bounded() {
    let mut fixture = errand();
    reach(&mut fixture, 0, 7, "https://myaadhaar.uidai.gov.in");
    assert_eq!(fixture.reducer.fruitless_arrivals(), 0);

    // The same site again, and again: each one spent a turn and changed
    // nothing. Nothing else bounds this — a search that succeeds moves no
    // refusal counter, and every search from one tab is one fingerprint.
    for ordinal in 1..=u64::from(MAX_FRUITLESS_ERRAND_ARRIVALS) {
        reach(&mut fixture, ordinal, 7, "https://myaadhaar.uidai.gov.in");
    }
    assert_eq!(
        fixture.reducer.fruitless_arrivals(),
        MAX_FRUITLESS_ERRAND_ARRIVALS
    );
    prose_reply(&mut fixture);
    // At the bound the errand stops rather than looking again, under the same
    // ending its prose replies reach: it read a page, so it is partly done.
    fixture.must_apply(Command::ResultCandidateReady);
    assert!(matches!(
        fixture
            .reducer
            .next_agent_command(None, &Digest)
            .unwrap_or_else(|error| panic!("{error:?}")),
        Some(Command::PartialResultValidated(_))
    ));
}

#[test]
fn reaching_a_new_site_clears_the_arrival_count() {
    let mut fixture = errand();
    reach(&mut fixture, 0, 7, "https://myaadhaar.uidai.gov.in");
    reach(&mut fixture, 1, 7, "https://myaadhaar.uidai.gov.in");
    assert_eq!(fixture.reducer.fruitless_arrivals(), 1);
    reach(&mut fixture, 2, 9, "https://eaadhaar.uidai.gov.in");
    assert_eq!(fixture.reducer.fruitless_arrivals(), 0);
}

/// A search that lands on the results page it was already on.
///
/// The browser offers a source only for a tab that moved off the origin its
/// source names, so a second search on one results page carries no source at
/// all — `DiscoveredSourceChange::None`, not `AlreadyBound`. The counter this
/// bound is built from never saw it, which is exactly the loop
/// `MAX_FRUITLESS_ERRAND_ARRIVALS` names in its own doc: search, read, query,
/// search again, on one page, for as many turns as the model's budget allows.
#[test]
fn searching_the_same_results_page_again_is_arriving_where_the_task_already_was() {
    let mut fixture = errand();
    reach(&mut fixture, 0, 7, "https://www.google.com");
    assert_eq!(fixture.reducer.fruitless_arrivals(), 0);

    for ordinal in 1..=u64::from(MAX_FRUITLESS_ERRAND_ARRIVALS) {
        let (action, dispatch) = dispatch_authorized(
            &mut fixture,
            BrowserIntent::Search {
                tab: TabId::new(DISCOVERY_TAB),
                query: OpaqueOperandRef::for_call(
                    ordinal,
                    0,
                    OpaqueOperandKind::SearchQuery,
                    [u8::try_from(ordinal).unwrap_or(u8::MAX); 32],
                ),
            },
            ordinal + 100,
        );
        // The tab did not leave the results page, so the browser names no
        // source: the outcome is verified and carries nothing.
        let mut outcome = discovered_outcome(dispatch, source(7, "https://www.google.com"));
        outcome.discovered_source = None;
        fixture.must_apply(Command::RecordActionOutcome {
            action_id: ActionId::new(action),
            outcome: Box::new(outcome),
        });
    }
    assert_eq!(
        fixture.reducer.fruitless_arrivals(),
        MAX_FRUITLESS_ERRAND_ARRIVALS
    );
}

/// And the bound a working errand must not hit. Walking one site is what an
/// errand does; only a search is counted, so navigating within a site it
/// already holds costs nothing.
#[test]
fn walking_the_site_it_is_already_on_is_not_arriving_where_it_was() {
    let mut fixture = errand();
    reach(&mut fixture, 0, 7, "https://myaadhaar.uidai.gov.in");
    for ordinal in 1..=u64::from(MAX_FRUITLESS_ERRAND_ARRIVALS) + 2 {
        let (action, dispatch) = dispatch_authorized(
            &mut fixture,
            BrowserIntent::Navigate {
                tab: TabId::new(DISCOVERY_TAB),
                address: format!("https://myaadhaar.uidai.gov.in/step-{ordinal}"),
                new_tab: false,
            },
            ordinal + 200,
        );
        let mut outcome = discovered_outcome(dispatch, source(7, "https://myaadhaar.uidai.gov.in"));
        outcome.discovered_source = None;
        fixture.must_apply(Command::RecordActionOutcome {
            action_id: ActionId::new(action),
            outcome: Box::new(outcome),
        });
    }
    assert_eq!(fixture.reducer.fruitless_arrivals(), 0);
}
