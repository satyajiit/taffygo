// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Managed and direct routes preserve the same tool transcript identity walk.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use model_router::catalog::WireApi;
use model_router::json::{parse, JsonValue};
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{Speaker, ToolCallReplay, ToolResultView, Turn, WireRequest};
use model_router::wire::{dialect_for, write_managed_request, write_request, ManagedRequest};

const REQUEST_ID: &str = "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00";

fn minted_call_id(ordinal: u64, sequence: u32) -> String {
    format!("turn-{ordinal}-call-{sequence}")
}

fn managed<'a>(turns: &'a [Turn<'a>], thinking: &'a ThinkingPlan) -> ManagedRequest<'a> {
    ManagedRequest {
        request_id: REQUEST_ID,
        model_id: "reasoner-one",
        system: Some("You are Taffy."),
        turns,
        tools: &[],
        thinking,
        answer_tokens: 4_096,
    }
}

/// Every string in `document` that is one of the minted identities, in the
/// order the body carries them.
///
/// An object's own keys are walked in sorted order rather than document order,
/// which does not matter here: one call or one result carries one identity, and
/// the steps being compared are elements of an array, whose order is the
/// document's.
fn identity_walk(document: &JsonValue, minted: &[String]) -> Vec<String> {
    match document {
        JsonValue::Text(text) => {
            if minted.iter().any(|id| id == text) {
                vec![text.clone()]
            } else {
                Vec::new()
            }
        }
        JsonValue::Array(items) => items
            .iter()
            .flat_map(|item| identity_walk(item, minted))
            .collect(),
        JsonValue::Object(map) => map
            .values()
            .flat_map(|item| identity_walk(item, minted))
            .collect(),
        JsonValue::Null | JsonValue::Bool(_) | JsonValue::Integer(_) | JsonValue::Decimal(_) => {
            Vec::new()
        }
    }
}

#[test]
fn a_managed_transcript_and_a_direct_transcript_walk_identically() {
    // The same three-shape transcript, written twice: once to the managed
    // schema and once through each dialect. What must not differ is the walk —
    // the same steps, with the same identities, in the same order — because a
    // recorded procedure has to replay unchanged after a person pastes their
    // own key, and unchanged again when that key expires and the managed route
    // serves the next turn (decision 0092 section 3).
    let first = minted_call_id(12, 3);
    let second = minted_call_id(13, 1);
    let read_arguments = parse(r#"{"tab":1}"#).unwrap();
    let click_arguments = parse(r#"{"node":8817,"tab":1}"#).unwrap();
    let read_calls = [ToolCallReplay {
        call_id: &first,
        tool: "read_page",
        arguments: &read_arguments,
    }];
    let click_calls = [ToolCallReplay {
        call_id: &second,
        tool: "click",
        arguments: &click_arguments,
    }];
    let read_results = [ToolResultView {
        call_id: &first,
        tool: "read_page",
        is_error: false,
        text: &["a headline".to_owned()],
    }];
    let click_results = [ToolResultView {
        call_id: &second,
        tool: "click",
        is_error: true,
        text: &["the tab was closed".to_owned()],
    }];
    let turns = [
        Turn::Said {
            speaker: Speaker::User,
            text: &["Open the headline."],
        },
        Turn::Called {
            text: &["Reading the page first."],
            calls: &read_calls,
            reasoning: &[],
        },
        Turn::Returned {
            results: &read_results,
        },
        Turn::Called {
            text: &[],
            calls: &click_calls,
            reasoning: &[],
        },
        Turn::Returned {
            results: &click_results,
        },
    ];
    let minted = [first.clone(), second.clone()];
    let thinking = ThinkingPlan::Disabled;

    let mut managed_body = String::new();
    write_managed_request(&managed(&turns, &thinking), &mut managed_body)
        .expect("the managed schema carries the exchange");
    let managed_walk = identity_walk(&parse(&managed_body).unwrap(), &minted);
    assert_eq!(
        managed_walk,
        vec![first.clone(), first.clone(), second.clone(), second.clone()],
        "each call and the result answering it, in order"
    );

    let direct = WireRequest {
        model_id: "a-model",
        tool_calling: true,
        system: Some("You are Taffy."),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &thinking,
        answer_tokens: 4_096,
        stream: false,
    };
    let mut carried = 0;
    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage,
    ] {
        let mut body = String::new();
        write_request(api, &direct, &mut body).expect("the family writes a body");
        let walk = identity_walk(&parse(&body).unwrap(), &minted);
        if dialect_for(api).calls.call_id_key.is_some() {
            carried += 1;
            assert_eq!(walk, managed_walk, "{api:?} walks differently");
        } else {
            // The one family with nowhere to carry an identity pairs a result
            // to its call by the tool's name. Nothing to compare, and nothing
            // renamed either — which is the point.
            assert!(walk.is_empty(), "{api:?} carries an identity after all");
        }
    }
    assert_eq!(carried, 3, "three of the four families carry an identity");
}
