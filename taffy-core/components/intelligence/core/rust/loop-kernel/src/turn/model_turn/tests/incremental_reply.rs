// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use model_router::ids::RequestId;
use task_engine::{ArgumentValue, Milestone, ModelStopReason, SuppliedArgument, TurnUsage};

use crate::context::LivePage;

use super::super::{compose_model_turn, ModelReplyReading, ModelReplyStream, ReplyWire};
use super::{candidate, facts, FoldDigest, NoCredentials, RecordingRouter};

const REQUEST_ID: &str = "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00";

#[test]
fn an_incremental_managed_terminal_drives_the_milestone_typed_tool_call() {
    let body = format!(
        concat!(
            "{{\"schema_version\":3,\"type\":\"start\",\"request_id\":\"{0}\",",
            "\"route\":{{\"mode\":\"managed\",\"provider_id\":\"provider-one\",",
            "\"model_id\":\"model-one\",\"wire_api\":\"OPENAI_RESPONSES\",",
            "\"attempts\":1,\"failover\":0,\"price_basis\":\"METERED\"}}}}\n",
            "{{\"schema_version\":3,\"type\":\"text_delta\",\"request_id\":\"{0}\",",
            "\"text\":\"Opening it.\"}}\n",
            "{{\"schema_version\":3,\"type\":\"tool_call_start\",\"request_id\":\"{0}\",",
            "\"index\":0,\"tool\":\"browser.navigate\"}}\n",
            "{{\"schema_version\":3,\"type\":\"tool_call_delta\",\"request_id\":\"{0}\",",
            "\"index\":0,\"arguments\":\"{{\\\"address\\\":\\\"https://example.com\\\",\"}}\n",
            "{{\"schema_version\":3,\"type\":\"tool_call_delta\",\"request_id\":\"{0}\",",
            "\"index\":0,\"arguments\":\"\\\"new_tab\\\":true}}\"}}\n",
            "{{\"schema_version\":3,\"type\":\"done\",\"request_id\":\"{0}\",",
            "\"stop_reason\":\"tool_call\",\"usage\":{{\"input_tokens\":17,",
            "\"output_tokens\":5,\"cache_read_tokens\":3,\"cache_write_tokens\":4}},",
            "\"cost\":{{\"currency\":\"USD\",\"micro_usd\":81,\"credits\":1}}}}\n"
        ),
        REQUEST_ID
    );
    let wire = ReplyWire::Managed {
        request_id: REQUEST_ID.to_owned(),
    };
    let mut stream = ModelReplyStream::for_wire(
        &wire,
        RequestId::from_bytes([5; 16]),
        200_000,
        4_096,
        Milestone::M3,
    );
    let mut visible = Vec::new();
    for chunk in body.as_bytes().chunks(1) {
        stream
            .push(chunk, &mut |text| visible.push(text.to_owned()))
            .expect("one byte is a legal chunk");
    }
    let reading = stream.finish(&mut |text| visible.push(text.to_owned()));

    assert_eq!(visible, vec!["Opening it."]);
    let ModelReplyReading::Read(reply) = reading else {
        panic!("the typed terminal must reach the reducer");
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
fn a_held_composed_turn_constructs_the_matching_incremental_decoder() {
    let mut router = RecordingRouter::default();
    router.plans = Some(candidate(true));
    let mut page = LivePage::default();
    let turn = compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &facts(),
        "model-task_1-1",
        &mut page,
        None,
    )
    .expect("a direct turn composes");
    let mut stream =
        ModelReplyStream::for_turn(&turn, RequestId::from_bytes([9; 16]), Milestone::M3);
    let body = concat!(
        "data: {\"type\":\"content_block_delta\",\"delta\":{",
        "\"type\":\"text_delta\",\"text\":\"visible\"}}\n\n",
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},",
        "\"usage\":{\"input_tokens\":7,\"output_tokens\":2}}\n\n",
    );
    let mut visible = Vec::new();
    for chunk in body.as_bytes().chunks(2) {
        stream
            .push(chunk, &mut |text| visible.push(text.to_owned()))
            .expect("two-byte chunks decode");
    }
    let ModelReplyReading::Read(reading) = stream.finish(&mut |text| visible.push(text.to_owned()))
    else {
        panic!("the held direct turn settles");
    };
    assert_eq!(visible, vec!["visible"]);
    assert_eq!(reading.stop, ModelStopReason::Complete);
    assert_eq!(reading.usage.input_units, 7);
}
