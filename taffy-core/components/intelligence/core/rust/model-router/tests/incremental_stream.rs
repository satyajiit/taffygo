// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Chunk-boundary and lifecycle tests for the sandbox stream decoder.

#![allow(
    clippy::expect_used,
    clippy::indexing_slicing,
    clippy::panic,
    clippy::unwrap_used
)]

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::json::MAX_INPUT_BYTES;
use model_router::request::{ErrorClass, StopReason};
use model_router::wire::reply::ReplyContext;
use model_router::wire::{
    fold_managed_stream, fold_stream, ManagedReplyDefect, ModelStreamDecoder, ModelStreamDefect,
    ModelStreamReading, ModelStreamToolCall, StreamDefect, MAX_DIRECT_STREAM_BYTES,
    MAX_MANAGED_STREAM_BYTES,
};

const REQUEST_ID: &str = "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00";

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([6; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

fn direct_fixtures() -> Vec<(WireApi, &'static str)> {
    let anthropic = concat!(
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{",
        "\"input_tokens\":3,\"output_tokens\":0}}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"delta\":{",
        "\"type\":\"text_delta\",\"text\":\"hé🙂\"}}\n\n",
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},",
        "\"usage\":{\"output_tokens\":2}}\n\n",
    );
    let completions = concat!(
        "data: {\"choices\":[{\"delta\":{\"content\":\"hé🙂\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],",
        "\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":2}}\n\n",
        "data: [DONE]\n\n",
    );
    let responses = concat!(
        "event: response.reasoning_summary_text.delta\n",
        "data: {\"type\":\"response.reasoning_summary_text.delta\",",
        "\"delta\":\"private reasoning\",\"response_id\":\"provider-secret\"}\n\n",
        "event: response.output_text.delta\n",
        "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hé🙂\",",
        "\"response_id\":\"provider-secret\"}\n\n",
        "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{",
        "\"input_tokens\":3,\"output_tokens\":2}}}\n\n",
    );
    let google = concat!(
        "{\"candidates\":[{\"content\":{\"parts\":[",
        "{\"text\":\"private reasoning\",\"thought\":true},{\"text\":\"hé🙂\"}]}}],",
        "\"usageMetadata\":{\"promptTokenCount\":3,\"candidatesTokenCount\":2}}\n",
        "{\"candidates\":[{\"content\":{\"parts\":[]},\"finishReason\":\"STOP\"}]}\n",
    );
    // The same two frames the generative-language row reads, each inside the
    // envelope its endpoint wraps them in. Chunked at every width, this is what
    // proves the envelope is unwrapped per frame rather than per body — the
    // opening `{"response":` of the second frame arrives in a different chunk
    // from the first frame's closing brace at most widths.
    let cloud_code_assist = concat!(
        "{\"response\":{\"candidates\":[{\"content\":{\"parts\":[",
        "{\"text\":\"private reasoning\",\"thought\":true},{\"text\":\"hé🙂\"}]}}],",
        "\"usageMetadata\":{\"promptTokenCount\":3,\"candidatesTokenCount\":2}}}\n",
        "{\"response\":{\"candidates\":[{\"content\":{\"parts\":[]},",
        "\"finishReason\":\"STOP\"}]}}\n",
    );
    vec![
        (WireApi::AnthropicMessages, anthropic),
        (WireApi::OpenAiCompletions, completions),
        (WireApi::OpenAiResponses, responses),
        (WireApi::OpenAiCodexResponses, responses),
        (WireApi::GoogleGenerativeLanguage, google),
        (WireApi::GoogleCloudCodeAssist, cloud_code_assist),
    ]
}

#[test]
fn every_direct_family_is_equivalent_across_arbitrary_byte_chunks() {
    for (api, body) in direct_fixtures() {
        let expected = sanitized_direct(api, body);
        for width in 1..=body.len().min(23) {
            let (text, reading) = decode_direct(api, body.as_bytes().chunks(width));
            assert_eq!(text, vec!["hé🙂"], "visible text for {api:?}/{width}");
            assert_eq!(reading, expected, "terminal for {api:?}/{width}");
            assert_eq!(reading.result.provider_stop_reason, None);
        }
    }
}

