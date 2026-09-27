// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the subscription way in changes about a request and a reply.
//!
//! The other half of `subscription_reach.rs`: that file is about which family
//! and which address a credential resolves to, and this one is about what the
//! body then carries. They are separate because the first needs a catalog and
//! a router and the second needs neither.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::credential::AuthType;
use model_router::ids::RequestId;
use model_router::json::{parse, JsonValue};
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{Speaker, Turn, WireRefusal, WireRequest};
use model_router::wire::{read_reply, write_request, ReplyContext};

fn asking() -> [Turn<'static>; 1] {
    const PIECES: [&str; 1] = ["what does this page say"];
    [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }]
}

// --- what the subscription family carries ------------------------------------

fn body(api: WireApi, request: &WireRequest<'_>) -> JsonValue {
    let mut out = String::new();
    write_request(api, request, &mut out).expect("the family writes a body");
    parse(&out).expect("the body it wrote is JSON")
}

#[test]
fn the_subscription_responses_body_carries_what_the_platform_endpoint_refuses() {
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let document = body(
        WireApi::OpenAiCodexResponses,
        &common::wire_request(&plan, &turns),
    );
    assert_eq!(
        document.field("store").and_then(JsonValue::as_bool),
        Some(false),
        "the endpoint refuses a request that asks it to keep the answer"
    );
    let include = document
        .field("include")
        .and_then(JsonValue::as_array)
        .expect("the reasoning has to be asked for");
    assert_eq!(include.len(), 1);
    assert_eq!(
        include[0].as_str(),
        Some("reasoning.encrypted_content"),
        "without it the next turn has nothing to hand back"
    );
    // The standing instruction is the field, not a turn: a `system` turn is
    // read here as something the person said.
    assert_eq!(
        document.field("instructions").and_then(JsonValue::as_str),
        Some("Be brief.")
    );
}

#[test]
fn the_platform_responses_body_carries_neither() {
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let document = body(
        WireApi::OpenAiResponses,
        &common::wire_request(&plan, &turns),
    );
    assert!(document.field("store").is_none());
    assert!(document.field("include").is_none());
}

// --- the sealed reasoning round trip -----------------------------------------

const SEALED: &str = "gAAAAABo-sealed-thinking";

fn reasoning_reply() -> JsonValue {
    parse(&format!(
        r#"{{"status":"completed","error":null,"incomplete_details":null,
            "output":[{{"type":"reasoning","id":"rs_1","encrypted_content":"{SEALED}"}},
                      {{"type":"function_call","call_id":"call_1","name":"read_page",
                        "arguments":"{{\"tab\":1}}"}}],
            "usage":{{"input_tokens":100,"output_tokens":20}}}}"#
    ))
    .expect("the reply is JSON")
}

fn reply_context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([4; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

#[test]
fn sealed_reasoning_comes_back_out_of_the_reply_and_goes_back_in_unchanged() {
    let reply = reasoning_reply();
    let outcome = read_reply(WireApi::OpenAiCodexResponses, &reply, &reply_context())
        .expect("the reply reads");
    assert_eq!(outcome.reading.reasoning.len(), 1);
    assert!(
        !outcome
            .reading
            .text
            .iter()
            .any(|piece| piece.contains("sealed")),
        "sealed thinking is not part of what the model said"
    );

    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [model_router::wire::ToolCallReplay {
        call_id: "turn-1-call-1",
        tool: "read_page",
        arguments: &arguments,
    }];
    let results = [model_router::wire::ToolResultView {
        call_id: "turn-1-call-1",
        tool: "read_page",
        is_error: false,
        text: &["a headline".to_owned()],
    }];
    let asked = asking();
    let turns = [
        asked[0],
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &outcome.reading.reasoning,
        },
        Turn::Returned { results: &results },
    ];
    let plan = ThinkingPlan::Disabled;
    let mut request = common::wire_request(&plan, &turns);
    request.tool_calling = true;
    let mut written = String::new();
    write_request(WireApi::OpenAiCodexResponses, &request, &mut written)
        .expect("the family writes a body");
    assert!(
        written.contains(SEALED),
        "the next turn of the same conversation has to carry it verbatim"
    );
    assert!(
        written.contains("\"type\":\"reasoning\""),
        "the item goes back whole, not just the payload inside it"
    );
}

#[test]
fn a_family_with_no_carriage_writes_no_reasoning_even_when_handed_some() {
    let reply = reasoning_reply();
    let outcome = read_reply(WireApi::OpenAiCodexResponses, &reply, &reply_context())
        .expect("the reply reads");
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [model_router::wire::ToolCallReplay {
        call_id: "turn-1-call-1",
        tool: "read_page",
        arguments: &arguments,
    }];
    let results = [model_router::wire::ToolResultView {
        call_id: "turn-1-call-1",
        tool: "read_page",
        is_error: false,
        text: &["a headline".to_owned()],
    }];
    let asked = asking();
    let turns = [
        asked[0],
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &outcome.reading.reasoning,
        },
        Turn::Returned { results: &results },
    ];
    let plan = ThinkingPlan::Disabled;
    let mut request = common::wire_request(&plan, &turns);
    request.tool_calling = true;
    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
    ] {
        let mut written = String::new();
        write_request(api, &request, &mut written).expect("the family writes a body");
        assert!(
            !written.contains(SEALED),
            "{api:?} has no field for another family's sealed thinking"
        );
    }
}

