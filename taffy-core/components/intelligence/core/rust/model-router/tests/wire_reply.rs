// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What each row of the family table reads back, and the three failures that
//! look like successes.
//!
//! Every family test is driven by `common::ALL_FAMILIES`, so a family whose row
//! is wrong fails here rather than at a provider. The replies are synthetic:
//! they are the published shape of each family, not recordings, and nothing
//! here claims to be the conformance suite against real fixtures.

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
use model_router::request::{ErrorClass, OverflowKind, RecoverableSignal, StopReason};
use model_router::wire::{read_reply, Arguments, ReplyContext};

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([5; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

/// One successful text reply per family, all reporting the same 150-token
/// prompt of which 40 were cached, and the same twenty tokens of answer.
fn text_reply(api: WireApi) -> &'static str {
    match api {
        // The one family that reports uncached input apart from the cache
        // counts, so its input number is 100 where the others say 150.
        WireApi::AnthropicMessages => {
            r#"{"stop_reason":"end_turn",
                "content":[{"type":"text","text":"the answer"}],
                "usage":{"input_tokens":100,"output_tokens":20,
                         "cache_read_input_tokens":40,
                         "cache_creation_input_tokens":10}}"#
        }
        // The two nulls are not decoration. This family sends `error` and
        // `incomplete_details` in every body, null when there is neither, so a
        // fixture without them is a shape the provider never emits — and it
        // was passing a reader that treated the presence of `error` as a
        // failure.
        WireApi::OpenAiResponses => {
            r#"{"status":"completed","error":null,"incomplete_details":null,
                "output":[{"type":"message",
                           "content":[{"type":"output_text","text":"the answer"}]}],
                "usage":{"input_tokens":150,"output_tokens":20,
                         "input_tokens_details":{"cached_tokens":40}}}"#
        }
        WireApi::OpenAiCompletions => {
            r#"{"choices":[{"finish_reason":"stop",
                            "message":{"role":"assistant","content":"the answer"}}],
                "usage":{"prompt_tokens":150,"completion_tokens":20,
                         "prompt_tokens_details":{"cached_tokens":40}}}"#
        }
        WireApi::GoogleGenerativeLanguage => {
            r#"{"candidates":[{"finishReason":"STOP",
                               "content":{"role":"model",
                                          "parts":[{"text":"the answer"}]}}],
                "usageMetadata":{"promptTokenCount":150,"candidatesTokenCount":20,
                                 "cachedContentTokenCount":40}}"#
        }
        // The one family that returns sealed reasoning beside the answer. The
        // item is here because reading it must not put any of it into the
        // answer text, and a fixture without one could not tell.
        WireApi::OpenAiCodexResponses => {
            r#"{"status":"completed","error":null,"incomplete_details":null,
                "output":[{"type":"reasoning","id":"rs_1",
                           "encrypted_content":"gAAAAAsealed"},
                          {"type":"message",
                           "content":[{"type":"output_text","text":"the answer"}]}],
                "usage":{"input_tokens":150,"output_tokens":20,
                         "input_tokens_details":{"cached_tokens":40}}}"#
        }
        // The same document the row above reads, wrapped. Read without the
        // envelope it has no candidates and no counts, so this fixture is what
        // says the row's paths carry the wrapper on every one of them.
        WireApi::GoogleCloudCodeAssist => {
            r#"{"response":{
                  "candidates":[{"finishReason":"STOP",
                                 "content":{"role":"model",
                                            "parts":[{"text":"the answer"}]}}],
                  "usageMetadata":{"promptTokenCount":150,"candidatesTokenCount":20,
                                   "cachedContentTokenCount":40}}}"#
        }
    }
}

