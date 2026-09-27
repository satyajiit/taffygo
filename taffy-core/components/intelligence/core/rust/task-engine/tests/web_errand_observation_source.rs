// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which source a reading belongs to.
//!
//! The tab is the match. A site may answer on a sibling host of its own
//! registrable domain, and a page may move there with a client-side route
//! after the commit; whether that is still the same site needs the
//! registry-controlled domain table, which only the browser holds. It has
//! already spent it — no observation reaches the reducer that the browser's
//! own ledger did not admit against this exact source — so a second, weaker
//! test here could only refuse what the browser allowed.

mod common;

use bip_types::identity::{ActionId, FrameId, PageEpoch, TabId};
use bip_types::Sensitivity;
use task_engine::action::BrowserIntent;
use task_engine::transition::RefusalReason;
use task_engine::{
    Command, ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
};

use common::agent::Digest;
use common::errand::{
    discovered_outcome, dispatch_authorized, prepared_errand, source, DISCOVERY_TAB,
};

fn navigate(address: &str) -> BrowserIntent {
    BrowserIntent::Navigate {
        tab: TabId::new(DISCOVERY_TAB),
        address: address.to_owned(),
        new_tab: false,
    }
}

fn read(tab: &str) -> BrowserIntent {
    BrowserIntent::DomRead {
        tab: TabId::new(tab),
        target: None,
    }
}

fn observation(tab: &str, origin: &str) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new(tab),
        frame_id: FrameId("frame_1".to_owned()),
        page_epoch: PageEpoch("epoch-1".to_owned()),
        graph_revision: 7,
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

/// Binds `https://myaadhaar.uidai.gov.in` to the discovery tab, the way the
/// errand on the phone does, and answers the fixture.
fn errand_on_its_first_site() -> common::Fixture {
    let mut fixture = prepared_errand(4);
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

/// The case that ended the core on a phone: a press on
/// `myaadhaar.uidai.gov.in` landed on `tathya.uidai.gov.in`, and the next
/// read's evidence named an origin the source row does not hold.
#[test]
fn a_reading_from_a_tab_that_moved_within_its_site_belongs_to_that_tabs_source() {
    let mut fixture = errand_on_its_first_site();
    let (action, dispatch) = dispatch_authorized(&mut fixture, read(DISCOVERY_TAB), 1);
    let mut outcome = discovered_outcome(dispatch, source(41, "https://tathya.uidai.gov.in"));
    outcome.discovered_source = None;
    outcome.observation = Some(observation(DISCOVERY_TAB, "https://tathya.uidai.gov.in"));
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(outcome),
    });
    let held = fixture.reducer.task().consented_sources();
    assert_eq!(held.len(), 1);
    assert_eq!(
        held.first().map(|source| source.normalized_origin.as_str()),
        Some("https://myaadhaar.uidai.gov.in")
    );
}

/// The rule the tab match still states: a reading from a tab this task holds
/// no source for is nobody's reading.
#[test]
fn a_reading_from_a_tab_the_task_holds_no_source_for_is_refused() {
    let mut fixture = errand_on_its_first_site();
    let (action, dispatch) = dispatch_authorized(&mut fixture, read("another-task-tab"), 1);
    let mut outcome = discovered_outcome(dispatch, source(41, "https://myaadhaar.uidai.gov.in"));
    outcome.discovered_source = None;
    outcome.observation = Some(observation(
        "another-task-tab",
        "https://myaadhaar.uidai.gov.in",
    ));
    let refused = fixture.apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(outcome),
    });
    assert_eq!(
        refused.err().map(|refusal| refusal.reason),
        Some(RefusalReason::ActionOutcomeMismatch)
    );
}

/// The prerequisite the reducer's tab match has to agree with.
///
/// `source_observation_state` held a fourth copy of the origin comparison, so
/// a reading of a tab that had moved within its own site verified and was then
/// found un-live: the walk proposed another, and another. A phone ran five
/// hundred and eleven identical reads of one page between two model turns.
#[test]
fn a_reading_of_a_tab_that_moved_within_its_site_is_the_reading_that_source_needed() {
    let mut fixture = errand_on_its_first_site();
    let held = fixture.reducer.task().consented_sources().to_vec();
    let source = held.first().unwrap_or_else(|| unreachable!("one source"));
    let evidence = observation(DISCOVERY_TAB, "https://tathya.uidai.gov.in");
    let live = vec![task_engine::LiveSourceObservation {
        source_id: source.source_id,
        evidence: evidence.clone(),
    }];
    // The walk asks for a reading, and gets the proposal it would dispatch.
    let task_engine::PreModelObservation::Command(Command::ProposeAction(proposal)) = fixture
        .reducer
        .next_pre_model_observation(&live, &Digest)
        .unwrap_or_else(|error| unreachable!("the errand may read: {error:?}"))
    else {
        unreachable!("a source with no reading needs one")
    };
    let key = proposal.idempotency_key.as_str().to_owned();
    fixture.must_apply(Command::ProposeAction(proposal));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map_or_else(
            || unreachable!("the proposal mints one action"),
            |action| action.action_id().clone(),
        );
    let dispatch = bip_types::identity::DispatchId::new("dispatch-read-1");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(task_engine::ProposalDecision::Authorize(
            task_engine::Authorization {
                capability_id: task_engine::CapabilityId::new("capability-read-1"),
            },
        )),
        dispatch_id: Some(dispatch.clone()),
    });
    let mut outcome = discovered_outcome(dispatch, source.clone());
    outcome.discovered_source = None;
    outcome.observation = Some(evidence);
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(outcome),
    });
    // And the reading it just recorded is the one that source needed.
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&live, &Digest)
            .unwrap_or_else(|error| unreachable!("{error:?}")),
        task_engine::PreModelObservation::Ready
    );
}
