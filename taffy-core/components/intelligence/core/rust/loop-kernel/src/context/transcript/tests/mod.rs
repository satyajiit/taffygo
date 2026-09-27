// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the conversation is allowed to be, asserted against a written body.
//!
//! Most of these go through `write_request` rather than stopping at the turn
//! list, because the pairing rule this module exists to hold is enforced
//! there: an unpaired call or result is a refusal, so a body that was written
//! at all is a body whose pairing survived whatever the ladder did.

mod media_probe;
mod opening;
mod overlay;
mod recent;
mod refusal;
mod retired_handles;

use std::collections::BTreeMap;

use model_router::catalog::WireApi;
use model_router::json::{self, JsonValue};
use model_router::thinking::ThinkingPlan;
use model_router::wire::{write_request, WireRequest};
use task_engine::ActionState;

use crate::context::transcript::{
    RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange, MAX_TRANSCRIPT_BYTES,
};

fn call(ordinal: u64, sequence: u32, tool: &str, outcome: ActionState) -> RecordedCall {
    RecordedCall::new(
        format!("turn-{ordinal}-call-{sequence}"),
        tool.to_owned(),
        JsonValue::Object(BTreeMap::new()),
        outcome,
    )
}

fn exchange(ordinal: u64, tools: &[&str]) -> TurnExchange {
    let calls = tools
        .iter()
        .enumerate()
        .map(|(index, tool)| {
            call(
                ordinal,
                u32::try_from(index).unwrap(),
                tool,
                ActionState::Verified,
            )
        })
        .collect();
    TurnExchange::new(ordinal, calls).expect("a turn with calls in it")
}

fn body(transcript: &TaskTranscript, api: WireApi) -> String {
    let views = transcript.views(None, None);
    let turns = views.turns();
    let request = WireRequest {
        model_id: "test-model",
        tool_calling: true,
        system: Some("standing instruction"),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &ThinkingPlan::Disabled,
        answer_tokens: 4096,
        stream: false,
    };
    let mut out = String::new();
    write_request(api, &request, &mut out).expect("a body");
    out
}

/// Every `(role, tool_use id)` and `(role, tool_result id)` an Anthropic body
/// carries, in the order the messages carry them.
fn anthropic_pairs(document: &JsonValue) -> Vec<(String, String, String)> {
    let mut found = Vec::new();
    let messages = document
        .as_object()
        .and_then(|body| body.get("messages"))
        .and_then(JsonValue::as_array)
        .expect("a messages array");
    for message in messages {
        let object = message.as_object().expect("a message object");
        let role = object
            .get("role")
            .and_then(JsonValue::as_str)
            .expect("a role")
            .to_owned();
        let Some(blocks) = object.get("content").and_then(JsonValue::as_array) else {
            continue;
        };
        for block in blocks {
            let block = block.as_object().expect("a content block");
            let kind = block.get("type").and_then(JsonValue::as_str).unwrap_or("");
            let identity = match kind {
                "tool_use" => block.get("id"),
                "tool_result" => block.get("tool_use_id"),
                _ => None,
            };
            if let Some(identity) = identity.and_then(JsonValue::as_str) {
                found.push((role.clone(), kind.to_owned(), identity.to_owned()));
            }
        }
    }
    found
}

#[test]
fn a_two_turn_conversation_pairs_every_call_with_its_result() {
    let transcript = TaskTranscript::new(
        "find the cheapest one".to_owned(),
        vec![
            exchange(0, &["browser.dom.read"]),
            exchange(1, &["browser.dom.click"]),
        ],
        TranscriptBudget::default(),
    );
    let document = json::parse(&body(&transcript, WireApi::AnthropicMessages)).expect("json");
    assert_eq!(
        anthropic_pairs(&document),
        vec![
            (
                "assistant".to_owned(),
                "tool_use".to_owned(),
                "turn-0-call-0".to_owned()
            ),
            (
                "user".to_owned(),
                "tool_result".to_owned(),
                "turn-0-call-0".to_owned()
            ),
            (
                "assistant".to_owned(),
                "tool_use".to_owned(),
                "turn-1-call-0".to_owned()
            ),
            (
                "user".to_owned(),
                "tool_result".to_owned(),
                "turn-1-call-0".to_owned()
            ),
        ]
    );
}

#[test]
fn the_conversation_is_a_different_question_on_every_turn() {
    // The defect this whole module exists to remove: with one fixed turn,
    // turn n + 1 was byte-identical to turn n and the loop could not progress.
    let goal = "find the cheapest one".to_owned();
    let first = TaskTranscript::new(goal.clone(), Vec::new(), TranscriptBudget::default());
    let second = TaskTranscript::new(
        goal,
        vec![exchange(0, &["browser.dom.read"])],
        TranscriptBudget::default(),
    );
    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage,
    ] {
        assert_ne!(body(&first, api), body(&second, api), "{api:?}");
    }
}

