// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one family whose request is nested and whose reply is wrapped.
//!
//! A file of its own rather than more of `subscription_envelope.rs`, which is
//! about the shape one endpoint insists a *replayed turn* wears. This is about
//! an envelope around the whole exchange: routing fields the endpoint reads
//! sit outside it, everything the model sees sits inside it, and the reply
//! comes back inside another.
//!
//! Every rule here fails the same way when it is broken, and that way is the
//! reason the file exists. A body written flat is refused by the endpoint. A
//! reply read flat is not refused by anything: the paths find no candidates
//! and no counts, and what a person sees is a model that answered with
//! nothing — which is indistinguishable from a model that had nothing to say.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::json::{parse, parse_provider, JsonValue};
use model_router::request::{ErrorClass, StopReason};
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{
    Speaker, ToolCallReplay, ToolDeclaration, ToolResultView, Turn, WireRequest,
};
use model_router::wire::{fold_stream, read_reply, write_request, ReplyContext, StreamDefect};

const API: WireApi = WireApi::GoogleCloudCodeAssist;

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([9; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

fn asking() -> [Turn<'static>; 1] {
    const PIECES: [&str; 1] = ["what does this page say"];
    [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }]
}

fn body(request: &WireRequest<'_>) -> JsonValue {
    let mut out = String::new();
    write_request(API, request, &mut out).expect("the family writes a body");
    parse(&out).expect("the body it wrote is JSON")
}

// --- the request envelope ----------------------------------------------------

#[test]
fn the_routing_fields_sit_outside_the_envelope_and_the_conversation_inside_it() {
    // The split is the whole family. `model`, `requestType` and `userAgent`
    // are read by the endpoint to decide where the request goes and which
    // client sent it; everything under `request` is what reaches a model. A
    // body that put the two on one level is refused, and the refusal names
    // neither field.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let document = body(&common::wire_request(&plan, &turns));

    assert_eq!(
        document.field("model").and_then(JsonValue::as_str),
        Some("a-model"),
        "the model is named at the envelope's top, not in the path"
    );
    assert_eq!(
        document.field("requestType").and_then(JsonValue::as_str),
        Some("agent")
    );
    assert_eq!(
        document.field("userAgent").and_then(JsonValue::as_str),
        Some("antigravity")
    );
    assert!(
        document.field("contents").is_none(),
        "the conversation is inside the envelope, not beside it"
    );

    let request = document.field("request").expect("the envelope is written");
    assert!(request.field("contents").is_some());
    assert!(request.field("systemInstruction").is_some());
    assert_eq!(
        request
            .field("generationConfig")
            .and_then(|config| config.field("maxOutputTokens"))
            .and_then(JsonValue::as_i64),
        Some(2_000)
    );
    assert!(
        request.field("model").is_none(),
        "the routing fields are not repeated inside"
    );
}

#[test]
fn the_instruction_arrives_as_the_users_with_the_endpoints_own_text_ahead_of_it() {
    // Two constant pieces and then whatever the caller supplied, under
    // `role: "user"`. The first piece is what the endpoint inspects; the
    // second is what stops the model from adopting it. They ship together or
    // not at all — the first alone would leave a second assistant identity
    // standing in the instruction, which decision 0009 forbids.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let document = body(&common::wire_request(&plan, &turns));
    let instruction = document
        .field("request")
        .and_then(|request| request.field("systemInstruction"))
        .expect("the instruction is written");

    assert_eq!(
        instruction.field("role").and_then(JsonValue::as_str),
        Some("user"),
        "this endpoint refuses the spelling the wrapped family omits entirely"
    );
    let parts = instruction
        .field("parts")
        .and_then(JsonValue::as_array)
        .expect("the parts are an array");
    assert_eq!(parts.len(), 3, "two constant pieces, then the caller's");
    let text = |index: usize| {
        parts[index]
            .field("text")
            .and_then(JsonValue::as_str)
            .expect("every part carries text")
    };
    assert!(text(0).starts_with("You are Antigravity,"));
    assert!(
        text(1).starts_with("Please ignore following [ignore]") && text(1).ends_with("[/ignore]"),
        "the neutraliser wraps the same text it neutralises"
    );
    assert!(
        text(1).contains(text(0)),
        "one spelling of the token, wrapped, rather than two that could drift"
    );
    assert_eq!(text(2), "Be brief.");
}

#[test]
fn the_endpoints_own_text_is_written_even_when_the_caller_supplies_no_instruction() {
    // The token marks which client is calling, so its absence is what the
    // endpoint reads. A row that wrote it only alongside a caller's
    // instruction would send an unmarked request on every turn that had none.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let mut request = common::wire_request(&plan, &turns);
    request.system = None;
    let document = body(&request);
    let parts = document
        .field("request")
        .and_then(|request| request.field("systemInstruction"))
        .and_then(|instruction| instruction.field("parts"))
        .and_then(JsonValue::as_array)
        .expect("the instruction is written all the same");
    assert_eq!(parts.len(), 2);
}

#[test]
fn a_replayed_call_and_its_result_both_carry_the_identity_inside_the_wrapper() {
    // The endpoint translates this request into another vendor's messages for
    // the models of a third, and a tool call there is refused without an
    // identity. Written on the part instead of inside the wrapper it is
    // silently absent: the request is accepted and the conversation fails
    // several turns later, when a result first has to be paired with its call.
    let plan = ThinkingPlan::Disabled;
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &arguments,
    }];
    let answer = ["a headline".to_owned()];
    let turns = [
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &[],
        },
        Turn::Returned {
            results: &[ToolResultView {
                call_id: "turn-12-call-3",
                tool: "read_page",
                is_error: false,
                text: &answer,
            }],
        },
    ];
    let schema = parse(r#"{"type":"object","properties":{}}"#).unwrap();
    let tools = [ToolDeclaration {
        name: "read_page",
        description: "Read the page.",
        parameters: &schema,
    }];
    let request = WireRequest {
        model_id: "claude-sonnet-4-6",
        tool_calling: true,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &tools,
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let document = body(&request);
    let contents = document
        .field("request")
        .and_then(|request| request.field("contents"))
        .and_then(JsonValue::as_array)
        .expect("the conversation is inside the envelope");

    let called = contents[0]
        .field("parts")
        .and_then(JsonValue::as_array)
        .and_then(|parts| parts.first())
        .expect("the call is a part of the turn that made it");
    assert!(
        called.field("id").is_none(),
        "the identity is the call's, not the part's"
    );
    let call = called.field("functionCall").expect("the wrapper");
    assert_eq!(
        call.field("id").and_then(JsonValue::as_str),
        Some("turn-12-call-3")
    );
    assert_eq!(
        call.field("name").and_then(JsonValue::as_str),
        Some("read_page")
    );

    let returned = contents[1]
        .field("parts")
        .and_then(JsonValue::as_array)
        .and_then(|parts| parts.first())
        .expect("the result is a part of the turn that carried it");
    assert!(returned.field("id").is_none());
    let result = returned.field("functionResponse").expect("the wrapper");
    assert_eq!(
        result.field("id").and_then(JsonValue::as_str),
        Some("turn-12-call-3"),
        "a result the endpoint cannot pair is a result it discards"
    );
    assert_eq!(
        result.field("name").and_then(JsonValue::as_str),
        Some("read_page"),
        "the name is kept beside the identity, because the shape it wraps pairs by name"
    );
}

// --- the reply envelope ------------------------------------------------------

#[test]
fn a_wrapped_reply_reads_its_answer_its_calls_and_its_counts() {
    let reply = parse_provider(
        r#"{"response":{
             "candidates":[{"finishReason":"STOP","content":{"role":"model","parts":[
                 {"text":"the answer"},
                 {"functionCall":{"name":"read_page","args":{"tab":1}}}]}}],
             "usageMetadata":{"promptTokenCount":150,"candidatesTokenCount":20,
                              "cachedContentTokenCount":40}}}"#,
    )
    .expect("the reply parses");
    let outcome = read_reply(API, &reply, &context()).expect("the reply reads");
    assert_eq!(outcome.reading.text, vec!["the answer"]);
    assert_eq!(outcome.reading.tool_calls.len(), 1);
    assert_eq!(outcome.reading.tool_calls[0].tool, "read_page");
    assert_eq!(
        outcome.reading.tool_calls[0].call_id, None,
        "the identity a caller pairs on is the one it minted and already sent"
    );
    assert_eq!(outcome.result.usage.total_input(), 150);
    assert_eq!(outcome.result.usage.output, 20);
    assert_eq!(outcome.result.usage.cache_read, 40);
    assert_eq!(
        outcome.result.stop,
        StopReason::ToolCall,
        "a reply carrying a call stopped to make it, whatever word the vendor used"
    );
}