#[test]
fn canonical_managed_chunks_split_utf8_and_interleaved_tool_arguments_safely() {
    let body = managed_body();
    let folded = fold_managed_stream(&body, REQUEST_ID, &context()).unwrap();
    let expected = ModelStreamReading {
        text_segments: folded.text_segments,
        tool_calls: folded
            .tool_calls
            .into_iter()
            .map(|call| ModelStreamToolCall {
                name: call.tool,
                arguments: call.arguments,
            })
            .collect(),
        result: folded.result,
        overflow: folded.overflow,
        recovery: None,
    };

    for width in 1..=body.len().min(29) {
        let mut decoder = ModelStreamDecoder::managed(REQUEST_ID, context());
        let mut visible = Vec::new();
        for chunk in body.as_bytes().chunks(width) {
            decoder
                .push(chunk, &mut |text| visible.push(text.to_owned()))
                .unwrap();
        }
        let reading = decoder
            .finish(&mut |text| visible.push(text.to_owned()))
            .unwrap();
        assert_eq!(visible, vec!["hé🙂"], "visible text at width {width}");
        assert_eq!(reading, expected, "terminal at width {width}");
        assert_eq!(reading.tool_calls[0].name, "second");
        assert_eq!(reading.tool_calls[1].name, "first");
    }
}

#[test]
fn cancellation_is_typed_and_end_before_terminal_is_a_defect() {
    let mut canceled = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    let mut visible = Vec::new();
    canceled
        .push(
            b"data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n",
            &mut |text| visible.push(text.to_owned()),
        )
        .unwrap();
    let reading = canceled.cancel().unwrap();
    assert_eq!(visible, vec!["partial"]);
    assert_eq!(reading.result.stop, StopReason::Error);
    assert_eq!(
        reading.result.error.as_ref().map(|error| error.class),
        Some(ErrorClass::Canceled)
    );
    assert_eq!(reading.result.provider_stop_reason, None);
    assert_eq!(
        canceled.finish(&mut |_| {}),
        Err(ModelStreamDefect::AlreadyFinished)
    );

    let mut direct = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    direct
        .push(
            b"data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n",
            &mut |_| {},
        )
        .unwrap();
    assert_eq!(
        direct.finish(&mut |_| {}),
        Err(ModelStreamDefect::Direct(StreamDefect::MissingStopReason))
    );

    let mut managed = ModelStreamDecoder::managed(REQUEST_ID, context());
    let prefix = format!(
        "{{\"schema_version\":3,\"type\":\"start\",\"request_id\":\"{REQUEST_ID}\",\"route\":{{\"mode\":\"managed\",\"provider_id\":\"p\",\"model_id\":\"m\",\"wire_api\":\"OPENAI_RESPONSES\",\"attempts\":1,\"failover\":0,\"price_basis\":\"METERED\"}}}}\n"
    );
    managed.push(prefix.as_bytes(), &mut |_| {}).unwrap();
    assert_eq!(
        managed.finish(&mut |_| {}),
        Err(ModelStreamDefect::Managed(
            ManagedReplyDefect::MissingTerminal
        ))
    );
}

#[test]
fn second_terminals_and_non_usage_after_a_terminal_are_refused() {
    let done = concat!(
        "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],",
        "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1}}\n",
    );
    let mut direct = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    let body = format!("{done}{done}");
    assert_eq!(
        direct.push(body.as_bytes(), &mut |_| {}),
        Err(ModelStreamDefect::Direct(StreamDefect::AlreadyTerminated))
    );

    let mut managed = ModelStreamDecoder::managed(REQUEST_ID, context());
    let body = managed_body();
    let second = format!("{body}{}\n", body.lines().last().unwrap());
    assert_eq!(
        managed.push(second.as_bytes(), &mut |_| {}),
        Err(ModelStreamDefect::Managed(
            ManagedReplyDefect::EventAfterTerminal
        ))
    );
}