#[test]
fn sealed_reasoning_cannot_be_rendered_or_recorded() {
    // The structural half. A projection or an audit record reaches a value
    // like this through `Debug` or through serde, and there is nothing here
    // for either of them to reach: the only accessor is module-private, so
    // this test can assert what a leak would look like without being able to
    // produce one.
    let reply = reasoning_reply();
    let outcome = read_reply(WireApi::OpenAiCodexResponses, &reply, &reply_context())
        .expect("the reply reads");
    let printed = format!("{:?}", outcome.reading);
    assert!(
        !printed.contains(SEALED),
        "debug output is where sealed material leaks first"
    );
    assert!(printed.contains("OpaqueReasoning(sealed)"));
}

// --- the subscription preamble on the other vendor ---------------------------

const PREAMBLE: &str = "You are Taffy, the assistant built into this browser.";

fn anthropic_body(method: Option<AuthType>) -> JsonValue {
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let request = WireRequest {
        system_preamble: Some(PREAMBLE),
        credential_method: method,
        ..common::wire_request(&plan, &turns)
    };
    body(WireApi::AnthropicMessages, &request)
}

#[test]
fn a_key_credential_carries_no_preamble_at_all() {
    let document = anthropic_body(Some(AuthType::ApiKey));
    assert_eq!(
        document.field("system").and_then(JsonValue::as_str),
        Some("Be brief."),
        "the instruction stays the string it has always been"
    );
}

#[test]
fn a_subscription_credential_carries_the_preamble_first_and_once() {
    let document = anthropic_body(Some(AuthType::Oauth));
    let blocks = document
        .field("system")
        .and_then(JsonValue::as_array)
        .expect("the instruction becomes a list when it has a preamble");
    assert_eq!(blocks.len(), 2);
    assert_eq!(
        blocks[0].field("type").and_then(JsonValue::as_str),
        Some("text")
    );
    assert_eq!(
        blocks[0].field("text").and_then(JsonValue::as_str),
        Some(PREAMBLE),
        "the vendor requires it first, so it cannot follow the instruction"
    );
    assert_eq!(
        blocks[1].field("text").and_then(JsonValue::as_str),
        Some("Be brief.")
    );
    assert_eq!(
        format!("{document:?}").matches(PREAMBLE).count(),
        1,
        "written twice it reads to the model as an instruction repeated for emphasis"
    );
}

#[test]
fn a_family_with_nowhere_to_put_a_preamble_refuses_rather_than_dropping_it() {
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let request = WireRequest {
        system_preamble: Some(PREAMBLE),
        credential_method: Some(AuthType::Oauth),
        ..common::wire_request(&plan, &turns)
    };
    let mut out = String::new();
    assert_eq!(
        write_request(WireApi::OpenAiCodexResponses, &request, &mut out),
        Err(WireRefusal::PreambleUnsupportedByFamily)
    );
    assert!(out.is_empty(), "a refusal leaves the buffer as it found it");
}
