// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The document each family is sent for one tool exchange.
//!
//! They are the point of the file, and they are the only place the shapes
//! can be read side by side: a turn that said something and called a tool, the
//! result of that call, a turn that only called, and a result that failed. A
//! diff in one of them is the whole review.
//!
//! Byte-exact rather than compared as structure, because the bytes are what a
//! provider is sent — a comparison of parsed documents would agree with a body
//! whose fields had been reordered or whose numbers had changed shape, and
//! neither of those is a change nobody needs to see.
//!
//! What is not claimed: that a provider accepts them. These are published
//! shapes written by hand, and the recorded-fixture conformance suite the
//! registry document specifies is separate and does not exist yet. To renew
//! them after a deliberate change, read what the writer produces and put it
//! back here — never the other way round.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::json::parse;
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{
    Speaker, ToolCallReplay, ToolDeclaration, ToolResultView, Turn, WireRequest,
};
use model_router::wire::{write_request, MAX_BODY_BYTES};

/// Writes one body, or fails the test naming the family.
fn body(api: WireApi, turns: &[Turn<'_>], tools: &[ToolDeclaration<'_>]) -> String {
    let plan = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "a-model",
        tool_calling: true,
        system: Some("Be brief."),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns,
        tools,
        // Set for every family, and written by one. A production body on the
        // subscription family always names its conversation, so a golden
        // without it would be a body that could not actually be sent; the
        // other four have nowhere to put it, and their documents are proof
        // that offering it changes nothing there.
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: Some("0123456789abcdef0123456789abcdef"),
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let mut out = String::new();
    write_request(api, &request, &mut out).expect("the family writes a body");
    out
}

/// The committed document for one family.
///
/// A match with no catch-all arm, so a further family cannot be added without a
/// document of its own.
const fn golden_for(api: WireApi) -> &'static str {
    match api {
        WireApi::AnthropicMessages => include_str!("golden/tool-exchange-anthropic-messages.json"),
        WireApi::OpenAiResponses => include_str!("golden/tool-exchange-openai-responses.json"),
        WireApi::OpenAiCompletions => include_str!("golden/tool-exchange-openai-completions.json"),
        WireApi::GoogleGenerativeLanguage => {
            include_str!("golden/tool-exchange-google-generative-language.json")
        }
        WireApi::OpenAiCodexResponses => {
            include_str!("golden/tool-exchange-openai-codex-responses.json")
        }
        WireApi::GoogleCloudCodeAssist => {
            include_str!("golden/tool-exchange-google-cloud-code-assist.json")
        }
    }
}

#[test]
fn every_family_writes_the_exchange_its_golden_document_records() {
    let read_arguments = parse(r#"{"tab":1}"#).unwrap();
    let click_arguments = parse(r#"{"node":8817,"tab":1}"#).unwrap();
    let schema = parse(r#"{"type":"object","properties":{}}"#).unwrap();
    let read_call = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &read_arguments,
    }];
    let click_call = [ToolCallReplay {
        call_id: "turn-13-call-1",
        tool: "click",
        arguments: &click_arguments,
    }];
    let goal = ["Open the headline."];
    let thinking_aloud = ["Reading the page first."];
    let read_result = ["The headline is \"Hello\", at node 8817.".to_owned()];
    let click_result = ["the tab was closed before the click".to_owned()];
    let turns = [
        Turn::Said {
            speaker: Speaker::User,
            text: &goal,
        },
        Turn::Called {
            text: &thinking_aloud,
            calls: &read_call,
            reasoning: &[],
        },
        Turn::Returned {
            results: &[ToolResultView {
                call_id: "turn-12-call-3",
                tool: "read_page",
                is_error: false,
                text: &read_result,
            }],
        },
        // A turn that only called: the model said nothing beside it, which is
        // the ordinary case and the one that shapes two of the four bodies.
        Turn::Called {
            text: &[],
            calls: &click_call,
            reasoning: &[],
        },
        Turn::Returned {
            results: &[ToolResultView {
                call_id: "turn-13-call-1",
                tool: "click",
                is_error: true,
                text: &click_result,
            }],
        },
    ];
    let tools = [ToolDeclaration {
        name: "read_page",
        description: "Read the page.",
        parameters: &schema,
    }];
    for api in common::ALL_FAMILIES {
        let written = body(api, &turns, &tools);
        assert!(
            parse(&written).is_ok(),
            "{api:?} wrote something unreadable"
        );
        assert!(written.len() <= MAX_BODY_BYTES);
        assert_eq!(
            written,
            golden_for(api).trim_end_matches('\n'),
            "{api:?} no longer writes its golden document"
        );
    }
}