#[test]
fn the_family_this_one_wraps_cannot_read_a_wrapped_reply() {
    // Why these are two rows rather than one row at two addresses. The flat
    // row finds no candidates, no counts and — because it finds no stop word
    // either — refuses outright. That refusal is luck rather than design: it
    // comes from a rule about stop words, not from anything noticing the
    // envelope, and a *streamed* frame carries no stop word on most frames and
    // is not refused for the lack of one. Which is why the stream arm requires
    // the envelope itself; see the next test.
    let reply = parse_provider(
        r#"{"response":{
             "candidates":[{"finishReason":"STOP","content":{"parts":[
                 {"text":"the answer"}]}}],
             "usageMetadata":{"promptTokenCount":150,"candidatesTokenCount":20}}}"#,
    )
    .expect("the reply parses");
    let reading = read_reply(WireApi::GoogleGenerativeLanguage, &reply, &context());
    assert!(
        reading.is_err(),
        "the flat row read an envelope it has no path into"
    );
}

#[test]
fn a_failure_is_read_from_outside_the_envelope() {
    // The one reply location that is not wrapped: a request that failed
    // carries no `response` at all, and the vendor's own error object arrives
    // at the top exactly as it does on the endpoint this family wraps.
    for (status, class) in [
        ("UNAUTHENTICATED", ErrorClass::Auth),
        ("PERMISSION_DENIED", ErrorClass::Auth),
        ("RESOURCE_EXHAUSTED", ErrorClass::Quota),
        ("NOT_FOUND", ErrorClass::InvalidRequest),
    ] {
        let text = format!(
            r#"{{"error":{{"code":401,"status":"{status}","message":"the vendor said so"}}}}"#
        );
        let reply = parse_provider(&text).expect("the reply parses");
        let outcome = read_reply(API, &reply, &context()).expect("the reply reads");
        let error = outcome
            .result
            .error
            .as_ref()
            .expect("a failing reply names a failure");
        assert_eq!(error.class, class, "{status}");
    }
}

