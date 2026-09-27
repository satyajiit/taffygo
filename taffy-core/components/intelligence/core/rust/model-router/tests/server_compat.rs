// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a detected server's overrides change about a request and a reply.
//!
//! The completions dialect is the one a person's own server speaks, and it is
//! the one row with somewhere to put what such a server needs. Every
//! assertion here is a failure that used to be invisible: a body the server
//! accepted and answered with the wrong length, or a reply refused for a field
//! that server never sends.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::json::{parse, JsonValue};
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{Speaker, Turn, WireRequest};
use model_router::wire::{detect, read_reply, write_request, ReplyContext, ServerKind};

fn asking() -> [Turn<'static>; 1] {
    const PIECES: [&str; 1] = ["what does this page say"];
    [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }]
}

fn body(request: &WireRequest<'_>) -> JsonValue {
    let mut out = String::new();
    write_request(WireApi::OpenAiCompletions, request, &mut out)
        .expect("the dialect writes a body");
    parse(&out).expect("the body it wrote is JSON")
}

#[test]
fn a_self_hosted_server_is_told_the_allowance_under_the_name_it_knows() {
    let plan = common::plan_for(WireApi::OpenAiCompletions);
    let turns = asking();
    let request = WireRequest {
        compat: Some(detect(ServerKind::Ollama)),
        ..common::wire_request(&plan, &turns)
    };
    let document = body(&request);
    assert_eq!(
        document.field("max_tokens").and_then(JsonValue::as_i64),
        Some(2_000)
    );
    assert!(
        document.field("max_completion_tokens").is_none(),
        "sent under a name the server does not read, the allowance is simply ignored"
    );
    assert!(
        document.field("reasoning_effort").is_none(),
        "a server that does not know the field may refuse the whole request over it"
    );
}

#[test]
fn the_platform_dialect_is_unchanged_by_a_server_that_speaks_it() {
    let plan = common::plan_for(WireApi::OpenAiCompletions);
    let turns = asking();
    let request = WireRequest {
        compat: Some(detect(ServerKind::OpenAiCompatible)),
        ..common::wire_request(&plan, &turns)
    };
    let document = body(&request);
    assert_eq!(
        document
            .field("max_completion_tokens")
            .and_then(JsonValue::as_i64),
        Some(2_000)
    );
    assert_eq!(
        document
            .field("reasoning_effort")
            .and_then(JsonValue::as_str),
        Some("medium")
    );
}

#[test]
fn a_chat_template_argument_and_a_sampling_parameter_reach_the_body() {
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let template = [("enable_thinking", "false")];
    let sampling = [("temperature", "0.2"), ("top_p", "0.9")];
    let compat = model_router::wire::ServerCompat {
        template_kwargs: &template,
        sampling: &sampling,
        ..detect(ServerKind::Vllm)
    };
    let request = WireRequest {
        compat: Some(compat),
        ..common::wire_request(&plan, &turns)
    };
    let document = body(&request);
    assert_eq!(
        document
            .field("chat_template_kwargs")
            .and_then(|kwargs| kwargs.field("enable_thinking"))
            .and_then(JsonValue::as_str),
        Some("false")
    );
    assert_eq!(
        document.field("temperature").and_then(JsonValue::as_str),
        Some("0.2")
    );
    assert_eq!(
        document.field("top_p").and_then(JsonValue::as_str),
        Some("0.9")
    );
}

#[test]
fn a_family_with_no_compat_row_writes_none_of_it() {
    // A row that names no place for these fields writes none of them, whatever
    // the caller attached: naming another dialect's field in a body is how a
    // request reaches a provider that rejects it for a reason nobody can read.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let template = [("enable_thinking", "false")];
    let compat = model_router::wire::ServerCompat {
        template_kwargs: &template,
        ..detect(ServerKind::Vllm)
    };
    let request = WireRequest {
        compat: Some(compat),
        ..common::wire_request(&plan, &turns)
    };
    let mut out = String::new();
    write_request(WireApi::AnthropicMessages, &request, &mut out).expect("a body");
    let document = parse(&out).unwrap();
    assert!(document.field("chat_template_kwargs").is_none());
}

fn context(reports_finish_reason: bool) -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([6; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason,
    }
}

/// A reply from a server that answered and said nothing about why it stopped.
const NO_FINISH_REASON: &str = r#"{"choices":[{"message":{"content":"the answer"}}],
    "usage":{"prompt_tokens":100,"completion_tokens":20}}"#;

#[test]
fn a_server_that_never_says_why_it_stopped_is_still_readable() {
    let reply = parse(NO_FINISH_REASON).unwrap();
    let outcome =
        read_reply(WireApi::OpenAiCompletions, &reply, &context(false)).expect("the reply reads");
    assert_eq!(
        outcome.result.stop,
        model_router::request::StopReason::Complete
    );
    assert_eq!(
        outcome.result.provider_stop_reason, None,
        "no word was sent, so none is quoted"
    );
    assert_eq!(outcome.reading.text, vec!["the answer"]);
}

#[test]
fn a_server_that_does_say_is_still_refused_when_it_does_not() {
    // The refusal stays where it matters: on a server that reports a stop word,
    // a reply without one is a reply whose ending is unknown, and reading it as
    // an ordinary finish is how a truncated answer becomes a wrong result.
    let reply = parse(NO_FINISH_REASON).unwrap();
    assert!(read_reply(WireApi::OpenAiCompletions, &reply, &context(true)).is_err());
}
