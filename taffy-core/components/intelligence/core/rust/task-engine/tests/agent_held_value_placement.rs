// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One sheet for every field only the person can supply, and their values
//! placed with no model turn between the answer and the fills (decision
//! 0238).
//!
//! On 2026-09-24 an eAadhaar errand asked the person for their identity number
//! alone — the page has no form element, so the sheet asked about the one line
//! named — while the CAPTCHA beside it waited for another ask. The person
//! answered, and the model then spent twenty-nine seconds reading, querying,
//! opening a link, pressing and scrolling, and never made the fill it had been
//! told how to make. Both halves are the task's to do: which fields only the
//! person can supply is a fact the snapshot already printed, and where each
//! answer goes is a fact the browser reports with the count.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::ActionResultCode;
use task_engine::authority::{Denial, ProposalDecision};
use task_engine::{
    CallVerdict, Command, FieldValueAskOutcome, HeldValuesPlaced, NotAttempted, RefusalReason,
    TaskState,
};

use common::agent::Digest;
use common::value_sheet::{
    aadhaar_page, answer_fill, answered_with_two_values, apply_fill, companions_of, errand, ids,
    is_fill_of, next, proposed_fill, rebuilt_from_the_store, request_naming, request_of,
    settle_fill, two_values, verdict_of_model_fill,
};

#[test]
fn a_field_named_on_its_own_brings_the_other_person_only_fields_onto_the_sheet() {
    let page = aadhaar_page();
    let (command, _) = request_naming(&mut errand(), page.id);
    assert_eq!(companions_of(&command), ["captcha-answer"]);
    // Naming the CAPTCHA brings the identity number, in page order.
    let (command, _) = request_naming(&mut errand(), page.captcha);
    assert_eq!(companions_of(&command), ["aadhaar-number"]);
}

#[test]
fn a_field_anybody_could_fill_brings_nothing() {
    let (command, _) = request_naming(&mut errand(), aadhaar_page().name);
    assert!(companions_of(&command).is_empty());
}

/// A block brings every field of its page only the person can supply. The
/// browser expands a form itself and asks about these only when the block
/// has no fields of its own, which is the myAadhaar form's case (decision
/// 0243).
#[test]
fn a_block_brings_every_person_only_field_of_its_page() {
    let (command, _) = request_naming(&mut errand(), aadhaar_page().region);
    assert_eq!(
        companions_of(&command),
        ["aadhaar-number", "captcha-answer"]
    );
}

/// The whole of the change, in order: the answer, then each value into the
/// field it was minted for, and only then the model — with no turn requested,
/// recorded or paid for between the answer and the last fill.
#[test]
fn the_persons_values_go_into_their_fields_before_the_model_is_asked_again() {
    let (mut fixture, residency, request) = answered_with_two_values();
    let turns_before = fixture.reducer.next_model_call_id();
    assert_eq!(fixture.state(), TaskState::Running);

    let first = next(&fixture, &residency);
    assert!(
        is_fill_of(first.as_ref(), &request, 0, "aadhaar-number"),
        "expected the fill of value 0, got {first:?}"
    );
    let action_id = apply_fill(&mut fixture, first.expect("the first fill"));
    // In flight: nothing, not a second fill and not a turn.
    assert_eq!(next(&fixture, &residency), None);
    answer_fill(&mut fixture, action_id, 0, ActionResultCode::Verified);
    assert_eq!(
        fixture.reducer.held_values_placed(),
        HeldValuesPlaced::Partly {
            placed: 1,
            count: 2
        }
    );

    let second = next(&fixture, &residency);
    assert!(
        is_fill_of(second.as_ref(), &request, 1, "captcha-answer"),
        "expected the fill of value 1, got {second:?}"
    );
    settle_fill(
        &mut fixture,
        second.expect("the second fill"),
        1,
        ActionResultCode::Verified,
    );

    assert_eq!(
        fixture.reducer.next_model_call_id(),
        turns_before,
        "no model turn was requested between the answer and the fills"
    );
    assert_eq!(
        fixture.reducer.held_values_placed(),
        HeldValuesPlaced::Placed { count: 2 }
    );
    assert!(
        matches!(
            next(&fixture, &residency),
            Some(Command::RequestModelTurn { call_id }) if call_id == turns_before
        ),
        "once every fill has settled, the model is asked"
    );
}

