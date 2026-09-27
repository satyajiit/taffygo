// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A refused call reaches the model as a result it can act on.

use std::collections::BTreeMap;

use bip_types::ActionResultCode;
use model_router::json::JsonValue;
use model_router::wire::Turn;
use task_engine::ActionState;

use crate::context::refusal::refusal_sentence;
use crate::context::transcript::{RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};

fn refused(code: ActionResultCode) -> RecordedCall {
    RecordedCall::refused(
        "turn-0-call-0".to_owned(),
        "browser.navigate".to_owned(),
        JsonValue::Object(BTreeMap::new()),
        ActionState::Rejected,
        code,
    )
}

#[test]
fn a_refused_call_carries_the_word_and_the_sentence_as_an_error_result() {
    let call = refused(ActionResultCode::EgressNotAuthorized);
    assert_eq!(call.outcome(), ActionState::Rejected);
    let Some(exchange) = TurnExchange::new(0, vec![call]) else {
        panic!("one call is a writable turn");
    };
    let transcript = TaskTranscript::new(
        "goal".to_owned(),
        vec![exchange],
        TranscriptBudget::default(),
    );
    let views = transcript.views(None, None);
    let turns = views.turns();
    let Some(Turn::Returned { results }) = turns.get(2) else {
        panic!("the exchange ends in the result that answers it");
    };
    let Some(result) = results.first() else {
        panic!("one result");
    };
    assert!(result.is_error);
    assert_eq!(
        result.text,
        [
            "refused".to_owned(),
            refusal_sentence(ActionResultCode::EgressNotAuthorized).to_owned(),
        ]
    );
}

#[test]
fn the_sentence_is_counted_in_the_material_the_ladder_spends() {
    let bare = RecordedCall::new(
        "turn-0-call-0".to_owned(),
        "browser.navigate".to_owned(),
        JsonValue::Object(BTreeMap::new()),
        ActionState::Rejected,
    );
    let (Some(plain), Some(said)) = (
        TurnExchange::new(0, vec![bare]),
        TurnExchange::new(0, vec![refused(ActionResultCode::BudgetExceeded)]),
    ) else {
        panic!("one call is a writable turn");
    };
    let plain = TaskTranscript::new("goal".to_owned(), vec![plain], TranscriptBudget::default());
    let said = TaskTranscript::new("goal".to_owned(), vec![said], TranscriptBudget::default());
    assert_eq!(
        said.text_bytes(),
        plain.text_bytes() + refusal_sentence(ActionResultCode::BudgetExceeded).len()
    );
}