#[test]
fn eviction_drops_a_call_and_its_result_together_and_never_one_alone() {
    // A budget that admits the goal, the reserve and one exchange only.
    let goal = "find the cheapest one".to_owned();
    let kept = exchange(2, &["browser.dom.click"]);
    let budget = TranscriptBudget::new(goal.len() + kept.calls()[0].tool().len() + 64);
    let transcript = TaskTranscript::new(
        goal,
        vec![
            exchange(0, &["browser.dom.read"]),
            exchange(1, &["browser.dom.read"]),
            kept,
        ],
        budget,
    );
    assert_eq!(transcript.elided(), 2);
    assert_eq!(transcript.exchanges().len(), 1);
    assert_eq!(transcript.exchanges()[0].ordinal(), 2);

    let text = body(&transcript, WireApi::AnthropicMessages);
    let document = json::parse(&text).expect("json");
    // The surviving turn keeps both halves, and the dropped turns took both
    // of theirs. A ladder that dropped a `Called` and left its `Returned`
    // would not reach here at all: `write_request` refuses that body.
    assert_eq!(
        anthropic_pairs(&document),
        vec![
            (
                "assistant".to_owned(),
                "tool_use".to_owned(),
                "turn-2-call-0".to_owned()
            ),
            (
                "user".to_owned(),
                "tool_result".to_owned(),
                "turn-2-call-0".to_owned()
            ),
        ]
    );
    for dropped in ["turn-0-call-0", "turn-1-call-0"] {
        assert!(!text.contains(dropped), "{dropped} survived eviction");
    }
}

#[test]
fn a_turn_with_several_calls_keeps_or_drops_all_of_them() {
    // The pairing rule is per exchange, not per call: a ladder that fitted
    // "some of turn 0" would leave calls with no results in the same turn.
    let goal = "compare the two".to_owned();
    let budget = TranscriptBudget::new(goal.len() + 300);
    let transcript = TaskTranscript::new(
        goal,
        vec![
            exchange(0, &["browser.dom.read", "browser.dom.read"]),
            exchange(1, &["browser.dom.click", "browser.dom.read"]),
        ],
        budget,
    );
    for exchange in transcript.exchanges() {
        assert_eq!(exchange.calls().len(), 2, "a turn was split");
    }
    let document = json::parse(&body(&transcript, WireApi::AnthropicMessages)).expect("json");
    let pairs = anthropic_pairs(&document);
    let calls: Vec<&String> = pairs
        .iter()
        .filter(|(_, kind, _)| kind == "tool_use")
        .map(|(_, _, id)| id)
        .collect();
    let results: Vec<&String> = pairs
        .iter()
        .filter(|(_, kind, _)| kind == "tool_result")
        .map(|(_, _, id)| id)
        .collect();
    assert_eq!(calls, results);
    assert!(!calls.is_empty());
}

#[test]
fn the_elision_line_appears_exactly_when_something_was_dropped() {
    let goal = "find the cheapest one".to_owned();
    let exchanges = vec![
        exchange(0, &["browser.dom.read"]),
        exchange(1, &["browser.dom.read"]),
        exchange(2, &["browser.dom.click"]),
    ];
    let roomy = TaskTranscript::new(goal.clone(), exchanges.clone(), TranscriptBudget::default());
    assert_eq!(roomy.elided(), 0);
    assert_eq!(roomy.elision_notice(), None);
    assert!(!body(&roomy, WireApi::AnthropicMessages).contains("earlier in this task"));

    let cramped = TaskTranscript::new(goal, exchanges, TranscriptBudget::new(1));
    assert_eq!(cramped.elided(), 2);
    let notice = cramped.elision_notice().expect("a line about what went");
    assert!(notice.contains(" 2 "), "{notice}");
    assert!(body(&cramped, WireApi::AnthropicMessages).contains(notice));
}

#[test]
fn the_line_says_how_many_went_and_the_number_is_the_number() {
    let exchanges: Vec<TurnExchange> = (0..7).map(|n| exchange(n, &["browser.dom.read"])).collect();
    let transcript = TaskTranscript::new("goal".to_owned(), exchanges, TranscriptBudget::new(1));
    assert_eq!(transcript.elided(), 6);
    let notice = transcript.elision_notice().expect("a line");
    // Not "contains a digit": the count itself, so a line that always said
    // one, or said the number it kept, fails here.
    assert!(notice.contains(" 6 "), "{notice}");
    assert!(!notice.contains(" 1 "), "{notice}");
}

