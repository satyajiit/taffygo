// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The next turn is told what came of a request for values (decisions 0192
//! and 0215).

use crate::context::opening::values_answer;
use task_engine::{field_value_request_id_for_call, FieldValueAskOutcome, SuppliedValueCount};

use super::*;

fn answered(supplied: u32) -> Vec<String> {
    answered_with(supplied, FieldValueAskOutcome::Answered)
}

fn answered_with(supplied: u32, outcome: FieldValueAskOutcome) -> Vec<String> {
    answered_naming(supplied, outcome, None)
}

fn answered_naming(
    supplied: u32,
    outcome: FieldValueAskOutcome,
    field_node_ids: Option<task_engine::FieldNodeIds>,
) -> Vec<String> {
    let mut reducer = running_reducer();
    let request_id = field_value_request_id_for_call(0, 0);
    apply(
        &mut reducer,
        Command::RequestFieldValues {
            request_id: request_id.clone(),
            tab_id: bip_types::identity::TabId::new("tab-1"),
            node_id: bip_types::identity::SemanticNodeId::new("form-1"),
            companion_node_ids: task_engine::FieldNodeIds::none(),
        },
        "request-values",
    );
    apply(
        &mut reducer,
        Command::SupplyFieldValues {
            outcome: Some(outcome),
            request_id,
            supplied: SuppliedValueCount::new(supplied).expect("count"),
            field_node_ids,
        },
        "supply-values",
    );
    reducer.model_turn_facts().transcript.preface().to_vec()
}

#[test]
fn an_answer_is_in_the_next_turns_opening() {
    assert!(answered(2).contains(&values_answer(2, Some(FieldValueAskOutcome::Answered))));
}

/// The browser named a field for each value, and this task may not fill a
/// field at all: nothing is placed for it, and the next turn reads exactly as
/// it did before the task could place values (decision 0238).
#[test]
fn an_answer_a_task_may_not_place_reads_as_it_always_has() {
    let fields = task_engine::FieldNodeIds::new(vec![
        bip_types::identity::SemanticNodeId::new("aadhaar-number"),
        bip_types::identity::SemanticNodeId::new("captcha-answer"),
    ])
    .expect("two fields");
    let preface = answered_naming(2, FieldValueAskOutcome::Answered, Some(fields));
    assert!(preface.contains(&values_answer(2, Some(FieldValueAskOutcome::Answered))));
}

#[test]
fn an_answer_of_nothing_is_in_the_next_turns_opening_too() {
    assert!(answered(0).contains(&values_answer(0, Some(FieldValueAskOutcome::Answered))));
}

/// The whole crossing, for the one outcome it was built for.
///
/// This is the end-to-end half of decision 0215: a browser that abandoned an
/// ask because the challenge picture was below the fold reaches the model's
/// next turn as an instruction to scroll and ask again, rather than as the
/// count `0`. Everything between is the reducer and the transcript, which is
/// what makes it worth asserting here rather than only over `values_answer`.
#[test]
fn a_challenge_below_the_fold_reaches_the_next_turn_as_a_scroll() {
    let preface = answered_with(0, FieldValueAskOutcome::ChallengeOffScreen);
    assert!(
        preface
            .iter()
            .any(|line| line.contains("browser.dom.scroll")),
        "the next turn was not told to scroll: {preface:?}"
    );
}

#[test]
fn an_errand_that_never_asked_is_told_nothing_about_values() {
    let reducer = running_reducer();
    let preface = reducer.model_turn_facts().transcript.preface().to_vec();
    assert!(!preface
        .iter()
        .any(|line| line.contains("last user.request_values")));
}
