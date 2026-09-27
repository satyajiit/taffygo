// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A reading of a tab that moved within the site its source names.
//!
//! Decision 0160 section 2: the tab is the match, and the origin belongs
//! to the browser. These drive the same production reducer and loop walk
//! as the file that declares them.

use loop_kernel::state::LoopState;
use task_engine::Command;

use super::{adopt, evidence_for, Driver};
#[test]
fn a_reading_of_a_tab_that_moved_within_its_site_opens_the_paid_turn() {
    // Run L on the phone: the press before this read left the tab on a sibling
    // host of the site the source names. The browser authorized the read
    // against that same source, and the core then refused to count it — five
    // times, until the bootstrap bound ended the task as "could not read
    // enough to answer". Decision 0160 section 2: the tab is the match.
    let mut driver = Driver::running();
    let mut state = LoopState::default();
    let (action_id, _, _) = driver.propose_observation(&mut state);

    let moved = evidence_for("tab-live", "https://beta.example.test", "epoch-2");
    driver.verify_observation_with(action_id, moved.clone(), "moved");
    adopt(&mut state, &moved);

    let sources = driver.task.task().consented_sources();
    assert_eq!(
        state.page.matching_observations(sources).len(),
        1,
        "the arena holds the reading that source needed"
    );
    let model = driver
        .advance(&mut state)
        .expect("the verified reading opens the turn");
    assert!(matches!(
        model.envelope.command,
        Command::RequestModelTurn { .. }
    ));
    assert_eq!(driver.task.turns_started(), 0);
    driver.task.apply(model.envelope).unwrap();
    assert_eq!(driver.task.turns_started(), 1);
}

#[test]
fn a_reading_of_a_tab_the_task_holds_no_source_for_reobserves() {
    // The other half of the same rule, so relaxing the origin cannot be read
    // as relaxing the tab: bytes from a tab the task was never given stay
    // invisible, and the walk asks for the reading it is still owed.
    let mut driver = Driver::running();
    let mut state = LoopState::default();
    let (action_id, _, _) = driver.propose_observation(&mut state);
    driver.verify_observation(action_id);

    let foreign = evidence_for("tab-not-held", "https://example.test", "epoch-1");
    adopt(&mut state, &foreign);
    let sources = driver.task.task().consented_sources();
    assert!(state.page.matching_observations(sources).is_empty());
    assert!(matches!(
        driver.advance(&mut state).map(|plan| plan.envelope.command),
        Some(Command::ProposeAction(_))
    ));
}