fn tool_reply(api: WireApi) -> &'static str {
    match api {
        WireApi::AnthropicMessages => {
            r#"{"stop_reason":"tool_use",
                "content":[{"type":"tool_use","id":"toolu_1","name":"read_page",
                            "input":{"handle":3}}],
                "usage":{"input_tokens":100,"output_tokens":20}}"#
        }
        WireApi::OpenAiResponses => {
            r#"{"status":"completed","error":null,"incomplete_details":null,
                "output":[{"type":"function_call","call_id":"call_1",
                           "name":"read_page","arguments":"{\"handle\":3}"}],
                "usage":{"input_tokens":100,"output_tokens":20}}"#
        }
        WireApi::OpenAiCompletions => {
            r#"{"choices":[{"finish_reason":"tool_calls","message":{
                    "tool_calls":[{"id":"call_1","type":"function",
                                   "function":{"name":"read_page",
                                               "arguments":"{\"handle\":3}"}}]}}],
                "usage":{"prompt_tokens":100,"completion_tokens":20}}"#
        }
        WireApi::GoogleGenerativeLanguage => {
            r#"{"candidates":[{"finishReason":"STOP","content":{"parts":[
                    {"functionCall":{"name":"read_page","args":{"handle":3}}}]}}],
                "usageMetadata":{"promptTokenCount":100,"candidatesTokenCount":20}}"#
        }
        WireApi::OpenAiCodexResponses => {
            r#"{"status":"completed","error":null,"incomplete_details":null,
                "output":[{"type":"reasoning","id":"rs_1",
                           "encrypted_content":"gAAAAAsealed"},
                          {"type":"function_call","call_id":"call_1",
                           "name":"read_page","arguments":"{\"handle\":3}"}],
                "usage":{"input_tokens":100,"output_tokens":20}}"#
        }
        // Wrapped, and carrying the identity the endpoint mints — which the
        // family it wraps does not, because that one reads none.
        WireApi::GoogleCloudCodeAssist => {
            r#"{"response":{
                  "candidates":[{"finishReason":"STOP","content":{"parts":[
                      {"functionCall":{"id":"call_1","name":"read_page",
                                       "args":{"handle":3}}}]}}],
                  "usageMetadata":{"promptTokenCount":100,"candidatesTokenCount":20}}}"#
        }
    }
}

#[test]
fn every_family_reads_its_answer_and_its_counts() {
    for api in common::ALL_FAMILIES {
        let reply = parse(text_reply(api)).unwrap();
        let outcome = read_reply(api, &reply, &context()).expect("the reply reads");
        assert_eq!(outcome.reading.text, vec!["the answer"], "{api:?}");
        assert_eq!(outcome.result.stop, StopReason::Complete, "{api:?}");
        assert_eq!(outcome.result.usage.output, 20, "{api:?}");
        // The prompt total is 150 on every family whatever each one calls it,
        // and whatever each one already folded into its own input number. A
        // reader that ignored that would price three of the four with the
        // cached tokens counted twice.
        assert_eq!(outcome.result.usage.total_input(), 150, "{api:?}");
        assert_eq!(outcome.result.usage.cache_read, 40, "{api:?}");
        assert!(outcome.result.is_success(), "{api:?}");
        assert!(outcome.overflow.is_none(), "{api:?}");
        assert!(outcome.recovery.is_none(), "{api:?}");
    }
}

