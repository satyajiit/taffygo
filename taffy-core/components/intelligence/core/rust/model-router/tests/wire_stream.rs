// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Streaming frames fold into the same taxonomy a complete reply uses.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::json::{parse, JsonValue, MAX_INPUT_BYTES};
use model_router::request::StopReason;
use model_router::wire::reply::ReplyContext;
use model_router::wire::request::{Speaker, Turn};
use model_router::wire::{
    apply_frame, fold_stream, looks_like_stream, write_request, StreamDefect, StreamEvent,
    StreamFold,
};

mod common;

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([7; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

#[test]
fn an_anthropic_sse_text_stream_folds_stop_and_usage() {
    let body = concat!(
        "event: message_start\n",
        "data: {\"type\":\"message_start\"}\n\n",
        "event: content_block_delta\n",
        "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"hi\"}}\n\n",
        "event: message_delta\n",
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},",
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":2}}\n\n",
        "event: message_stop\n",
        "data: {\"type\":\"message_stop\"}\n\n",
    );
    assert!(looks_like_stream(body));
    let folded = fold_stream(WireApi::AnthropicMessages, body, &context()).unwrap();
    assert_eq!(folded.result.stop, StopReason::Complete);
    assert_eq!(folded.result.usage.input, 10);
    assert_eq!(folded.result.usage.output, 2);
    assert_eq!(folded.text_segments, 1);
    assert!(folded.tool_calls.is_empty());
}

#[test]
fn an_anthropic_sse_tool_stream_marks_every_call_as_a_tool_stop() {
    let body = concat!(
        "data: {\"type\":\"content_block_start\",\"content_block\":",
        "{\"type\":\"tool_use\",\"id\":\"c1\",\"name\":\"browser.dom.read\"}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"delta\":",
        "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"tab\\\":1}\"}}\n\n",
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},",
        "\"usage\":{\"input_tokens\":8,\"output_tokens\":4}}\n\n",
    );
    let folded = fold_stream(WireApi::AnthropicMessages, body, &context()).unwrap();
    assert_eq!(folded.result.stop, StopReason::ToolCall);
    let call = folded.tool_calls.first().expect("one call");
    assert_eq!(call.name, "browser.dom.read");
}

#[test]
fn an_openai_sse_text_stream_folds() {
    let body = concat!(
        "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],",
        "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":1}}\n\n",
        "data: [DONE]\n\n",
    );
    let folded = fold_stream(WireApi::OpenAiCompletions, body, &context()).unwrap();
    assert_eq!(folded.result.stop, StopReason::Complete);
    assert_eq!(folded.result.usage.input, 5);
    assert_eq!(folded.result.usage.output, 1);
}

#[test]
fn usage_split_across_anthropic_events_is_merged_without_double_charging_cache() {
    let body = concat!(
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{",
        "\"input_tokens\":12,\"output_tokens\":0,",
        "\"cache_read_input_tokens\":4,\"cache_creation_input_tokens\":2}}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"index\":0,",
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"ok\"}}\n\n",
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},",
        "\"usage\":{\"output_tokens\":3}}\n\n",
    );
    let folded = fold_stream(WireApi::AnthropicMessages, body, &context()).unwrap();
    assert_eq!(folded.result.usage.input, 12);
    assert_eq!(folded.result.usage.output, 3);
    assert_eq!(folded.result.usage.cache_read, 4);
    assert_eq!(folded.result.usage.cache_write, 2);
}

#[test]
fn openai_stream_usage_prices_cached_input_once() {
    let body = concat!(
        "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}\n\n",
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":10,",
        "\"completion_tokens\":2,\"prompt_tokens_details\":{\"cached_tokens\":4}}}\n\n",
    );
    let folded = fold_stream(WireApi::OpenAiCompletions, body, &context()).unwrap();
    assert_eq!(folded.result.usage.input, 6);
    assert_eq!(folded.result.usage.cache_read, 4);
}