#[test]
fn a_frame_without_the_envelope_is_a_defect_rather_than_an_empty_answer() {
    // A bare generative-language frame is not this family's frame. Unwrapped
    // when present and read flat when absent, it would fold into a reply that
    // finished with nothing to say — the one failure a caller cannot tell
    // from a quiet model.
    let bare = concat!(
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"the answer\"}]}}],",
        "\"usageMetadata\":{\"promptTokenCount\":3,\"candidatesTokenCount\":2}}\n",
        "{\"candidates\":[{\"content\":{\"parts\":[]},\"finishReason\":\"STOP\"}]}\n",
    );
    assert_eq!(
        fold_stream(API, bare, &context()),
        Err(StreamDefect::MalformedFrame)
    );
}

#[test]
fn a_streamed_failure_arrives_classified_rather_than_as_a_malformed_frame() {
    // The error branch is asked before the envelope is required, because a
    // failing frame legitimately has no envelope. Refused as malformed it
    // would lose the class, and an unclassified failure is terminal — so a
    // rate limit that a second attempt would clear would end the task.
    let frame = concat!(
        "data: {\"error\":{\"code\":429,\"status\":\"RESOURCE_EXHAUSTED\",",
        "\"message\":\"too many\"}}\n\n",
    );
    let folded = fold_stream(API, frame, &context()).expect("the frame reads");
    let error = folded
        .result
        .error
        .as_ref()
        .expect("a failing frame names a failure");
    assert_eq!(error.class, ErrorClass::Quota);
}
