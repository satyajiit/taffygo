// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A task records what it did, in order (decision 0148).
//!
//! Three properties, and each one is a defect this record exists to close.
//! The list is bounded, so a task that loops does not grow a second journal.
//! A replayed task has the steps the task that ran had, because a timeline
//! that empties when a person reopens the browser is worse than one that was
//! never there. And one transition appends one step, because the panel this
//! feeds is read as a sequence of things that happened.
#![allow(clippy::unwrap_used, clippy::expect_used)]

mod common;

use bip_types::identity::{ActionId, DispatchId, FrameId, MonotonicMillis, PageEpoch, TabId};
use bip_types::{ActionResultCode, Sensitivity};
use task_engine::action::{ActionOutcome, BrowserIntent};
use task_engine::{
    Command, ManualClock, ObservationCompleteness, ObservationGraphSummary,
    PageObservationEvidence, Reducer, TaskActivityKind, TaskActivityStep, TaskState,
    MAX_TASK_ACTIVITY,
};

use common::errand::{
    discovered_outcome, dispatch_authorized, prepared_errand, source, DISCOVERY_TAB,
};

fn steps(fixture: &common::Fixture) -> Vec<TaskActivityStep> {
    fixture.reducer.task().activity().steps().to_vec()
}

fn kinds(fixture: &common::Fixture) -> Vec<TaskActivityKind> {
    steps(fixture).iter().map(|step| step.kind).collect()
}

/// A settled outcome that carries no source, and evidence only where the read
/// actually brought a page back.
fn settled(
    dispatch: DispatchId,
    code: ActionResultCode,
    observation: Option<PageObservationEvidence>,
) -> ActionOutcome {
    ActionOutcome {
        code,
        dispatch_id: Some(dispatch),
        observed_at: MonotonicMillis(2_000),
        observation,
        discovered_source: None,
    }
}

/// What the browser committed for one read of the discovery tab.
fn observation(origin: &str, epoch: u64) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new(DISCOVERY_TAB),
        frame_id: FrameId("frame_1".to_owned()),
        page_epoch: PageEpoch(format!("epoch_{epoch}")),
        graph_revision: epoch,
        normalized_origin: origin.to_owned(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 1,
            text_byte_count: 4,
        },
        total_bytes: 32,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    }
}

/// The one origin every fixture here reads.
const ORIGIN: &str = "https://destination.example";

/// Navigates the discovery tab to `origin`, admitting it as a source.
fn navigate_to(fixture: &mut common::Fixture, ordinal: u64, seed: u8, origin: &str) {
    let (action, dispatch) = dispatch_authorized(
        fixture,
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: format!("{origin}/start"),
            new_tab: false,
        },
        ordinal,
    );
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(discovered_outcome(dispatch, source(seed, origin))),
    });
}

/// Reads the discovery tab, answering `code`.
fn read_page(fixture: &mut common::Fixture, ordinal: u64, code: ActionResultCode) {
    let (action, dispatch) = dispatch_authorized(
        fixture,
        BrowserIntent::DomRead {
            tab: TabId::new(DISCOVERY_TAB),
            target: None,
        },
        ordinal,
    );
    let seen = (code == ActionResultCode::Verified).then(|| observation(ORIGIN, ordinal));
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(settled(dispatch, code, seen)),
    });
}

/// The errand's own shape: a page opened, then read, and both name the host.
#[test]
fn a_page_opened_and_read_is_two_steps_that_name_where_they_happened() {
    let mut fixture = prepared_errand(4);
    navigate_to(&mut fixture, 0, 50, ORIGIN);
    read_page(&mut fixture, 1, ActionResultCode::Verified);

    let steps = steps(&fixture);
    assert_eq!(
        steps.iter().map(|step| step.kind).collect::<Vec<_>>(),
        vec![TaskActivityKind::OpenedPage, TaskActivityKind::ReadPage],
    );
    for step in &steps {
        assert_eq!(
            step.host.as_deref(),
            Some("destination.example"),
            "a step about a page names its host and never its address",
        );
    }
    // The step that discovered the source names it too: the note is written
    // after the admission, so the move that reaches a page for the first time
    // is not the one step with nothing to say.
    assert_eq!(steps.first().map(|step| step.sequence), Some(1));
}

/// The two refusals a person reads differently.
#[test]
fn a_page_that_would_not_load_and_a_move_that_was_refused_are_different_steps() {
    let mut fixture = prepared_errand(4);
    navigate_to(&mut fixture, 0, 50, ORIGIN);
    read_page(&mut fixture, 1, ActionResultCode::TabGone);
    read_page(&mut fixture, 2, ActionResultCode::DeniedByPolicy);

    assert_eq!(
        kinds(&fixture),
        vec![
            TaskActivityKind::OpenedPage,
            TaskActivityKind::PageUnavailable,
            TaskActivityKind::MoveRefused,
        ],
    );
}