#[test]
fn a_family_that_writes_whole_number_floats_for_usage_still_reads() {
    let reply = parse_provider(
        r#"{"stop_reason":"end_turn",
            "content":[{"type":"text","text":"the answer"}],
            "usage":{"input_tokens":100.0,"output_tokens":20.0,
                     "cache_read_input_tokens":40.0,
                     "cache_creation_input_tokens":10.0}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap();
    assert_eq!(outcome.result.stop, StopReason::Complete);
    assert_eq!(outcome.result.usage.input, 100);
    assert_eq!(outcome.result.usage.output, 20);
    assert_eq!(outcome.result.usage.cache_read, 40);
}

#[test]
fn a_reading_borrows_from_the_reply_rather_than_copying_it() {
    // The structural half of "this crate holds no page content": the answer in
    // the reading is the very same bytes the caller's own value holds, not a
    // copy this crate made and could have kept.
    let reply = parse(text_reply(WireApi::AnthropicMessages)).unwrap();
    let outcome = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap();
    let inside = reply
        .field("content")
        .and_then(JsonValue::as_array)
        .and_then(|blocks| blocks.first())
        .and_then(|block| block.field("text"))
        .and_then(JsonValue::as_str)
        .unwrap();
    assert!(std::ptr::eq(outcome.reading.text[0], inside));
}

#[test]
fn a_tool_call_is_a_tool_call_whatever_the_family_calls_the_stop() {
    // Two families report an ordinary finish word beside a tool call. The
    // structure is the fact; the word is that family's own habit.
    for api in common::ALL_FAMILIES {
        let reply = parse(tool_reply(api)).unwrap();
        let outcome = read_reply(api, &reply, &context()).expect("the reply reads");
        assert_eq!(outcome.result.stop, StopReason::ToolCall, "{api:?}");
        assert_eq!(outcome.reading.tool_calls.len(), 1, "{api:?}");
        let call = outcome.reading.tool_calls[0];
        assert_eq!(call.tool, "read_page", "{api:?}");
        match call.arguments {
            Arguments::Value(value) => {
                assert_eq!(value.field("handle").and_then(JsonValue::as_i64), Some(3));
            }
            Arguments::Text(text) => assert!(text.contains("\"handle\"")),
        }
    }
}

#[test]
fn one_family_mints_no_identity_for_a_tool_call() {
    let minted = common::ALL_FAMILIES
        .into_iter()
        .filter(|api| {
            let reply = parse(tool_reply(*api)).unwrap();
            let outcome = read_reply(*api, &reply, &context()).unwrap();
            outcome.reading.tool_calls[0].call_id.is_some()
        })
        .count();
    assert_eq!(minted, 4, "the fifth pairs a result by the tool's name");
    let reply = parse(tool_reply(WireApi::GoogleGenerativeLanguage)).unwrap();
    let outcome = read_reply(WireApi::GoogleGenerativeLanguage, &reply, &context()).unwrap();
    assert_eq!(
        outcome.reading.tool_calls[0].call_id, None,
        "an absent identity read as an empty string is worse than no identity"
    );
}

#[test]
fn a_reply_reporting_more_input_than_the_window_is_not_a_success() {
    let reply = parse(
        r#"{"stop_reason":"end_turn","content":[{"type":"text","text":"hm"}],
            "usage":{"input_tokens":250000,"output_tokens":5}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap();
    assert_eq!(outcome.overflow, Some(OverflowKind::Silent));
    assert!(!outcome.result.is_success());
    assert_eq!(
        outcome.result.error.as_ref().map(|error| error.class),
        Some(ErrorClass::Overflow)
    );
    // Named as it was actually noticed, not as the class the promotion wrote —
    // which is why the recovery is read before the promotion runs.
    assert_eq!(
        outcome.recovery,
        Some(RecoverableSignal::CompactAndRetry(OverflowKind::Silent))
    );
}

#[test]
fn a_length_stop_with_no_output_at_a_full_window_is_a_truncation_overflow() {
    let reply = parse(
        r#"{"choices":[{"finish_reason":"length","message":{"content":""}}],
            "usage":{"prompt_tokens":200000,"completion_tokens":0}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::OpenAiCompletions, &reply, &context()).unwrap();
    assert_eq!(outcome.overflow, Some(OverflowKind::Truncation));
    assert_eq!(
        outcome.recovery,
        Some(RecoverableSignal::CompactAndRetry(OverflowKind::Truncation))
    );
}

#[test]
fn an_answer_cut_short_below_its_allowance_is_recoverable_without_being_an_error() {
    let reply = parse(
        r#"{"status":"incomplete","incomplete_details":{"reason":"max_output_tokens"},
            "output":[{"type":"message","content":[{"type":"output_text","text":"half"}]}],
            "usage":{"input_tokens":100,"output_tokens":900}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::OpenAiResponses, &reply, &context()).unwrap();
    assert_eq!(
        outcome.result.stop,
        StopReason::Length,
        "the specific path is read before the general one"
    );
    assert!(outcome.overflow.is_none());
    assert_eq!(outcome.recovery, Some(RecoverableSignal::AnswerTruncated));
    assert!(outcome.result.error.is_none());
}

#[test]
fn a_stop_word_nobody_mapped_is_a_refusal() {
    let reply = parse(
        r#"{"stop_reason":"vibes","content":[],"usage":{"input_tokens":1,"output_tokens":1}}"#,
    )
    .unwrap();
    let error = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap_err();
    assert!(format!("{error}").contains("vibes"));
}

#[test]
fn a_reply_with_no_usage_is_a_refusal_rather_than_a_free_call() {
    let reply = parse(r#"{"stop_reason":"end_turn","content":[]}"#).unwrap();
    let error = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap_err();
    assert_eq!(format!("{error}"), "the reply carries no usage");
}

#[test]
fn every_family_normalizes_its_own_failure_words() {
    let cases = [
        (
            WireApi::AnthropicMessages,
            r#"{"error":{"type":"rate_limit_error","message":"slow down"}}"#,
            ErrorClass::Quota,
        ),
        (
            WireApi::OpenAiResponses,
            r#"{"error":{"code":"context_length_exceeded","message":"too long"}}"#,
            ErrorClass::Overflow,
        ),
        (
            WireApi::OpenAiCompletions,
            r#"{"error":{"code":"invalid_api_key","message":"no"}}"#,
            ErrorClass::Auth,
        ),
        (
            WireApi::GoogleGenerativeLanguage,
            r#"{"error":{"status":"RESOURCE_EXHAUSTED","message":"quota"}}"#,
            ErrorClass::Quota,
        ),
    ];
    for (api, text, expected) in cases {
        let reply = parse(text).unwrap();
        let outcome = read_reply(api, &reply, &context()).unwrap();
        let error = outcome.result.error.as_ref().unwrap();
        assert_eq!(error.class, expected, "{api:?}");
        assert!(!outcome.result.is_success(), "{api:?}");
    }
}

#[test]
fn a_null_where_a_failure_would_be_is_not_a_failure() {
    // The reply every successful call on this family produces. Read by the
    // presence of the `error` key rather than by what is under it, all four
    // assertions below fail at once — the ending, the answer and both halves
    // of the price — and they fail on the ordinary path, not an edge of it.
    let reply = parse(
        r#"{"status":"completed","error":null,"incomplete_details":null,
            "output":[{"type":"message","content":[{"type":"output_text","text":"the answer"}]}],
            "usage":{"input_tokens":150,"output_tokens":20}}"#,
    )
    .unwrap();
    let outcome = read_reply(WireApi::OpenAiResponses, &reply, &context()).unwrap();
    assert_eq!(outcome.result.stop, StopReason::Complete);
    assert!(outcome.result.error.is_none());
    assert_eq!(outcome.reading.text, vec!["the answer"]);
    assert_eq!(outcome.result.usage.total_input(), 150);
    assert_eq!(outcome.result.usage.output, 20);
}

#[test]
fn a_null_where_the_counts_belong_is_refused_rather_than_read_as_free() {
    // The mirror of the rule above, and it has to land the other way: an
    // absent count is not zero tokens, it is an unknown bill, and a budget
    // told a call was free stops being a budget.
    let reply = parse(r#"{"stop_reason":"end_turn","content":[],"usage":null}"#).unwrap();
    let error = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap_err();
    assert_eq!(format!("{error}"), "the reply carries no usage");
}

#[test]
fn a_failure_word_nobody_mapped_stops_instead_of_being_retried() {
    let reply = parse(r#"{"error":{"type":"weather_error","message":"it rained"}}"#).unwrap();
    let outcome = read_reply(WireApi::AnthropicMessages, &reply, &context()).unwrap();
    let error = outcome.result.error.as_ref().unwrap();
    assert_eq!(error.class, ErrorClass::Unknown);
    assert_eq!(error.detail.as_str(), "it rained");
    assert!(!error.class.permits_failover());
}
