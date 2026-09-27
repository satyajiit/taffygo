// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a transcript that has gone wrong earns instead of a body.
//!
//! Every member here is a refusal, and not one of them repairs anything
//! (decision 0069 section 8). The repair worth naming is the one a
//! normalization layer reaches for first: synthesizing a result for a call the
//! transcript never answered. What that writes into the body is a sentence
//! saying a tool failed when nothing ran, and a model has no way to tell it
//! from a tool that really did — so the builder defect that produced the
//! unpaired turn is hidden by telling the model something untrue.
//!
//! A refusal also has to leave the caller's buffer exactly as it found it. A
//! caller writing several bodies into one buffer must not have to unwind a
//! partial one, so `refusal` asserts that on every case rather than each test
//! remembering to.

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
use model_router::wire::reply::MAX_TOOL_CALLS;
use model_router::wire::request::{
    Speaker, ToolCallReplay, ToolResultView, Turn, WireRefusal, WireRequest,
};
use model_router::wire::write_request;

/// The identity the core mints for the `sequence`-th call of turn `ordinal`.
fn minted_call_id(ordinal: u64, sequence: u32) -> String {
    format!("turn-{ordinal}-call-{sequence}")
}

/// The refusal one conversation earns from `api`, with the buffer checked.
///
/// A refusal has to leave the caller's buffer exactly as it found it — a
/// caller writing several bodies into one buffer must not have to unwind a
/// partial one — so that is asserted here rather than once per test.
fn refusal(api: WireApi, turns: &[Turn<'_>], tool_calling: bool) -> WireRefusal {
    let plan = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "a-model",
        tool_calling,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let mut out = String::from("prefix:");
    let refused = write_request(api, &request, &mut out).expect_err("the conversation is refused");
    assert_eq!(out, "prefix:", "{api:?} left a partial body behind");
    refused
}

#[test]
fn a_replayed_exchange_needs_a_model_that_can_answer_with_a_tool_call() {
    // The same refusal a tool vocabulary earns, for the same reason: a model
    // that cannot call a tool cannot be shown a transcript in which it did.
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &arguments,
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
    for api in common::ALL_FAMILIES {
        assert_eq!(
            refusal(api, &turns, false),
            WireRefusal::ToolsUnsupportedByModel,
            "{api:?}"
        );
    }
}

#[test]
fn half_an_exchange_is_refused_rather_than_repaired() {
    // Synthesizing the missing half is the repair that would paper over the
    // builder defect this refusal exists to stop. Neither shape is written:
    // one would be a turn with an empty content array the provider rejects,
    // the other would vanish from the body and leave the model waiting.
    let said = ["ok"];
    let spoke = Turn::Said {
        speaker: Speaker::User,
        text: &said,
    };
    for (index, turn) in [
        Turn::Called {
            text: &said,
            calls: &[],
            reasoning: &[],
        },
        Turn::Returned { results: &[] },
    ]
    .into_iter()
    .enumerate()
    {
        assert_eq!(
            refusal(WireApi::AnthropicMessages, &[spoke, turn], true),
            WireRefusal::EmptyToolTurn { turn_index: 1 },
            "case {index}"
        );
    }
}

#[test]
fn an_unpaired_call_or_result_is_refused_rather_than_synthesized() {
    // The repair a normalization layer reaches for first is inserting "no
    // result provided" for a call the transcript never answered. What that
    // writes into the body is a sentence saying a tool failed when nothing
    // ran, and the model has no way to tell it from a tool that really did.
    // Both directions are refused, and the second is the worse one: a result
    // answering a call nobody made reads as an answer to a question the model
    // never asked.
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &arguments,
    }];
    let answer = ["a headline".to_owned()];
    let unanswered = [Turn::Called {
        text: &[],
        calls: &calls,
        reasoning: &[],
    }];
    assert_eq!(
        refusal(WireApi::AnthropicMessages, &unanswered, true),
        WireRefusal::CallWithoutResult {
            turn_index: 0,
            call_index: 0
        }
    );
    let unasked = [Turn::Returned {
        results: &[ToolResultView {
            call_id: "turn-12-call-3",
            tool: "read_page",
            is_error: false,
            text: &answer,
        }],
    }];
    assert_eq!(
        refusal(WireApi::AnthropicMessages, &unasked, true),
        WireRefusal::ResultWithoutCall {
            turn_index: 0,
            result_index: 0
        }
    );
}

#[test]
fn a_turn_replaying_more_calls_than_a_reply_may_carry_is_refused() {
    // The reader's own ceiling. A writer stricter than the reader would refuse
    // a conversation the process had already accepted; a looser one would
    // write back a conversation the reader refused.
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let ids = (0..=MAX_TOOL_CALLS)
        .map(|sequence| minted_call_id(12, u32::try_from(sequence).unwrap()))
        .collect::<Vec<_>>();
    let calls = ids
        .iter()
        .map(|id| ToolCallReplay {
            call_id: id,
            tool: "read_page",
            arguments: &arguments,
        })
        .collect::<Vec<_>>();
    let turns = [Turn::Called {
        text: &[],
        calls: &calls,
        reasoning: &[],
    }];
    assert_eq!(
        refusal(WireApi::AnthropicMessages, &turns, true),
        WireRefusal::TooManyToolCalls { turn_index: 0 }
    );
}

#[test]
fn replayed_arguments_that_nest_too_deeply_are_refused() {
    // The same bound as a tool's schema, for the same reason: the writer walks
    // the value recursively, so how much stack that costs is decided here and
    // not by whatever the reply happened to carry.
    let mut arguments = JsonValue::Integer(1);
    for _ in 0..model_router::json::MAX_DEPTH + 2 {
        let mut map = std::collections::BTreeMap::new();
        map.insert("a".to_owned(), arguments);
        arguments = JsonValue::Object(map);
    }
    let calls = [ToolCallReplay {
        call_id: "turn-12-call-3",
        tool: "read_page",
        arguments: &arguments,
    }];
    let said = ["ok"];
    let turns = [
        Turn::Said {
            speaker: Speaker::User,
            text: &said,
        },
        Turn::Called {
            text: &[],
            calls: &calls,
            reasoning: &[],
        },
    ];
    assert_eq!(
        refusal(WireApi::AnthropicMessages, &turns, true),
        WireRefusal::ToolArgumentsTooDeep {
            turn_index: 1,
            call_index: 0
        }
    );
}

#[test]
fn a_foreign_call_identity_is_refused_rather_than_rewritten() {
    // Decision 0069 sections 4–5: identities are minted, never renamed. The
    // reference implementation rewrites `|` to `_` and slices to 64
    // characters. Applied here that would map two distinct calls onto one
    // string, and the failure is a result delivered against the wrong call
    // with no error anywhere. A paired foreign id is still a pairing — the
    // two sides match — so this is not `CallWithoutResult`; it is an
    // identity the core never minted, refused rather than rewritten into the
    // body. `refusal` also asserts the caller's buffer is left untouched.
    let arguments = parse(r#"{"tab":1}"#).unwrap();
    let long = "a".repeat(65);
    for foreign in ["resp_abc|tool_0", long.as_str()] {
        let calls = [ToolCallReplay {
            call_id: foreign,
            tool: "read_page",
            arguments: &arguments,
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
                    call_id: foreign,
                    tool: "read_page",
                    is_error: false,
                    text: &answer,
                }],
            },
        ];
        for api in common::ALL_FAMILIES {
            assert_eq!(
                refusal(api, &turns, true),
                WireRefusal::InvalidCallId {
                    turn_index: 0,
                    index: 0
                },
                "{api:?} {foreign}"
            );
        }
    }
}