/// **The bound, and which end of the list it drops from.**
///
/// The task this record was written for had read the same page eighty-five
/// times. What matters is that the list stays a fact about the task rather
/// than a function of a model's behaviour, and that what survives is the
/// recent end — the oldest step is the one nobody is looking for.
#[test]
fn the_record_stops_at_its_bound_and_drops_from_the_front() {
    let mut fixture = prepared_errand(4);
    navigate_to(&mut fixture, 0, 50, ORIGIN);
    let over = u64::try_from(MAX_TASK_ACTIVITY).unwrap() + 4;
    for ordinal in 1..=over {
        read_page(&mut fixture, ordinal, ActionResultCode::Verified);
    }

    let steps = steps(&fixture);
    assert_eq!(steps.len(), MAX_TASK_ACTIVITY);
    assert_eq!(
        fixture.reducer.task().activity().appended(),
        over + 1,
        "the count of everything that happened outlives the steps that are kept",
    );
    assert!(
        steps
            .iter()
            .all(|step| step.kind == TaskActivityKind::ReadPage),
        "the one opened_page at the front is the step that was dropped",
    );
    // The sequence never restarts, so a surface can tell the first thing that
    // happened from the oldest thing still kept.
    assert_eq!(
        steps.first().map(|step| step.sequence),
        Some(over + 2 - u64::try_from(MAX_TASK_ACTIVITY).unwrap())
    );
    assert_eq!(steps.last().map(|step| step.sequence), Some(over + 1));
}

/// **A restored task has the steps the task that ran had.**
///
/// This is the whole reason the append points are in the reducer. A step
/// written in a port or a projection is a step replay does not reproduce, and
/// the timeline would empty every time a person reopened the browser.
#[test]
fn a_replayed_task_has_the_same_steps_as_the_one_that_ran() {
    let mut fixture = prepared_errand(4);
    navigate_to(&mut fixture, 0, 50, ORIGIN);
    read_page(&mut fixture, 1, ActionResultCode::Verified);
    read_page(&mut fixture, 2, ActionResultCode::PostconditionTimeout);
    fixture.must_apply(Command::RequestUserInput);
    fixture.must_apply(Command::SupplyUserInput);

    let (restored, _) = Reducer::replay(
        common::errand::errand_seed(),
        common::defaults(),
        ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        fixture.reducer.journal(),
    )
    .expect("the journal replays");

    assert_eq!(
        restored.task().activity().steps(),
        fixture.reducer.task().activity().steps(),
        "every step, in order, with the same hosts, counts and times",
    );
    assert_eq!(
        restored.task().activity().appended(),
        fixture.reducer.task().activity().appended(),
    );
}

/// One transition appends one step — never two, and never none.
///
/// The panel is read as a sequence of things that happened, so a handler that
/// noted twice would draw one moment as two and a person would count it as
/// two pages.
#[test]
fn one_transition_appends_exactly_one_step() {
    let mut fixture = prepared_errand(4);
    let mut before = fixture.reducer.task().activity().appended();

    let mut one_more = |fixture: &common::Fixture, what: &str| {
        let after = fixture.reducer.task().activity().appended();
        assert_eq!(
            after,
            before + 1,
            "{what} appended more or less than one step"
        );
        before = after;
    };

    navigate_to(&mut fixture, 0, 50, ORIGIN);
    one_more(&fixture, "a navigation that discovered a source");
    read_page(&mut fixture, 1, ActionResultCode::Verified);
    one_more(&fixture, "a read");
    fixture.must_apply(Command::RequestUserInput);
    one_more(&fixture, "asking the person");
    fixture.must_apply(Command::SupplyUserInput);
    one_more(&fixture, "the person answering");
}

/// The person's own steps, both halves of every wait.
#[test]
fn every_wait_on_the_person_opens_and_closes_with_a_step() {
    let mut fixture = prepared_errand(4);
    fixture.must_apply(Command::RequestUserInput);
    fixture.must_apply(Command::SupplyUserInput);
    fixture.must_apply(Command::RequestHandover {
        handover_id: task_engine::handover::handover_id_for_call(0, 0),
    });
    assert_eq!(fixture.state(), TaskState::WaitingUser);

    assert_eq!(
        kinds(&fixture),
        vec![
            TaskActivityKind::AskedYou,
            TaskActivityKind::YouAnswered,
            TaskActivityKind::HandedBack,
        ],
    );
}

/// The last step of a task that finished, and the count it carries.
#[test]
fn the_output_is_the_last_step_and_names_the_facts_it_rests_on() {
    let mut fixture = prepared_errand(4);
    navigate_to(&mut fixture, 0, 50, ORIGIN);
    fixture.must_apply(Command::ResultCandidateReady);
    fixture.must_apply(Command::CompleteResultValidated(common::complete_result()));
    assert_eq!(fixture.state(), TaskState::Completed);

    let steps = steps(&fixture);
    let last = steps.last().expect("a finished task has steps");
    assert_eq!(last.kind, TaskActivityKind::BuiltOutput);
    assert_eq!(last.count, 2, "the facts the result rests on");
    assert_eq!(last.host, None, "an output is not about one page");
}
