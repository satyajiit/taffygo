// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The facts a running task's notice line is read from: a paid attempt in
//! flight, a reply being asked for again, a move that was refused.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{ActionId, TabId};
use bip_types::ActionResultCode;
use task_engine::action::BrowserIntent;
use task_engine::{Command, Denial, ModelAttemptKind, ModelStopReason, ProposalDecision, TurnGap};

use common::agent::{record_turn, request_turn};
use common::errand::{dispatch_authorized, prepared_errand, DISCOVERY_TAB};

fn outstanding_call(fixture: &common::Fixture) -> task_engine::ModelCallId {
    fixture.reducer.model_turn().map_or_else(
        || unreachable!("a turn is outstanding"),
        |turn| turn.call_id().clone(),
    )
}

#[test]
fn a_retry_and_a_failover_name_themselves_while_in_flight_and_not_after() {
    let mut fixture = common::running_within(
        task_engine::TaskBudgets::none().with(task_engine::BudgetKind::MaxRetriesPerStep, 2),
    );
    fixture.must_apply(request_turn(&fixture));
    assert_eq!(fixture.reducer.model_attempt_in_flight(), None);
    let call_id = outstanding_call(&fixture);
    fixture.must_apply(Command::RequestModelAttempt {
        call_id: call_id.clone(),
        attempt_ordinal: 1,
        candidate_ordinal: 0,
        kind: ModelAttemptKind::Retry,
    });
    assert_eq!(
        fixture.reducer.model_attempt_in_flight(),
        Some(ModelAttemptKind::Retry)
    );
    fixture.must_apply(Command::RequestModelAttempt {
        call_id,
        attempt_ordinal: 2,
        candidate_ordinal: 1,
        kind: ModelAttemptKind::Failover,
    });
    assert_eq!(
        fixture.reducer.model_attempt_in_flight(),
        Some(ModelAttemptKind::Failover)
    );
    fixture.must_apply(record_turn(&fixture));
    assert_eq!(fixture.reducer.model_attempt_in_flight(), None);
}

#[test]
fn a_turn_after_an_unreadable_reply_or_a_cut_off_one_is_a_re_ask() {
    let mut fixture = common::agent::running();
    fixture.must_apply(request_turn(&fixture));
    assert!(!fixture.reducer.reply_being_reasked());
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id: outstanding_call(&fixture),
        gap: TurnGap::Unreadable,
    });
    fixture.must_apply(request_turn(&fixture));
    assert!(fixture.reducer.reply_being_reasked());
    fixture.must_apply(record_turn(&fixture));
    assert!(!fixture.reducer.reply_being_reasked());

    let mut cut = common::turn_digest();
    cut.stop = ModelStopReason::Length;
    fixture.must_apply(request_turn(&fixture));
    fixture.must_apply(Command::RecordModelTurn {
        call_id: outstanding_call(&fixture),
        digest: Box::new(cut),
    });
    fixture.must_apply(request_turn(&fixture));
    assert!(fixture.reducer.reply_being_reasked());

    // A reply that was read whole is not re-asked, whatever came before it.
    fixture.must_apply(record_turn(&fixture));
    fixture.must_apply(request_turn(&fixture));
    assert!(!fixture.reducer.reply_being_reasked());
}

#[test]
fn a_refused_move_is_remembered_until_one_is_verified() {
    let mut fixture = prepared_errand(4);
    assert!(!fixture.reducer.last_move_refused());
    let proposal = common::proposal_for(
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: "https://destination.example/start".to_owned(),
            new_tab: false,
        },
        "turn-0-call-0",
    );
    fixture.must_apply(Command::ProposeAction(Box::new(proposal)));
    let refused = fixture.reducer.actions().next().map_or_else(
        || unreachable!("the proposal minted an action"),
        |action| action.action_id().clone(),
    );
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: refused,
        decision: Box::new(ProposalDecision::Deny(Denial::new(
            ActionResultCode::EgressNotAuthorized,
        ))),
        dispatch_id: None,
    });
    assert!(fixture.reducer.last_move_refused());

    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: "https://destination.example/start".to_owned(),
            new_tab: false,
        },
        1,
    );
    // Authorized and dispatched is not yet verified.
    assert!(fixture.reducer.last_move_refused());
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(common::errand::discovered_outcome(
            dispatch,
            common::errand::source(50, "https://destination.example"),
        )),
    });
    assert!(!fixture.reducer.last_move_refused());
}
