// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Two shapes of a tool-calling reply on the completions family, which is the
//! family the `xai` row speaks: several calls in one reply, and a reply cut
//! off at its token allowance in the middle of a call.
//!
//! Both are read here so the loop kernel's rows 17 and 21 rest on a reader
//! that reports what the provider sent: every call, in the provider's order,
//! and a `Length` stop that keeps the partial call visible rather than
//! dropping it or dressing it as complete.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::json::parse;
use model_router::request::StopReason;
use model_router::wire::{read_reply, Arguments, ReplyContext};

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([5; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

#[test]
fn two_calls_in_one_reply_are_read_in_the_order_the_provider_declared_them() {
    let reply = parse(
        r#"{"choices":[{"finish_reason":"tool_calls","message":{"tool_calls":[
                {"id":"call_1","type":"function",
                 "function":{"name":"browser.search","arguments":"{\"query\":\"official site\"}"}},
                {"id":"call_2","type":"function",
                 "function":{"name":"browser.navigate","arguments":"{\"address\":\"https://a.example/\"}"}}
            ]}}],
            "usage":{"prompt_tokens":100,"completion_tokens":30}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::OpenAiCompletions, &reply, &context()).expect("reads");
    assert_eq!(outcome.result.stop, StopReason::ToolCall);
    let names: Vec<&str> = outcome
        .reading
        .tool_calls
        .iter()
        .map(|call| call.tool)
        .collect();
    assert_eq!(names, ["browser.search", "browser.navigate"]);
    let ids: Vec<Option<&str>> = outcome
        .reading
        .tool_calls
        .iter()
        .map(|call| call.call_id)
        .collect();
    assert_eq!(ids, [Some("call_1"), Some("call_2")]);
    // The family embeds arguments as a JSON document in a string, handed on
    // unparsed; the arena parses it under its own bound.
    for call in &outcome.reading.tool_calls {
        assert!(
            matches!(call.arguments, Arguments::Text(_)),
            "{}",
            call.tool
        );
    }
}

#[test]
fn a_reply_cut_off_inside_a_call_is_a_length_stop_that_keeps_the_partial_call() {
    // The arguments string ends mid-document: the provider stopped at the
    // allowance. Row 17 of the agent table refuses every call of such a reply
    // as truncated, which it can only do if the reader reports the stop as
    // `Length` and still hands the call on.
    let reply = parse(
        r#"{"choices":[{"finish_reason":"length","message":{"tool_calls":[
                {"id":"call_1","type":"function",
                 "function":{"name":"browser.navigate","arguments":"{\"address\":\"https://a.exa"}}
            ]}}],
            "usage":{"prompt_tokens":100,"completion_tokens":2000}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::OpenAiCompletions, &reply, &context()).expect("reads");
    assert_eq!(outcome.result.stop, StopReason::Length);
    assert_eq!(outcome.reading.tool_calls.len(), 1);
    let call = outcome.reading.tool_calls[0];
    assert_eq!(call.tool, "browser.navigate");
    let Arguments::Text(text) = call.arguments else {
        panic!("the family embeds arguments as text");
    };
    assert!(
        text.ends_with("a.exa"),
        "the partial document is handed on as it arrived"
    );
    assert!(outcome.result.error.is_none());
}