/// A fill that does not go in stops the placing there. The rest are the
/// model's, and the task never tries that position or any after it again.
#[test]
fn a_fill_that_does_not_go_in_hands_the_rest_to_the_model() {
    for refusal in [ActionResultCode::NodeGone, ActionResultCode::DeniedByPolicy] {
        let (mut fixture, residency, request) = answered_with_two_values();
        let first = next(&fixture, &residency).expect("the first fill");
        if refusal == ActionResultCode::DeniedByPolicy {
            let action_id = apply_fill(&mut fixture, first);
            fixture.must_apply(Command::RecordPolicyDecision {
                action_id,
                decision: Box::new(ProposalDecision::Deny(Denial::new(refusal))),
                dispatch_id: None,
            });
        } else {
            settle_fill(&mut fixture, first, 0, refusal);
        }
        assert_eq!(
            fixture.reducer.held_values_placed(),
            HeldValuesPlaced::Partly {
                placed: 0,
                count: 2
            },
            "{refusal:?}"
        );
        assert!(
            matches!(
                next(&fixture, &residency),
                Some(Command::RequestModelTurn { .. })
            ),
            "{refusal:?}: the model is handed the rest"
        );
        assert!(
            !proposed_fill(&fixture, &request, 1),
            "{refusal:?}: the position after a refused fill is never tried by the task"
        );
        // The model did not make this move, so it is not counted against the
        // model: its own fill of the same field is not one step nearer being
        // abandoned.
        assert_eq!(fixture.reducer.refusals().total(), 0, "{refusal:?}");
    }
}

/// A value the task already put into its field is not the model's to fill
/// again: the browser spends a value once, and a second fill of it is refused
/// before it leaves this process rather than a turn later.
#[test]
fn a_placed_value_is_refused_on_sight_when_the_model_fills_it_again() {
    let (mut fixture, residency, _) = answered_with_two_values();
    for index in 0..2 {
        let fill = next(&fixture, &residency).expect("a fill");
        settle_fill(&mut fixture, fill, index, ActionResultCode::Verified);
    }
    assert_eq!(
        verdict_of_model_fill(&mut fixture, &residency, aadhaar_page().id, 0),
        Some(CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected))
    );
}

/// The refusal above is about a value already placed and nothing wider: when
/// the task's own fill did not go in, the same call from the model is the
/// ordinary fill it always was.
#[test]
fn a_value_the_task_could_not_place_is_still_the_models_to_fill() {
    let (mut fixture, residency, _) = answered_with_two_values();
    let first = next(&fixture, &residency).expect("the first fill");
    settle_fill(&mut fixture, first, 0, ActionResultCode::NodeGone);
    assert_ne!(
        verdict_of_model_fill(&mut fixture, &residency, aadhaar_page().id, 0),
        Some(CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected))
    );
}

/// An answer whose list of fields does not have one entry per value is a
/// browser and a core disagreeing about which value goes where, and it is
/// refused rather than repaired.
#[test]
fn an_answer_whose_fields_do_not_match_its_count_is_refused() {
    for fields in [
        &["aadhaar-number"][..],
        &["aadhaar-number", "captcha-answer", "otp"][..],
    ] {
        let mut fixture = errand();
        let (request, _) = request_naming(&mut fixture, aadhaar_page().id);
        let request_id = request_of(&request);
        fixture.must_apply(request);
        let refused = fixture
            .apply(Command::SupplyFieldValues {
                request_id,
                supplied: two_values(),
                outcome: Some(FieldValueAskOutcome::Answered),
                field_node_ids: Some(ids(fields)),
            })
            .expect_err("a mismatched answer is refused");
        assert_eq!(refused.reason, RefusalReason::FieldValueRequestMismatch);
        assert_eq!(fixture.state(), TaskState::WaitingUser, "{fields:?}");
    }
}

/// An answer that names no fields — rebuilt from the journal, which does not
/// record them — is filled by the model, exactly as before.
#[test]
fn an_answer_that_names_no_fields_is_the_models_to_fill() {
    let mut fixture = errand();
    let (request, residency) = request_naming(&mut fixture, aadhaar_page().id);
    let request_id = request_of(&request);
    fixture.must_apply(request);
    fixture.must_apply(Command::SupplyFieldValues {
        request_id,
        supplied: two_values(),
        outcome: None,
        field_node_ids: None,
    });
    assert_eq!(
        fixture.reducer.held_values_placed(),
        HeldValuesPlaced::NotPlaced
    );
    assert!(matches!(
        next(&fixture, &residency),
        Some(Command::RequestModelTurn { .. })
    ));
    assert_eq!(fixture.reducer.actions().count(), 0, "nothing was proposed");
}

/// A task rebuilt part way through placing values keeps what its journalled
/// fill records say — the first value is in — and does not know where the
/// second goes, so that one is the model's and the task proposes nothing more.
#[test]
fn a_task_rebuilt_part_way_through_placing_hands_the_rest_to_the_model() {
    let (mut fixture, residency, request) = answered_with_two_values();
    let first = next(&fixture, &residency).expect("the first fill");
    settle_fill(&mut fixture, first, 0, ActionResultCode::Verified);

    let rebuilt = rebuilt_from_the_store(&fixture);
    assert_eq!(
        rebuilt.held_values_placed(),
        HeldValuesPlaced::Partly {
            placed: 1,
            count: 2
        }
    );
    assert!(matches!(
        rebuilt.next_agent_command(Some(&residency), &Digest),
        Ok(Some(Command::RequestModelTurn { .. }))
    ));
    let key = task_engine::held_value_fill_key(&request, 1);
    assert!(rebuilt
        .actions()
        .all(|action| action.proposal().idempotency_key != key));
}
