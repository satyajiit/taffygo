// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which of the model's own numbers go back in front of it.
//!
//! The transcript replays sixteen turns of arguments, so a task that has left
//! a page carries that page's numbers in the model's own voice long after the
//! projection that issued them is gone. Errand `f8356d76` named one on 14 of
//! its 43 turns (decision 0222).

use std::collections::BTreeMap;

use model_router::json::JsonValue;
use task_engine::ActionState;

use crate::context::transcript::{RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};

/// The sentence a call is given when its number is taken out of it.
const RETIRED: &str = "the number this named is gone";

fn arguments(pairs: &[(&str, JsonValue)]) -> JsonValue {
    JsonValue::Object(
        pairs
            .iter()
            .map(|(name, value)| ((*name).to_owned(), value.clone()))
            .collect::<BTreeMap<_, _>>(),
    )
}

fn transcript_naming(tool: &str, arguments: JsonValue) -> TaskTranscript {
    let call = RecordedCall::new(
        "turn-1-call-0".to_owned(),
        tool.to_owned(),
        arguments,
        ActionState::Verified,
    );
    TaskTranscript::new(
        "download my eAadhaar".to_owned(),
        vec![TurnExchange::new(1, vec![call]).expect("a turn with a call in it")],
        TranscriptBudget::new(64 * 1024),
    )
}

fn only_call(transcript: &TaskTranscript) -> (JsonValue, Vec<String>) {
    let views = transcript.views(None, None);
    let exchange = views.exchanges.first().expect("one exchange");
    let call = exchange.calls.first().expect("one call");
    let result = exchange.results.first().expect("one result");
    (call.arguments.clone(), result.text.to_vec())
}

/// A number the live pages will not honour does not go back to the model.
#[test]
fn a_number_the_page_will_not_honour_is_taken_out_and_said() {
    let mut transcript = transcript_naming(
        "browser.dom.click",
        arguments(&[("node", JsonValue::Integer(47))]),
    );

    transcript.retire_unusable_handles(&|_| false);

    let (arguments, result) = only_call(&transcript);
    assert_eq!(arguments, JsonValue::Object(BTreeMap::new()));
    assert!(
        result.iter().any(|line| line.contains(RETIRED)),
        "the call says its number is gone: {result:?}"
    );
}

/// A number that still works is left exactly as it was.
///
/// This is the reason the arguments are replayed at all: without them a model
/// that had just found the right page began the next turn looking for it.
#[test]
fn a_number_the_page_still_honours_is_left_alone() {
    let mut transcript = transcript_naming(
        "browser.dom.click",
        arguments(&[("node", JsonValue::Integer(47))]),
    );

    transcript.retire_unusable_handles(&|value| value == 47);

    let (arguments, result) = only_call(&transcript);
    assert_eq!(arguments, arguments_with_node(47));
    assert!(
        !result.iter().any(|line| line.contains(RETIRED)),
        "and says nothing about it: {result:?}"
    );
}

/// Only the handle goes. Everything else the call carried is still what it did.
#[test]
fn what_is_not_a_handle_survives_beside_a_dropped_one() {
    let mut transcript = transcript_naming(
        "browser.form.fill",
        arguments(&[
            ("field", JsonValue::Integer(12)),
            (
                "value",
                JsonValue::Text("the one the person gave".to_owned()),
            ),
        ]),
    );

    transcript.retire_unusable_handles(&|_| false);

    let (arguments, _) = only_call(&transcript);
    assert_eq!(
        arguments,
        JsonValue::Object(
            [(
                "value".to_owned(),
                JsonValue::Text("the one the person gave".to_owned()),
            )]
            .into_iter()
            .collect::<BTreeMap<_, _>>()
        ),
    );
}

/// A tool with no handle parameter is never touched, whatever it carries.
///
/// `browser.search` takes words, and a number among them is a number the
/// person or the model meant — not a node this ever issued.
#[test]
fn a_tool_that_takes_no_handle_keeps_every_argument() {
    let original = arguments(&[("query", JsonValue::Text("eAadhaar download 12".to_owned()))]);
    let mut transcript = transcript_naming("browser.search", original.clone());

    transcript.retire_unusable_handles(&|_| false);

    let (arguments, result) = only_call(&transcript);
    assert_eq!(arguments, original);
    assert!(!result.iter().any(|line| line.contains(RETIRED)));
}

fn arguments_with_node(value: i64) -> JsonValue {
    arguments(&[("node", JsonValue::Integer(value))])
}