#[test]
fn interleaved_tool_deltas_follow_their_provider_slots() {
    let body = concat!(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[",
        "{\"index\":0,\"id\":\"a\",\"function\":{\"name\":\"first\",\"arguments\":\"{\\\"a\\\":\"}},",
        "{\"index\":1,\"id\":\"b\",\"function\":{\"name\":\"second\",\"arguments\":\"{\\\"b\\\":\"}}]}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[",
        "{\"index\":1,\"function\":{\"arguments\":\"2}\"}},",
        "{\"index\":0,\"function\":{\"arguments\":\"1}\"}}]},",
        "\"finish_reason\":\"tool_calls\"}],",
        "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":4}}\n\n",
    );
    let folded = fold_stream(WireApi::OpenAiCompletions, body, &context()).unwrap();
    assert_eq!(folded.tool_calls.len(), 2);
    assert_eq!(folded.tool_calls[0].name, "first");
    assert_eq!(
        folded.tool_calls[0]
            .arguments
            .field("a")
            .and_then(JsonValue::as_i64),
        Some(1)
    );
    assert_eq!(folded.tool_calls[1].name, "second");
    assert_eq!(
        folded.tool_calls[1]
            .arguments
            .field("b")
            .and_then(JsonValue::as_i64),
        Some(2)
    );
}

#[test]
fn google_hides_thinking_keeps_its_cost_and_preserves_tool_arguments() {
    let body = concat!(
        "{\"candidates\":[{\"content\":{\"parts\":[",
        "{\"text\":\"private\",\"thought\":true},{\"text\":\"answer\"},",
        "{\"functionCall\":{\"name\":\"read_page\",\"args\":{\"tab\":7}}}]},",
        "\"finishReason\":\"STOP\"}],\"usageMetadata\":{",
        "\"promptTokenCount\":10,\"cachedContentTokenCount\":4,",
        "\"candidatesTokenCount\":2,\"thoughtsTokenCount\":3}}\n",
    );
    let folded = fold_stream(WireApi::GoogleGenerativeLanguage, body, &context()).unwrap();
    assert_eq!(folded.text_segments, 1, "the thought is not answer text");
    assert_eq!(folded.result.usage.input, 6);
    assert_eq!(folded.result.usage.cache_read, 4);
    assert_eq!(folded.result.usage.output, 5);
    assert_eq!(
        folded.tool_calls[0]
            .arguments
            .field("tab")
            .and_then(JsonValue::as_i64),
        Some(7)
    );
}

#[test]
fn malformed_streamed_tool_arguments_are_a_defect_not_an_empty_object() {
    let body = concat!(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[",
        "{\"index\":0,\"function\":{\"name\":\"read_page\",\"arguments\":\"{bad\"}}]},",
        "\"finish_reason\":\"tool_calls\"}],",
        "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":1}}\n\n",
    );
    assert_eq!(
        fold_stream(WireApi::OpenAiCompletions, body, &context()),
        Err(StreamDefect::MalformedToolCall)
    );
}

#[test]
fn a_second_terminal_is_refused() {
    let mut fold = StreamFold::new();
    apply_frame(
        WireApi::AnthropicMessages,
        r#"{"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"input_tokens":1,"output_tokens":1}}"#,
        &mut fold,
    )
    .unwrap();
    assert!(fold.is_terminated());
    let second = apply_frame(
        WireApi::AnthropicMessages,
        r#"{"type":"error","error":{"type":"api_error","message":"no"}}"#,
        &mut fold,
    );
    assert!(second.is_err());
}

#[test]
fn cumulative_tool_arguments_never_outgrow_one_bounded_json_document() {
    let mut fold = StreamFold::new();
    fold.apply(StreamEvent::ToolCallStart {
        index: None,
        call_id: Some("call-1"),
        name: "browser.dom.read",
    })
    .unwrap();

    let at_limit = " ".repeat(MAX_INPUT_BYTES);
    assert!(fold
        .apply(StreamEvent::ToolCallDelta {
            index: None,
            arguments: &at_limit,
        })
        .is_ok());
    assert_eq!(
        fold.apply(StreamEvent::ToolCallDelta {
            index: None,
            arguments: " ",
        }),
        Err(StreamDefect::ToolArgumentsTooLarge),
        "the cumulative bound has a stable, actionable defect"
    );
}

