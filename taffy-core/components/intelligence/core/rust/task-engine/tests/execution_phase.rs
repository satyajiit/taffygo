// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The phase inside RUNNING follows the loop's own commands, so a surface
//! reading it sees thinking, reading and acting rather than one word for the
//! whole run.

mod common;

use bip_types::identity::{ActionId, TabId};
use task_engine::action::BrowserIntent;
use task_engine::{Command, ExecutionPhase, Reducer};

use common::agent::{record_turn, request_turn};
use common::errand::{
    discovered_outcome, dispatch_authorized, errand_seed, prepared_errand, source, DISCOVERY_TAB,
};

fn phase(fixture: &common::Fixture) -> Option<ExecutionPhase> {
    fixture.reducer.task().execution_phase()
}

#[test]
fn the_phase_follows_one_turn_one_move_and_one_read() {
    let mut fixture = prepared_errand(4);
    assert_eq!(phase(&fixture), Some(ExecutionPhase::Planning));

    let request = request_turn(&fixture);
    fixture.must_apply(request);
    assert_eq!(phase(&fixture), Some(ExecutionPhase::Inferencing));

    let record = record_turn(&fixture);
    fixture.must_apply(record);
    assert_eq!(phase(&fixture), Some(ExecutionPhase::Planning));

    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: "https://destination.example/start".to_owned(),
            new_tab: false,
        },
        0,
    );
    assert_eq!(phase(&fixture), Some(ExecutionPhase::Acting));

    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(50, "https://destination.example"),
        )),
    });
    assert_eq!(phase(&fixture), Some(ExecutionPhase::Planning));

    dispatch_authorized(
        &mut fixture,
        BrowserIntent::DomRead {
            tab: TabId::new(DISCOVERY_TAB),
            target: None,
        },
        1,
    );
    assert_eq!(phase(&fixture), Some(ExecutionPhase::Observing));
}

/// The phase is not journaled: a replay re-applies the same commands and
/// stands where the live reducer stood.
#[test]
fn replay_reaches_the_same_phase() {
    let mut fixture = prepared_errand(2);
    let request = request_turn(&fixture);
    fixture.must_apply(request);
    let rebuilt = Reducer::replay(
        errand_seed(),
        common::defaults(),
        task_engine::ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        fixture.reducer.journal(),
    );
    let Ok((rebuilt, _)) = rebuilt else {
        unreachable!("a journal written by this reducer must replay")
    };
    assert_eq!(
        rebuilt.task().execution_phase(),
        Some(ExecutionPhase::Inferencing)
    );
    assert_eq!(rebuilt.task().execution_phase(), phase(&fixture));
}
