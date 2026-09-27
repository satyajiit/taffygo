// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use model_router::ids::RequestId;
use task_engine::{
    ArgumentValue, Milestone, ModelStopReason, SuppliedArgument, TurnGap, TurnOverflow, TurnUsage,
};

use super::super::{read_model_reply, ModelReplyReading, ReplyWire};

const DEVICE_REQUEST_ID: &str = "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00";

fn start() -> String {
    format!(
        r#"{{"schema_version":3,"type":"start","request_id":"{DEVICE_REQUEST_ID}","route":{{"mode":"managed","provider_id":"provider-one","model_id":"reasoner-one","wire_api":"ANTHROPIC_MESSAGES","attempts":1,"failover":0,"price_basis":"METERED"}}}}"#
    )
}

fn done(stop: &str, input: u64, output: u64) -> String {
    format!(
        r#"{{"schema_version":3,"type":"done","request_id":"{DEVICE_REQUEST_ID}","stop_reason":"{stop}","usage":{{"input_tokens":{input},"output_tokens":{output},"cache_read_tokens":3,"cache_write_tokens":4}},"cost":{{"currency":"USD","micro_usd":81,"credits":1}}}}"#
    )
}

fn body(events: &[String]) -> Vec<u8> {
    format!("{}\n", events.join("\n")).into_bytes()
}

fn read(completion: &[u8], context_window: u64) -> ModelReplyReading {
    read_model_reply(
        &ReplyWire::Managed {
            request_id: DEVICE_REQUEST_ID.to_owned(),
        },
        completion,
        RequestId::from_bytes([5; 16]),
        context_window,
        4_096,
        Milestone::M3,
    )
}

#[test]
fn a_managed_stream_reaches_the_kernel_as_typed_tool_calls_and_usage() {
    let completion = body(&[
        start(),
        format!(
            r#"{{"schema_version":3,"type":"text_delta","request_id":"{DEVICE_REQUEST_ID}","text":"Opening it."}}"#
        ),
        format!(
            r#"{{"schema_version":3,"type":"tool_call_start","request_id":"{DEVICE_REQUEST_ID}","index":4,"tool":"browser.navigate"}}"#
        ),
        format!(
            r#"{{"schema_version":3,"type":"tool_call_delta","request_id":"{DEVICE_REQUEST_ID}","index":4,"arguments":"{{\"address\":\"https://"}}"#
        ),
        format!(
            r#"{{"schema_version":3,"type":"tool_call_delta","request_id":"{DEVICE_REQUEST_ID}","index":4,"arguments":"example.com\",\"new_tab\":true}}"}}"#
        ),
        done("tool_call", 17, 5),
    ]);

    let ModelReplyReading::Read(reply) = read(&completion, 200_000) else {
        panic!("the managed completion must be readable");
    };
    assert_eq!(reply.stop, ModelStopReason::ToolCall);
    assert_eq!(reply.answer_segments, 1);
    assert_eq!(
        reply.usage,
        TurnUsage {
            input_units: 17,
            output_units: 5,
            cache_read_units: 3,
            cache_write_units: 4,
        }
    );
    assert_eq!(reply.tool_calls.len(), 1);
    assert_eq!(reply.tool_calls[0].tool_name, "browser.navigate");
    assert_eq!(
        reply.tool_calls[0].arguments,
        vec![
            SuppliedArgument::new(
                "address",
                ArgumentValue::Address("https://example.com".to_owned()),
            ),
            SuppliedArgument::new("new_tab", ArgumentValue::Flag(true)),
        ]
    );
}

#[test]
fn managed_success_error_and_overflow_keep_the_kernel_stop_taxonomy() {
    let complete = body(&[start(), done("end_turn", 7, 2)]);
    let ModelReplyReading::Read(reply) = read(&complete, 100) else {
        panic!("complete reply");
    };
    assert_eq!(reply.stop, ModelStopReason::Complete);
    assert_eq!(reply.overflow, None);

    let error = body(&[
        start(),
        format!(
            r#"{{"schema_version":3,"type":"error","request_id":"{DEVICE_REQUEST_ID}","error":{{"class":"overloaded","code":"upstream_unavailable","message":"The provider is unavailable.","retryable":true}}}}"#
        ),
    ]);
    let ModelReplyReading::Read(reply) = read(&error, 100) else {
        panic!("canonical error reply");
    };
    assert_eq!(reply.stop, ModelStopReason::Error);
    assert_eq!(reply.usage, TurnUsage::default());

    let overflow = body(&[start(), done("end_turn", 101, 1)]);
    let ModelReplyReading::Read(reply) = read(&overflow, 100) else {
        panic!("classified overflow reply");
    };
    assert_eq!(reply.stop, ModelStopReason::Error);
    assert_eq!(reply.overflow, Some(TurnOverflow::Silent));
}

#[test]
fn malformed_or_mis_correlated_managed_streams_are_turn_gaps() {
    let malformed_arguments = body(&[
        start(),
        format!(
            r#"{{"schema_version":3,"type":"tool_call_start","request_id":"{DEVICE_REQUEST_ID}","index":0,"tool":"browser.navigate"}}"#
        ),
        format!(
            r#"{{"schema_version":3,"type":"tool_call_delta","request_id":"{DEVICE_REQUEST_ID}","index":0,"arguments":"[1]"}}"#
        ),
        done("tool_call", 1, 1),
    ]);
    assert_eq!(
        read(&malformed_arguments, 100),
        ModelReplyReading::Gap(TurnGap::Unreadable)
    );

    let other_id = "1e95e8a2-77c4-41d3-8b6e-2a9c41d37f00";
    let mismatch = body(&[
        start(),
        format!(
            r#"{{"schema_version":3,"type":"done","request_id":"{other_id}","stop_reason":"end_turn","usage":{{"input_tokens":1,"output_tokens":1,"cache_read_tokens":0,"cache_write_tokens":0}},"cost":{{"currency":"USD","micro_usd":1,"credits":1}}}}"#
        ),
    ]);
    assert_eq!(
        read(&mismatch, 100),
        ModelReplyReading::Gap(TurnGap::Unreadable)
    );
}
