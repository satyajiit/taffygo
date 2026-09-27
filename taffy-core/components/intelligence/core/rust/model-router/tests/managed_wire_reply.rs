// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Folding the managed Worker's schema-3 canonical NDJSON response.

#![allow(
    clippy::expect_used,
    clippy::indexing_slicing,
    clippy::panic,
    clippy::unwrap_used
)]

use model_router::cost::TokenUsage;
use model_router::ids::RequestId;
use model_router::json::{parse_provider, JsonValue};
use model_router::request::{ErrorClass, OverflowKind, StopReason};
use model_router::wire::reply::{ReplyContext, MAX_TOOL_CALLS};
use model_router::wire::{
    fold_managed_stream, ManagedReplyDefect, MANAGED_SCHEMA_VERSION, MAX_MANAGED_STREAM_BYTES,
};

const REQUEST_ID: &str = "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00";

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([7; 16]),
        context_window: 200_000,
        requested_answer_tokens: 4_096,
        reports_finish_reason: true,
    }
}

fn start(request_id: &str) -> String {
    format!(
        r#"{{"schema_version":{MANAGED_SCHEMA_VERSION},"type":"start","request_id":"{request_id}","route":{{"mode":"managed","provider_id":"provider-one","model_id":"reasoner-one","wire_api":"ANTHROPIC_MESSAGES","attempts":1,"failover":0,"price_basis":"METERED"}}}}"#
    )
}

fn text(request_id: &str, value: &str) -> String {
    format!(
        r#"{{"schema_version":{MANAGED_SCHEMA_VERSION},"type":"text_delta","request_id":"{request_id}","text":"{value}"}}"#
    )
}

fn tool_start(request_id: &str, index: u64, tool: &str) -> String {
    format!(
        r#"{{"schema_version":{MANAGED_SCHEMA_VERSION},"type":"tool_call_start","request_id":"{request_id}","index":{index},"tool":"{tool}"}}"#
    )
}

fn tool_delta(request_id: &str, index: u64, escaped_fragment: &str) -> String {
    format!(
        r#"{{"schema_version":{MANAGED_SCHEMA_VERSION},"type":"tool_call_delta","request_id":"{request_id}","index":{index},"arguments":"{escaped_fragment}"}}"#
    )
}

fn done(request_id: &str, stop: &str, input: u64, output: u64) -> String {
    format!(
        r#"{{"schema_version":{MANAGED_SCHEMA_VERSION},"type":"done","request_id":"{request_id}","stop_reason":"{stop}","usage":{{"input_tokens":{input},"output_tokens":{output},"cache_read_tokens":3,"cache_write_tokens":4}},"cost":{{"currency":"USD","micro_usd":81,"credits":1}}}}"#
    )
}

fn error(request_id: &str, class: &str) -> String {
    format!(
        r#"{{"schema_version":{MANAGED_SCHEMA_VERSION},"type":"error","request_id":"{request_id}","error":{{"class":"{class}","code":"response_too_large","message":"The response was too large.","retryable":false}}}}"#
    )
}

fn stream(lines: &[String]) -> String {
    format!("{}\n", lines.join("\n"))
}

#[test]
fn text_usage_stop_and_cost_shape_fold_without_retaining_text() {
    let body = stream(&[
        start(REQUEST_ID),
        text(REQUEST_ID, "hel"),
        text(REQUEST_ID, "lo"),
        done(REQUEST_ID, "end_turn", 12, 5),
    ]);
    let reading =
        fold_managed_stream(&body, REQUEST_ID, &context()).expect("a canonical stream folds");

    assert_eq!(reading.text_segments, 2);
    assert!(reading.tool_calls.is_empty());
    assert_eq!(reading.result.request_id, context().request_id);
    assert_eq!(reading.result.stop, StopReason::Complete);
    assert_eq!(
        reading.result.usage,
        TokenUsage {
            input: 12,
            output: 5,
            cache_read: 3,
            cache_write: 4,
        }
    );
    assert_eq!(reading.overflow, None);
}