#[test]
fn usage_may_follow_the_stop_and_is_merged_until_finish() {
    let mut decoder = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    decoder
        .push(
            b"{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n",
            &mut |_| {},
        )
        .unwrap();
    assert!(!decoder.terminal_ready(), "usage is still mandatory");
    decoder
        .push(
            b"{\"choices\":[],\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":1}}\n",
            &mut |_| {},
        )
        .unwrap();
    assert!(decoder.terminal_ready());
    decoder
        .push(
            b"{\"choices\":[],\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":2}}\n",
            &mut |_| {},
        )
        .unwrap();
    let reading = decoder.finish(&mut |_| {}).unwrap();
    assert_eq!(reading.result.usage.input, 5);
    assert_eq!(reading.result.usage.output, 2);

    let mut rejects_text = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    rejects_text
        .push(done_with_usage().as_bytes(), &mut |_| {})
        .unwrap();
    let mut leaked = Vec::new();
    assert_eq!(
        rejects_text.push(
            b"{\"choices\":[{\"delta\":{\"content\":\"late\"}}]}\n",
            &mut |text| leaked.push(text.to_owned())
        ),
        Err(ModelStreamDefect::Direct(StreamDefect::AlreadyTerminated))
    );
    assert!(
        leaked.is_empty(),
        "invalid post-terminal text stays private"
    );
}

#[test]
fn total_and_unfinished_frame_buffers_fail_at_stable_bounds() {
    let mut frame = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    let oversized_line = vec![b'x'; MAX_INPUT_BYTES + 7];
    assert_eq!(
        frame.push(&oversized_line, &mut |_| {}),
        Err(ModelStreamDefect::FrameTooLarge)
    );

    let mut total = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    let mut ignored_line = vec![b' '; MAX_INPUT_BYTES - 1];
    ignored_line.push(b'\n');
    for _ in 0..(MAX_DIRECT_STREAM_BYTES / MAX_INPUT_BYTES) {
        total.push(&ignored_line, &mut |_| {}).unwrap();
    }
    assert_eq!(
        total.push(b"x", &mut |_| {}),
        Err(ModelStreamDefect::InputTooLarge)
    );

    let mut managed = ModelStreamDecoder::managed(REQUEST_ID, context());
    assert_eq!(
        managed.push(&vec![b'x'; MAX_MANAGED_STREAM_BYTES + 1], &mut |_| {}),
        Err(ModelStreamDefect::InputTooLarge)
    );

    let mut newline_flood = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    assert_eq!(
        newline_flood.push(&vec![b'\n'; 100_000], &mut |_| {}),
        Err(ModelStreamDefect::InputTooLarge)
    );
}

#[test]
fn streamed_argument_fragments_share_one_incremental_json_bound() {
    let mut decoder = ModelStreamDecoder::direct(WireApi::OpenAiCompletions, context());
    decoder
        .push(
            b"{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"read_page\"}}]}}]}\n",
            &mut |_| {},
        )
        .unwrap();
    let half = " ".repeat(MAX_INPUT_BYTES / 2);
    let delta = argument_delta(&half);
    decoder.push(delta.as_bytes(), &mut |_| {}).unwrap();
    decoder.push(delta.as_bytes(), &mut |_| {}).unwrap();
    assert_eq!(
        decoder.push(argument_delta(" ").as_bytes(), &mut |_| {}),
        Err(ModelStreamDefect::Direct(
            StreamDefect::ToolArgumentsTooLarge
        ))
    );
}