#[test]
fn the_goal_survives_eviction_however_little_room_there_is() {
    let goal = "book the earliest flight that arrives before noon".to_owned();
    let exchanges: Vec<TurnExchange> = (0..9).map(|n| exchange(n, &["browser.dom.read"])).collect();
    let transcript = TaskTranscript::new(goal.clone(), exchanges, TranscriptBudget::new(0));
    assert_eq!(transcript.goal(), goal);
    assert_eq!(transcript.exchanges().len(), 1, "the newest turn went too");
    assert!(body(&transcript, WireApi::AnthropicMessages).contains(&goal));
}

#[test]
fn a_transcript_at_the_bound_does_not_exceed_it() {
    let goal = "find the cheapest one".to_owned();
    let budget = TranscriptBudget::new(goal.len() + 400);
    // Far more material than the budget admits, so the ladder has to run.
    let exchanges: Vec<TurnExchange> = (0..40)
        .map(|n| exchange(n, &["browser.dom.read", "browser.dom.click"]))
        .collect();
    let transcript = TaskTranscript::new(goal, exchanges, budget);
    assert!(transcript.elided() > 0, "nothing was dropped");
    assert!(
        transcript.text_bytes() <= budget.bytes(),
        "{} bytes over a {} byte budget",
        transcript.text_bytes(),
        budget.bytes()
    );
    // And the line explaining the drop is inside the bound rather than added
    // to it: the reserve is why this holds.
    assert!(transcript.elision_notice().is_some());
}

#[test]
fn the_page_is_a_piece_of_the_opening_turn() {
    // The mutation this exists to kill: stuffing the page into a second User
    // turn. Two consecutive same-role turns are a shape two of the four
    // families refuse, which is why the projection sits beside the goal.
    let transcript = TaskTranscript::new(
        "download it".to_owned(),
        vec![exchange(1, &["browser.dom.read"])],
        TranscriptBudget::default(),
    );
    let page = "[0] button \"Download\" — can activate\n";
    let views = transcript.views(Some(page), None);
    let turns = views.turns();
    let Some(model_router::wire::Turn::Said { text, .. }) = turns.first() else {
        panic!("the conversation opens with the person's turn");
    };
    assert_eq!(text.first().copied(), Some("download it"));
    assert_eq!(text.last().copied(), Some(page));
    assert!(matches!(
        turns.get(1),
        Some(model_router::wire::Turn::Called { .. })
    ));
}

#[test]
fn a_turn_that_called_nothing_is_not_a_turn() {
    // `WireRefusal::EmptyToolTurn` is what a body would answer. The shape is
    // removed here instead, so nothing downstream has to.
    assert!(TurnExchange::new(0, Vec::new()).is_none());
    let too_many: Vec<RecordedCall> = (0..=model_router::wire::reply::MAX_TOOL_CALLS)
        .map(|index| {
            call(
                0,
                u32::try_from(index).unwrap(),
                "browser.dom.read",
                ActionState::Verified,
            )
        })
        .collect();
    assert!(TurnExchange::new(0, too_many).is_none());
}

#[test]
fn a_failed_call_reaches_the_wire_as_a_failure_and_a_verified_one_does_not() {
    let goal = "submit the form".to_owned();
    let failed = TurnExchange::new(
        0,
        vec![call(0, 0, "browser.dom.click", ActionState::Failed)],
    )
    .expect("a turn");
    let done = TurnExchange::new(
        1,
        vec![call(1, 0, "browser.dom.read", ActionState::Verified)],
    )
    .expect("a turn");
    let transcript = TaskTranscript::new(goal, vec![failed, done], TranscriptBudget::default());
    let text = body(&transcript, WireApi::AnthropicMessages);
    let document = json::parse(&text).expect("json");
    let flags: Vec<bool> = document
        .as_object()
        .and_then(|body| body.get("messages"))
        .and_then(JsonValue::as_array)
        .expect("messages")
        .iter()
        .filter_map(|message| message.as_object()?.get("content")?.as_array())
        .flatten()
        .filter_map(|block| {
            let block = block.as_object()?;
            if block.get("type")?.as_str()? != "tool_result" {
                return None;
            }
            Some(matches!(block.get("is_error"), Some(JsonValue::Bool(true))))
        })
        .collect();
    assert_eq!(flags, vec![true, false]);
    assert!(text.contains("did not happen"));
    assert!(text.contains("done, and checked"));
}

#[test]
fn the_default_budget_is_the_compiled_in_bound_and_not_a_second_number() {
    // The relationship between the bound and the contract's goal ceiling is a
    // `const` assertion beside the constant, so lowering it is a build error
    // rather than a test failure. What is left to check here is that the
    // default budget is that constant, because a second literal is how a
    // bound gets two values.
    assert_eq!(TranscriptBudget::default().bytes(), MAX_TRANSCRIPT_BYTES);
}