#[test]
fn indexed_interleaved_arguments_reconstruct_as_objects_in_index_order() {
    let body = stream(&[
        start(REQUEST_ID),
        tool_start(REQUEST_ID, 9, "browser.dom.query"),
        tool_start(REQUEST_ID, 2, "browser.dom.read"),
        tool_delta(REQUEST_ID, 9, r#"{\"selector\":\"a"#),
        tool_delta(REQUEST_ID, 2, r#"{\"node\":"#),
        tool_delta(REQUEST_ID, 9, r#"\"}"#),
        tool_delta(REQUEST_ID, 2, "7}"),
        // Structure wins over an ordinary stop word: there is work waiting.
        done(REQUEST_ID, "end_turn", 8, 3),
    ]);
    let reading = fold_managed_stream(&body, REQUEST_ID, &context()).expect("calls fold");

    assert_eq!(reading.result.stop, StopReason::ToolCall);
    assert_eq!(reading.tool_calls.len(), 2);
    assert_eq!(reading.tool_calls[0].tool, "browser.dom.read");
    assert_eq!(reading.tool_calls[1].tool, "browser.dom.query");
    assert_eq!(
        reading.tool_calls[0].arguments,
        parse_provider(r#"{"node":7}"#).unwrap()
    );
    assert_eq!(
        reading.tool_calls[1].arguments,
        parse_provider(r#"{"selector":"a"}"#).unwrap()
    );
}

#[test]
fn an_empty_argument_stream_is_the_empty_object_but_any_other_non_object_is_refused() {
    let empty = stream(&[
        start(REQUEST_ID),
        tool_start(REQUEST_ID, 0, "browser.dom.read"),
        done(REQUEST_ID, "tool_call", 1, 1),
    ]);
    let reading = fold_managed_stream(&empty, REQUEST_ID, &context()).expect("empty object");
    assert_eq!(
        reading.tool_calls[0].arguments,
        JsonValue::Object(std::collections::BTreeMap::new())
    );

    for fragment in ["[1]", "true", r#"{\"node\":"#] {
        let malformed = stream(&[
            start(REQUEST_ID),
            tool_start(REQUEST_ID, 0, "browser.dom.read"),
            tool_delta(REQUEST_ID, 0, fragment),
            done(REQUEST_ID, "tool_call", 1, 1),
        ]);
        assert_eq!(
            fold_managed_stream(&malformed, REQUEST_ID, &context()),
            Err(ManagedReplyDefect::MalformedToolCall),
            "fragment {fragment:?}"
        );
    }
}

#[test]
fn the_reader_enforces_sixteen_distinct_calls_and_correlates_every_event() {
    let mut lines = vec![start(REQUEST_ID)];
    for index in 0..=MAX_TOOL_CALLS {
        lines.push(tool_start(
            REQUEST_ID,
            u64::try_from(index).unwrap(),
            "browser.dom.read",
        ));
    }
    lines.push(done(REQUEST_ID, "tool_call", 1, 1));
    assert_eq!(
        fold_managed_stream(&stream(&lines), REQUEST_ID, &context()),
        Err(ManagedReplyDefect::TooManyToolCalls)
    );

    let other = "1e95e8a2-77c4-41d3-8b6e-2a9c41d37f00";
    for mismatched in [text(other, "x"), done(other, "end_turn", 1, 1)] {
        assert_eq!(
            fold_managed_stream(
                &stream(&[start(REQUEST_ID), mismatched]),
                REQUEST_ID,
                &context()
            ),
            Err(ManagedReplyDefect::RequestMismatch)
        );
    }
    // The Worker canonicalizes the UUID spelling to lowercase.
    assert!(fold_managed_stream(
        &stream(&[start(REQUEST_ID), done(REQUEST_ID, "end_turn", 1, 1)]),
        &REQUEST_ID.to_ascii_uppercase(),
        &context()
    )
    .is_ok());
}

#[test]
fn frame_order_and_the_schema_shape_are_closed() {
    assert_eq!(
        fold_managed_stream(
            &stream(&[text(REQUEST_ID, "early")]),
            REQUEST_ID,
            &context()
        ),
        Err(ManagedReplyDefect::MissingStart)
    );
    assert_eq!(
        fold_managed_stream(
            &stream(&[start(REQUEST_ID), start(REQUEST_ID)]),
            REQUEST_ID,
            &context()
        ),
        Err(ManagedReplyDefect::DuplicateStart)
    );
    assert_eq!(
        fold_managed_stream(
            &stream(&[
                start(REQUEST_ID),
                done(REQUEST_ID, "end_turn", 1, 1),
                text(REQUEST_ID, "late"),
            ]),
            REQUEST_ID,
            &context()
        ),
        Err(ManagedReplyDefect::EventAfterTerminal)
    );
    assert_eq!(
        fold_managed_stream(&stream(&[start(REQUEST_ID)]), REQUEST_ID, &context()),
        Err(ManagedReplyDefect::MissingTerminal)
    );

    let with_unknown = start(REQUEST_ID).replacen(r#""route":"#, r#""future":true,"route":"#, 1);
    assert_eq!(
        fold_managed_stream(
            &stream(&[with_unknown, done(REQUEST_ID, "end_turn", 1, 1)]),
            REQUEST_ID,
            &context()
        ),
        Err(ManagedReplyDefect::MalformedFrame)
    );

    let older = start(REQUEST_ID).replacen(
        &format!("\"schema_version\":{MANAGED_SCHEMA_VERSION}"),
        "\"schema_version\":2",
        1,
    );
    assert_eq!(
        fold_managed_stream(
            &stream(&[older, done(REQUEST_ID, "end_turn", 1, 1)]),
            REQUEST_ID,
            &context()
        ),
        Err(ManagedReplyDefect::UnsupportedSchemaVersion)
    );
}

#[test]
fn a_terminal_error_is_read_and_overflow_is_promoted_on_both_paths() {
    let reported = fold_managed_stream(
        &stream(&[start(REQUEST_ID), error(REQUEST_ID, "overflow")]),
        REQUEST_ID,
        &context(),
    )
    .expect("a canonical error is a readable terminal");
    assert_eq!(reported.result.stop, StopReason::Error);
    assert_eq!(reported.result.usage, TokenUsage::default());
    assert_eq!(reported.overflow, Some(OverflowKind::ProviderReported));
    assert_eq!(
        reported.result.error.as_ref().map(|error| error.class),
        Some(ErrorClass::Overflow)
    );

    let mut small = context();
    small.context_window = 10;
    let silent = fold_managed_stream(
        &stream(&[start(REQUEST_ID), done(REQUEST_ID, "end_turn", 11, 1)]),
        REQUEST_ID,
        &small,
    )
    .expect("the structurally valid reply is classified");
    assert_eq!(silent.result.stop, StopReason::Error);
    assert_eq!(silent.overflow, Some(OverflowKind::Silent));
    assert_eq!(
        silent.result.error.as_ref().map(|error| error.class),
        Some(ErrorClass::Overflow)
    );
}

#[test]
fn malformed_or_oversized_transport_bytes_are_refused_before_folding() {
    let no_final_lf = stream(&[start(REQUEST_ID), done(REQUEST_ID, "end_turn", 1, 1)])
        .trim_end_matches('\n')
        .to_owned();
    assert_eq!(
        fold_managed_stream(&no_final_lf, REQUEST_ID, &context()),
        Err(ManagedReplyDefect::MalformedFrame)
    );
    assert_eq!(
        fold_managed_stream("\n", REQUEST_ID, &context()),
        Err(ManagedReplyDefect::MalformedFrame)
    );
    let oversized = "x".repeat(MAX_MANAGED_STREAM_BYTES + 1);
    assert_eq!(
        fold_managed_stream(&oversized, REQUEST_ID, &context()),
        Err(ManagedReplyDefect::StreamTooLarge)
    );
}
