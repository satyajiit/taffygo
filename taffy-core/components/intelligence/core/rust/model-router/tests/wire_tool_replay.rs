// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Replaying a tool call and the result it was given into the next request.
//!
//! Every test is driven by `common::ALL_FAMILIES`, so a family added to the
//! enumeration without the two replay rows fails to compile and a family whose
//! rows are wrong fails here rather than at a provider.
//!
//! What each family's body looks like end to end is `wire_tool_golden.rs`, and
//! what a transcript that has gone wrong earns instead of a body is
//! `wire_tool_refusals.rs`. This file asserts the properties a well-formed
//! exchange has to have: that an identity survives unaltered wherever there is
//! room for one, that the family with room for none pairs by the tool's name
//! instead, that a failure reaches the wire in whatever way each family has,
//! and that arguments are escaped into the body rather than spliced into it.

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
use model_router::wire::dialect::{ArgumentsShape, FailureSignal};
use model_router::wire::request::{
    Speaker, ToolCallReplay, ToolDeclaration, ToolResultView, Turn, WireRequest,
};
use model_router::wire::{dialect_for, write_request};

/// The identity the core mints for the `sequence`-th call of turn `ordinal`.
///
/// `task-engine`'s `turn_call_key` owns the format and this is a copy of it,
/// which is the whole of what this spelling can be trusted for: a fact about
/// *this* crate's table — that the strictest of the four families takes an
/// identity of this shape unaltered — and never a fact about the reducer.
/// That the reducer still mints this shape is asserted where it is minted, by
/// `the_minted_call_identity_satisfies_the_strictest_protocol_family` in
/// `task-engine`'s `agent_tool_calls`; a change made there does not reach
/// here.
fn minted_call_id(ordinal: u64, sequence: u32) -> String {
    format!("turn-{ordinal}-call-{sequence}")
}

/// What a failed result is prefixed with where the family cannot say so.
///
/// The constant itself lives in `wire::turns` and is private. Spelled here so
/// that a change to it is a change to a test as well as to the writer, which
/// is what a marker the model reads should cost.
const FAILED_MARKER: &str = "[the tool failed] ";

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
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let mut out = String::new();
    write_request(api, &request, &mut out).expect("the family writes a body");
    out
}

#[test]
fn the_minted_identity_satisfies_the_strictest_family_by_construction() {
    // One family accepts `[a-zA-Z0-9_-]` up to sixty-four characters and
    // silently rewrites anything else. Nothing in this crate sanitizes an
    // identity, and nothing should: a sanitizer is a repair, and this is the
    // one place a repair would break the pairing it was meant to protect —
    // the call written under a rewritten spelling and its result under the
    // original, or the reverse, depending on which side ran first.
    for (ordinal, sequence) in [(0, 0), (1, 3), (12, 3), (u64::MAX, u32::MAX)] {
        let id = minted_call_id(ordinal, sequence);
        assert!(id.len() <= 64, "{id} is longer than the family accepts");
        assert!(
            id.chars()
                .all(|letter| letter.is_ascii_alphanumeric() || letter == '_' || letter == '-'),
            "{id} carries a character the family would rewrite"
        );
    }
}

#[test]
fn the_identity_reaches_the_body_unaltered_wherever_there_is_room_for_one() {
    let id = minted_call_id(12, 3);
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: &id,
        tool: "read_page",
        arguments: &arguments,
    }];
    let answer = ["a headline".to_owned()];
    let turns = [
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &[],
        },
        Turn::Returned {
            results: &[ToolResultView {
                call_id: &id,
                tool: "read_page",
                is_error: false,
                text: &answer,
            }],
        },
    ];
    let mut carried = 0;
    for api in common::ALL_FAMILIES {
        let written = body(api, &turns, &[]);
        if dialect_for(api).calls.call_id_key.is_some() {
            carried += 1;
            assert!(written.contains(&id), "{api:?} altered the identity");
        } else {
            assert!(
                !written.contains(&id),
                "{api:?} has no field for one, so writing it would invent a field"
            );
        }
    }
    assert_eq!(carried, 5, "every family but one carries an identity");
}

#[test]
fn the_family_that_mints_no_identity_pairs_a_result_to_its_call_by_name() {
    let api = WireApi::GoogleGenerativeLanguage;
    let dialect = dialect_for(api);
    assert!(dialect.calls.call_id_key.is_none());
    assert_eq!(dialect.results.name_key, Some("name"));
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &arguments,
    }];
    let answer = ["a headline".to_owned()];
    let turns = [
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &[],
        },
        Turn::Returned {
            results: &[ToolResultView {
                call_id: "turn-12-call-3",
                tool: "read_page",
                is_error: false,
                text: &answer,
            }],
        },
    ];
    let document = parse(&body(api, &turns, &[])).unwrap();
    let contents = document.field("contents").unwrap().as_array().unwrap();
    let called = part(&contents[0], "functionCall");
    let returned = part(&contents[1], "functionResponse");
    assert_eq!(
        called.field("name").and_then(JsonValue::as_str),
        returned.field("name").and_then(JsonValue::as_str),
        "the name is the only thing pairing them"
    );
    assert_eq!(
        called.field("name").and_then(JsonValue::as_str),
        Some("read_page")
    );
}