#[test]
fn a_stream_flag_is_written_on_families_that_put_it_in_the_body() {
    const PIECES: [&str; 1] = ["hello"];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }];
    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiCompletions,
        WireApi::OpenAiResponses,
    ] {
        let plan = common::plan_for(api);
        let mut request = common::wire_request(&plan, &turns);
        request.stream = true;
        let mut out = String::new();
        write_request(api, &request, &mut out).unwrap();
        let document = parse(&out).unwrap();
        assert_eq!(
            document.field("stream").and_then(JsonValue::as_bool),
            Some(true),
            "{api:?}"
        );
    }
    let plan = common::plan_for(WireApi::GoogleGenerativeLanguage);
    let mut request = common::wire_request(&plan, &turns);
    request.stream = true;
    let mut google = String::new();
    write_request(WireApi::GoogleGenerativeLanguage, &request, &mut google).unwrap();
    let document = parse(&google).unwrap();
    assert!(document.field("stream").is_none());
}

#[test]
fn a_streamed_completions_request_asks_for_its_usage_and_no_other_family_does() {
    const PIECES: [&str; 1] = ["hello"];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }];
    let include_usage = |api: WireApi, stream: bool| {
        let plan = common::plan_for(api);
        let mut request = common::wire_request(&plan, &turns);
        request.stream = stream;
        let mut out = String::new();
        write_request(api, &request, &mut out).unwrap();
        parse(&out)
            .unwrap()
            .field("stream_options")
            .map(|options| options.field("include_usage").and_then(JsonValue::as_bool))
    };
    assert_eq!(
        include_usage(WireApi::OpenAiCompletions, true),
        Some(Some(true))
    );
    assert_eq!(include_usage(WireApi::OpenAiCompletions, false), None);
    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::GoogleGenerativeLanguage,
    ] {
        assert_eq!(include_usage(api, true), None, "{api:?}");
    }
}

/// The frame sequence a Grok chat-completions stream sent on 2026-09-18
/// when the request did not ask for usage: reasoning deltas, answer text, one
/// whole tool call, a separate stop frame and `[DONE]` — and no counts at all.
const STREAM_WITHOUT_USAGE: &str = concat!(
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"The\",",
    "\"role\":\"assistant\"}}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Searching.\"}}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"call-1\",",
    "\"function\":{\"name\":\"browser.search\",\"arguments\":\"{\\\"query\\\":\\\"q\\\"}\"},",
    "\"index\":0,\"type\":\"function\"}]}}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n",
);

#[test]
fn a_completions_stream_without_usage_is_a_defect_until_the_usage_frame_arrives() {
    let unasked = format!("{STREAM_WITHOUT_USAGE}data: [DONE]\n\n");
    assert_eq!(
        fold_stream(WireApi::OpenAiCompletions, &unasked, &context()),
        Err(StreamDefect::MissingUsage)
    );
    let asked = format!(
        "{STREAM_WITHOUT_USAGE}data: {{\"choices\":[],\"usage\":{{\"prompt_tokens\":9,\
         \"completion_tokens\":3}}}}\n\ndata: [DONE]\n\n"
    );
    let folded = fold_stream(WireApi::OpenAiCompletions, &asked, &context()).unwrap();
    assert_eq!(folded.result.stop, StopReason::ToolCall);
    assert_eq!(folded.result.usage.input, 9);
    assert_eq!(folded.text_segments, 1, "reasoning is not answer text");
    assert_eq!(folded.tool_calls.len(), 1);
    assert_eq!(folded.tool_calls[0].name, "browser.search");
    assert_eq!(
        folded.tool_calls[0]
            .arguments
            .field("query")
            .and_then(JsonValue::as_str),
        Some("q")
    );
}