#[test]
fn decoder_debug_state_cannot_reveal_a_buffered_raw_frame() {
    let mut decoder = ModelStreamDecoder::direct(WireApi::OpenAiResponses, context());
    decoder
        .push(
            b"data: {\"type\":\"response.reasoning.delta\",\"delta\":\"raw-secret",
            &mut |_| {},
        )
        .unwrap();
    let debug = format!("{decoder:?}");
    assert!(!debug.contains("raw-secret"));
    assert!(!debug.contains("reasoning"));
}

fn decode_direct<'a>(
    api: WireApi,
    chunks: impl IntoIterator<Item = &'a [u8]>,
) -> (Vec<String>, ModelStreamReading) {
    let mut decoder = ModelStreamDecoder::direct(api, context());
    let mut visible = Vec::new();
    for chunk in chunks {
        decoder
            .push(chunk, &mut |text| visible.push(text.to_owned()))
            .unwrap();
    }
    let reading = decoder
        .finish(&mut |text| visible.push(text.to_owned()))
        .unwrap();
    (visible, reading)
}

fn sanitized_direct(api: WireApi, body: &str) -> ModelStreamReading {
    let folded = fold_stream(api, body, &context()).unwrap();
    let mut result = folded.result;
    result.provider_stop_reason = None;
    ModelStreamReading {
        text_segments: folded.text_segments,
        tool_calls: folded
            .tool_calls
            .into_iter()
            .map(|call| ModelStreamToolCall {
                name: call.name,
                arguments: call.arguments,
            })
            .collect(),
        result,
        overflow: folded.overflow,
        recovery: folded.recovery,
    }
}

fn done_with_usage() -> String {
    concat!(
        "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],",
        "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1}}\n",
    )
    .to_owned()
}

fn argument_delta(arguments: &str) -> String {
    format!(
        "{{\"choices\":[{{\"delta\":{{\"tool_calls\":[{{\"index\":0,\"function\":{{\"arguments\":\"{arguments}\"}}}}]}}}}]}}\n"
    )
}

fn managed_body() -> String {
    let start = format!(
        "{{\"schema_version\":3,\"type\":\"start\",\"request_id\":\"{REQUEST_ID}\",\"route\":{{\"mode\":\"managed\",\"provider_id\":\"provider-secret\",\"model_id\":\"model-secret\",\"wire_api\":\"OPENAI_RESPONSES\",\"attempts\":1,\"failover\":0,\"price_basis\":\"METERED\"}}}}"
    );
    let text = format!(
        "{{\"schema_version\":3,\"type\":\"text_delta\",\"request_id\":\"{REQUEST_ID}\",\"text\":\"hé🙂\"}}"
    );
    let first = format!(
        "{{\"schema_version\":3,\"type\":\"tool_call_start\",\"request_id\":\"{REQUEST_ID}\",\"index\":9,\"tool\":\"first\"}}"
    );
    let second = format!(
        "{{\"schema_version\":3,\"type\":\"tool_call_start\",\"request_id\":\"{REQUEST_ID}\",\"index\":2,\"tool\":\"second\"}}"
    );
    let deltas = [
        (9, r#"{\"a\":"#),
        (2, r#"{\"b\":"#),
        (9, "1}"),
        (2, "2}"),
    ]
    .map(|(index, arguments)| {
        format!(
            "{{\"schema_version\":3,\"type\":\"tool_call_delta\",\"request_id\":\"{REQUEST_ID}\",\"index\":{index},\"arguments\":\"{arguments}\"}}"
        )
    });
    let done = format!(
        "{{\"schema_version\":3,\"type\":\"done\",\"request_id\":\"{REQUEST_ID}\",\"stop_reason\":\"tool_call\",\"usage\":{{\"input_tokens\":3,\"output_tokens\":2,\"cache_read_tokens\":0,\"cache_write_tokens\":0}},\"cost\":{{\"currency\":\"USD\",\"micro_usd\":4,\"credits\":0}}}}"
    );
    let mut lines = vec![start, text, first, second];
    lines.extend(deltas);
    lines.push(done);
    format!("{}\n", lines.join("\n"))
}