/// The first part of `content` carrying `key`.
fn part<'a>(content: &'a JsonValue, key: &str) -> &'a JsonValue {
    content
        .field("parts")
        .and_then(JsonValue::as_array)
        .and_then(|parts| parts.iter().find_map(|item| item.field(key)))
        .unwrap_or_else(|| panic!("no part carries {key}"))
}

#[test]
fn a_failure_reaches_the_wire_in_whatever_way_the_family_has() {
    // One test over all four rows rather than four tests, because what is
    // asserted is that no row loses it. Two have a field — a boolean on one,
    // the payload's own field name on the other — and two have nowhere at all,
    // where it is folded into the text. Folding is never used where a field
    // exists, because a marker in prose is the worse of the two answers.
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &arguments,
    }];
    let called = Turn::Called {
        text: &[],
        calls: &calls,
        reasoning: &[],
    };
    let answer = ["the tab was closed".to_owned()];
    let result = |is_error| {
        [ToolResultView {
            call_id: "turn-12-call-3",
            tool: "read_page",
            is_error,
            text: &answer,
        }]
    };
    for api in common::ALL_FAMILIES {
        let worked = result(false);
        let failed = result(true);
        let after_success = body(api, &[called, Turn::Returned { results: &worked }], &[]);
        let after_failure = body(api, &[called, Turn::Returned { results: &failed }], &[]);
        assert_ne!(
            after_success, after_failure,
            "{api:?} says nothing about a tool that failed"
        );
        let folded = matches!(
            dialect_for(api).results.failure,
            FailureSignal::FoldedIntoText
        );
        assert_eq!(after_failure.contains(FAILED_MARKER), folded, "{api:?}");
        assert!(!after_success.contains(FAILED_MARKER), "{api:?}");
        match dialect_for(api).results.failure {
            FailureSignal::Flag { key } => {
                assert!(
                    after_failure.contains(&format!(r#""{key}":true"#)),
                    "{api:?}"
                );
                assert!(!after_success.contains(key), "{api:?}");
            }
            FailureSignal::PayloadKey { key } => {
                assert!(after_failure.contains(&format!(r#""{key}":"#)), "{api:?}");
                assert!(!after_success.contains(key), "{api:?}");
            }
            FailureSignal::FoldedIntoText => {}
        }
    }
}

#[test]
fn arguments_are_escaped_into_the_body_rather_than_spliced_into_it() {
    // The value is a string that closes its own literal and opens a field of
    // its own. Written as bytes it would add that field to the body; written
    // as a value it is characters of a string on every family, which is what
    // `emit::Json` exists to guarantee and why a replayed call carries a
    // decoded value rather than the text a reply delivered.
    //
    // The field it tries to open is named for this test rather than for
    // something a family already writes — `role` would have matched the turn
    // one family gives the standing instruction and reported a splice that had
    // not happened.
    let hostile = parse(r#"{"q":"\",\"spliced\":\"yes\",\"x\":\""}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &hostile,
    }];
    let answer = ["done".to_owned()];
    let turns = [
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &[],
        },
        Turn::Returned {
            results: &[ToolResultView {
                call_id: "turn-12-call-3",
                tool: "read_page",
                is_error: false,
                text: &answer,
            }],
        },
    ];
    let mut embedded = 0;
    for api in common::ALL_FAMILIES {
        let written = body(api, &turns, &[]);
        assert!(
            !written.contains(r#""spliced":"yes""#),
            "{api:?} spliced the value into the body as structure"
        );
        let document = parse(&written).expect("the body is still readable");
        if matches!(
            dialect_for(api).calls.arguments,
            ArgumentsShape::EmbeddedText
        ) {
            embedded += 1;
            assert!(
                carries_embedded(&document, &hostile),
                "{api:?} lost the arguments on the way into the string"
            );
        }
    }
    assert_eq!(
        embedded, 3,
        "three families want a document inside a string"
    );
}

/// Whether any string anywhere in `value` is a JSON document equal to `wanted`.
fn carries_embedded(value: &JsonValue, wanted: &JsonValue) -> bool {
    match value {
        JsonValue::Text(text) => parse(text).is_ok_and(|parsed| &parsed == wanted),
        JsonValue::Array(items) => items.iter().any(|item| carries_embedded(item, wanted)),
        JsonValue::Object(map) => map.values().any(|item| carries_embedded(item, wanted)),
        JsonValue::Null | JsonValue::Bool(_) | JsonValue::Integer(_) | JsonValue::Decimal(_) => {
            false
        }
    }
}

#[test]
fn no_view_on_the_write_path_can_hold_an_owned_string() {
    // `String` is not `Copy`, and `Copy` is required of every field of a `Copy`
    // type at every depth. So this bound is the module's own claim — "there is
    // no owned `String` in a single one of them" — proved by the type system
    // rather than by reading the fields, and it survives a field added later.
    fn borrowed_view<T: Copy>() {}
    borrowed_view::<Speaker>();
    borrowed_view::<Turn<'static>>();
    borrowed_view::<ToolCallReplay<'static>>();
    borrowed_view::<ToolResultView<'static>>();
    borrowed_view::<ToolDeclaration<'static>>();
    borrowed_view::<WireRequest<'static>>();
}
