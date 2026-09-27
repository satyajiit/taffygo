// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The envelope the subscription endpoint reads its own output back in.
//!
//! A third file beside `subscription_request.rs` and `subscription_reach.rs`,
//! and split from the first for the reason that one was split from the
//! second: those are about what the body *carries* and where a credential
//! *routes*, and this is about the shape the endpoint insists a replayed turn
//! wears. They share a fixture and nothing else.
//!
//! Every rule here fails the same way when it is broken, which is why they
//! are together: this endpoint answers a document it did not expect by
//! closing the connection before headers, so a wrong shape arrives as a
//! stream that never started rather than as anything a person could read.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::json::{parse, JsonValue};
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{Speaker, Turn, WireRequest};
use model_router::wire::write_request;

/// A conversation name, in the shape the kernel's derivation produces.
const CONVERSATION: &str = "0123456789abcdef0123456789abcdef";

fn asking() -> [Turn<'static>; 1] {
    const PIECES: [&str; 1] = ["what does this page say"];
    [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }]
}

fn body(api: WireApi, request: &WireRequest<'_>) -> JsonValue {
    let mut out = String::new();
    write_request(api, request, &mut out).expect("the family writes a body");
    parse(&out).expect("the body it wrote is JSON")
}

// --- the conversation name ---------------------------------------------------

#[test]
fn the_subscription_body_names_the_conversation_and_the_others_do_not() {
    // The endpoint reads this as which cached prefix a turn belongs to, so it
    // has to be the same on every turn of one task and different between two.
    // The value is the caller's; what this asserts is that the family writes
    // it where that endpoint reads it, and that no other family writes it at
    // all — a hint one vendor understands is an unknown field to the rest.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let mut request = common::wire_request(&plan, &turns);
    request.conversation_key = Some(CONVERSATION);

    assert_eq!(
        body(WireApi::OpenAiCodexResponses, &request)
            .field("prompt_cache_key")
            .and_then(JsonValue::as_str),
        Some(CONVERSATION)
    );
    for api in [
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::AnthropicMessages,
        WireApi::GoogleGenerativeLanguage,
    ] {
        assert!(
            body(api, &request).field("prompt_cache_key").is_none(),
            "{api:?} has no such field and must write nothing into it"
        );
    }
}

#[test]
fn a_call_that_names_no_conversation_writes_no_key() {
    // Written nowhere rather than written empty. An endpoint reading an empty
    // conversation name is worse than one reading none: it is a name every
    // conversation on the device would share.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let mut request = common::wire_request(&plan, &turns);
    assert!(request.conversation_key.is_none());
    assert!(body(WireApi::OpenAiCodexResponses, &request)
        .field("prompt_cache_key")
        .is_none());

    request.conversation_key = Some("");
    assert!(body(WireApi::OpenAiCodexResponses, &request)
        .field("prompt_cache_key")
        .is_none());
}

// --- the envelope the endpoint reads its own output back in -------------------

#[test]
fn a_replayed_assistant_turn_wears_the_shape_the_endpoint_emitted() {
    // This endpoint reads the conversation back as its own **output** items,
    // so a replayed assistant turn has to look like the item it produced: it
    // announces its kind, says it completed, is identified, and its text
    // block carries the annotation list the family always emits. A body that
    // replays the model's own words in the input-message shape instead is a
    // different document, and the endpoint answers a different document by
    // closing the connection before headers.
    const SAID: [&str; 1] = ["Reading the page first."];
    let turns = [
        Turn::Said {
            speaker: Speaker::User,
            text: &["what does this page say"],
        },
        Turn::Said {
            speaker: Speaker::Assistant,
            text: &SAID,
        },
    ];
    let plan = ThinkingPlan::Disabled;
    let document = body(
        WireApi::OpenAiCodexResponses,
        &common::wire_request(&plan, &turns),
    );
    let input = document
        .field("input")
        .and_then(JsonValue::as_array)
        .expect("the conversation");
    assert_eq!(input.len(), 2);

    // The person's turn is an input message on this family as on every other,
    // and gains none of the envelope.
    assert!(input[0].field("type").is_none());
    assert!(input[0].field("status").is_none());
    assert!(input[0].field("id").is_none());

    let said = &input[1];
    assert_eq!(
        said.field("type").and_then(JsonValue::as_str),
        Some("message")
    );
    assert_eq!(
        said.field("role").and_then(JsonValue::as_str),
        Some("assistant")
    );
    assert_eq!(
        said.field("status").and_then(JsonValue::as_str),
        Some("completed")
    );
    // The identity is this writer's and is a position inside this one body:
    // the durable transcript never kept the item id the vendor minted, and an
    // invented durable-looking one would be worse than a count.
    assert_eq!(
        said.field("id").and_then(JsonValue::as_str),
        Some("msg_taffy_0")
    );
    let content = said
        .field("content")
        .and_then(JsonValue::as_array)
        .expect("the blocks");
    assert_eq!(content.len(), 1);
    assert_eq!(
        content[0].field("text").and_then(JsonValue::as_str),
        Some(SAID[0])
    );
    let annotations = content[0]
        .field("annotations")
        .and_then(JsonValue::as_array)
        .expect("written, and empty: what was kept is the text");
    assert!(annotations.is_empty());
}

#[test]
fn no_other_family_wears_that_envelope() {
    // The envelope is a row fact. A family that names none writes none, and
    // its replayed assistant turn is the bare role and content it always was.
    const SAID: [&str; 1] = ["Reading the page first."];
    let turns = [
        Turn::Said {
            speaker: Speaker::User,
            text: &["what does this page say"],
        },
        Turn::Said {
            speaker: Speaker::Assistant,
            text: &SAID,
        },
    ];
    let plan = ThinkingPlan::Disabled;
    for api in [
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::AnthropicMessages,
    ] {
        let document = body(api, &common::wire_request(&plan, &turns));
        let input = document
            .field(match api {
                WireApi::AnthropicMessages => "messages",
                _ => "input",
            })
            .or_else(|| document.field("messages"))
            .and_then(JsonValue::as_array)
            .unwrap_or_else(|| panic!("{api:?} writes a conversation"));
        let said = input.last().expect("the model's turn");
        assert!(said.field("status").is_none(), "{api:?}");
        assert!(said.field("id").is_none(), "{api:?}");
    }
}

#[test]
fn only_the_subscription_family_states_a_strictness_it_has_no_opinion_about() {
    // `null` is not `false` and is not an absent key. `false` asks the family
    // not to constrain sampling; `null` leaves its own default in place; an
    // absent key is a third answer, and it is the one this row was giving.
    const SCHEMA: &str = r#"{"type":"object","properties":{}}"#;

    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let mut request = common::wire_request(&plan, &turns);
    let parsed = parse(SCHEMA).expect("a schema");
    let tools = [model_router::wire::request::ToolDeclaration {
        name: "read_page",
        description: "Read the page.",
        parameters: &parsed,
    }];
    request.tools = &tools;

    let subscription = body(WireApi::OpenAiCodexResponses, &request);
    let declared = subscription
        .field("tools")
        .and_then(JsonValue::as_array)
        .expect("the tools");
    assert!(
        matches!(declared[0].field("strict"), Some(JsonValue::Null)),
        "written, and written as null"
    );

    let platform = body(WireApi::OpenAiResponses, &request);
    let platform_tools = platform
        .field("tools")
        .and_then(JsonValue::as_array)
        .expect("the tools");
    assert!(platform_tools[0].field("strict").is_none());
}
