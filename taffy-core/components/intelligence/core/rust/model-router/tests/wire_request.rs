// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What each row of the family table actually writes.
//!
//! Every test is driven by `common::ALL_FAMILIES`, so a family added to the
//! enumeration without a table row fails to compile and a family whose row is
//! wrong fails here rather than at a provider.
//!
//! The bodies are synthetic and small on purpose. What is proved is that the
//! table places a field where that family puts it — not that a real provider
//! accepts the result, which is the recorded-fixture conformance suite the
//! registry document specifies separately and which nothing here claims to be.

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
use model_router::wire::request::{Speaker, ToolDeclaration, Turn, WireRefusal, WireRequest};
use model_router::wire::{dialect_for, write_request};

fn asking() -> [Turn<'static>; 1] {
    const PIECES: [&str; 1] = ["the page said this"];
    [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }]
}

#[test]
fn every_family_has_a_row_and_writes_a_body_carrying_the_conversation() {
    for api in common::ALL_FAMILIES {
        let plan = common::plan_for(api);
        let turns = asking();
        let dialect = dialect_for(api);
        assert_eq!(
            dialect.family, api,
            "a row describes the family it is under"
        );
        let document = common::wire_body(api, &common::wire_request(&plan, &turns));
        let written = common::request_root(api, &document)
            .field(dialect.turns_key)
            .and_then(JsonValue::as_array)
            .expect("the conversation is where the row says it is");
        assert!(!written.is_empty());
        assert_eq!(
            document.field(dialect.answer_tokens.key).is_some(),
            dialect.answer_tokens.parents.is_empty(),
            "the answer allowance sits at the depth the row declares"
        );
    }
}

#[test]
fn the_assistant_is_named_by_the_family_rather_than_by_the_router() {
    // One family calls the model `model`; writing `assistant` there is rejected
    // for a reason that reads as nothing to do with roles.
    const SPOKEN: [&str; 1] = ["I looked."];
    let turns = [Turn::Said {
        speaker: Speaker::Assistant,
        text: &SPOKEN,
    }];
    let mut seen = Vec::new();
    for api in common::ALL_FAMILIES {
        let plan = common::plan_for(api);
        let dialect = dialect_for(api);
        let document = common::wire_body(api, &common::wire_request(&plan, &turns));
        let list = common::request_root(api, &document)
            .field(dialect.turns_key)
            .and_then(JsonValue::as_array)
            .unwrap();
        let spoken_turn = list.last().unwrap();
        let role = spoken_turn
            .field("role")
            .and_then(JsonValue::as_str)
            .unwrap();
        seen.push(role.to_owned());
    }
    assert_eq!(
        seen,
        [
            "assistant",
            "assistant",
            "assistant",
            "assistant",
            "model",
            "model"
        ]
    );
}

#[test]
fn the_standing_instruction_is_a_turn_in_exactly_one_family() {
    let mut as_turn = 0;
    for api in common::ALL_FAMILIES {
        let plan = common::plan_for(api);
        let turns = asking();
        let dialect = dialect_for(api);
        let document = common::wire_body(api, &common::wire_request(&plan, &turns));
        let list = common::request_root(api, &document)
            .field(dialect.turns_key)
            .and_then(JsonValue::as_array)
            .unwrap();
        if list.len() == 2 {
            as_turn += 1;
            assert_eq!(
                list[0].field("role").and_then(JsonValue::as_str),
                Some("system"),
                "the leading turn is the instruction, not something the user said"
            );
        }
    }
    assert_eq!(as_turn, 1);
}

#[test]
fn a_nested_allowance_and_a_nested_budget_share_one_parent_object() {
    // The case the grouping in the request writer exists for: two scalars two
    // levels deep sharing only their first parent. Writing each at its own path
    // would emit that parent twice, and a duplicate key is refused by the
    // reader — which is what makes this assertion possible at all.
    let plan = common::plan_for(WireApi::GoogleGenerativeLanguage);
    let turns = asking();
    let document = common::wire_body(
        WireApi::GoogleGenerativeLanguage,
        &common::wire_request(&plan, &turns),
    );
    let config = document.field("generationConfig").unwrap();
    // 2,000 reserved for the answer plus the 8,192 the ladder asked to think
    // with. `maxOutputTokens` is the whole output budget on this family and
    // the thinking budget is spent inside it, so the allowance has to cover
    // both — see `answer_allowance` in `wire/request.rs`.
    assert_eq!(
        config.field("maxOutputTokens").and_then(JsonValue::as_i64),
        Some(10_192)
    );
    assert_eq!(
        config
            .field("thinkingConfig")
            .and_then(|nested| nested.field("thinkingBudget"))
            .and_then(JsonValue::as_i64),
        Some(8_192)
    );
}

#[test]
fn a_budget_family_that_needs_switching_on_is_switched_on() {
    let plan = common::plan_for(WireApi::AnthropicMessages);
    let turns = asking();
    let document = common::wire_body(
        WireApi::AnthropicMessages,
        &common::wire_request(&plan, &turns),
    );
    let thinking = document.field("thinking").unwrap();
    assert_eq!(
        thinking.field("type").and_then(JsonValue::as_str),
        Some("enabled"),
        "a budget with nothing turned on reads as thinking left off"
    );
    assert_eq!(
        thinking.field("budget_tokens").and_then(JsonValue::as_i64),
        Some(8_192)
    );
}

#[test]
fn a_disabled_plan_writes_no_thinking_control_at_all() {
    // Not a budget of zero and not an effort of "none": two of the four read
    // those differently from an absent control.
    let plan = ThinkingPlan::Disabled;
    for api in common::ALL_FAMILIES {
        let turns = asking();
        let document = common::wire_body(api, &common::wire_request(&plan, &turns));
        let root = common::request_root(api, &document);
        assert!(root.field("thinking").is_none(), "{api:?}");
        assert!(root.field("reasoning").is_none(), "{api:?}");
        assert!(root.field("reasoning_effort").is_none(), "{api:?}");
        assert!(
            root.field("generationConfig")
                .and_then(|config| config.field("thinkingConfig"))
                .is_none(),
            "{api:?}"
        );
    }
}

#[test]
fn an_effort_family_sends_the_word_the_catalog_supplied() {
    let plan = common::plan_for(WireApi::OpenAiResponses);
    let turns = asking();
    let document = common::wire_body(
        WireApi::OpenAiResponses,
        &common::wire_request(&plan, &turns),
    );
    assert_eq!(
        document
            .field("reasoning")
            .and_then(|nested| nested.field("effort"))
            .and_then(JsonValue::as_str),
        Some("medium")
    );
}

#[test]
fn several_pieces_of_one_turn_survive_on_a_family_with_no_blocks() {
    const PIECES: [&str; 2] = ["first piece", "second piece"];
    let plan = common::plan_for(WireApi::OpenAiCompletions);
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &PIECES,
    }];
    let document = common::wire_body(
        WireApi::OpenAiCompletions,
        &common::wire_request(&plan, &turns),
    );
    let list = document
        .field("messages")
        .and_then(JsonValue::as_array)
        .unwrap();
    let content = list
        .last()
        .unwrap()
        .field("content")
        .and_then(JsonValue::as_str)
        .unwrap();
    assert!(content.contains("first piece"));
    assert!(content.contains("second piece"));
    assert!(
        content.contains("\n\n"),
        "run together they would read as one sentence"
    );
}

#[test]
fn every_family_declares_a_tool_where_its_row_says() {
    let schema = parse(r#"{"type":"object","properties":{}}"#).unwrap();
    let tools = [ToolDeclaration {
        name: "read_page",
        description: "Read the page.",
        parameters: &schema,
    }];
    for api in common::ALL_FAMILIES {
        let plan = common::plan_for(api);
        let turns = asking();
        let request = WireRequest {
            model_id: "a-model",
            tool_calling: true,
            system: None,
            system_preamble: None,
            credential_method: None,
            compat: None,
            turns: &turns,
            tools: &tools,
            tool_cache_retention: model_router::request::CacheRetention::None,
            conversation_key: None,
            thinking: &plan,
            answer_tokens: 2_000,
            stream: false,
        };
        let document = common::wire_body(api, &request);
        let list = common::request_root(api, &document)
            .field("tools")
            .and_then(JsonValue::as_array)
            .unwrap();
        assert_eq!(list.len(), 1, "{api:?}");
        // Whatever the wrapping, the name appears under `tools` exactly once,
        // and the router never renames a tool.
        assert_eq!(
            format!("{list:?}").matches("read_page").count(),
            1,
            "{api:?}"
        );
    }
}

#[test]
fn an_empty_conversation_is_refused_before_a_body_exists() {
    let plan = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "a-model",
        tool_calling: true,
        system: Some("Be brief."),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &[],
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let mut out = String::new();
    assert_eq!(
        write_request(WireApi::AnthropicMessages, &request, &mut out),
        Err(WireRefusal::EmptyConversation)
    );
    assert!(out.is_empty());
}

#[test]
fn one_family_carries_its_model_in_the_url_rather_than_the_body() {
    let absent = common::ALL_FAMILIES
        .into_iter()
        .filter(|api| dialect_for(*api).model.is_none())
        .count();
    assert_eq!(absent, 1);
    let plan = common::plan_for(WireApi::GoogleGenerativeLanguage);
    let turns = asking();
    let document = common::wire_body(
        WireApi::GoogleGenerativeLanguage,
        &common::wire_request(&plan, &turns),
    );
    assert!(
        document.field("model").is_none(),
        "writing a field the provider ignores would read as configuration"
    );
}

#[test]
fn a_body_the_writer_produced_is_appended_rather_than_returned() {
    // The signature is the point: the buffer belongs to the caller, so no body
    // — and therefore no page-derived material inside one — is ever held in a
    // value this crate owns.
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let mut out = String::from("prefix:");
    write_request(
        WireApi::AnthropicMessages,
        &common::wire_request(&plan, &turns),
        &mut out,
    )
    .unwrap();
    assert!(out.starts_with("prefix:{"));
}

#[test]
fn a_tool_schema_that_nests_too_deeply_is_refused() {
    // The writer walks a schema recursively, so its depth has to be somebody's
    // decision rather than the input's.
    let mut text = String::new();
    let depth = model_router::json::MAX_DEPTH + 2;
    for _ in 0..depth {
        text.push_str("{\"a\":");
    }
    text.push('1');
    for _ in 0..depth {
        text.push('}');
    }
    // Too deep for the reader as well, so build the value the reader would
    // have produced had it accepted it.
    assert!(parse(&text).is_err(), "the reader draws the same line");
    let mut schema = JsonValue::Integer(1);
    for _ in 0..depth {
        let mut map = std::collections::BTreeMap::new();
        map.insert("a".to_owned(), schema);
        schema = JsonValue::Object(map);
    }
    let tools = [ToolDeclaration {
        name: "deep",
        description: "Too deep.",
        parameters: &schema,
    }];
    let plan = ThinkingPlan::Disabled;
    let turns = asking();
    let request = WireRequest {
        model_id: "a-model",
        tool_calling: true,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &tools,
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let mut out = String::new();
    assert_eq!(
        write_request(WireApi::AnthropicMessages, &request, &mut out),
        Err(WireRefusal::ToolSchemaTooDeep { tool_index: 0 })
    );
    assert!(out.is_empty());
}

#[test]
fn a_budget_family_never_asks_to_think_past_its_own_allowance() {
    // Anthropic refuses a request whose `max_tokens` does not exceed
    // `thinking.budget_tokens`, and Google counts thinking tokens inside
    // `maxOutputTokens`. The pair used to be written as two independent
    // numbers, so every request that asked to think at all carried an
    // allowance smaller than the budget beside it and was rejected before the
    // provider read anything else. The relation, not the two literals, is what
    // this asserts — a later ladder change must not be able to reintroduce it.
    for (api, allowance, budget) in [
        (
            WireApi::AnthropicMessages,
            &["max_tokens"][..],
            &["thinking", "budget_tokens"][..],
        ),
        (
            WireApi::GoogleGenerativeLanguage,
            &["generationConfig", "maxOutputTokens"][..],
            &["generationConfig", "thinkingConfig", "thinkingBudget"][..],
        ),
    ] {
        let plan = common::plan_for(api);
        let turns = asking();
        let document = common::wire_body(api, &common::wire_request(&plan, &turns));
        let read = |path: &[&str]| {
            path.iter()
                .try_fold(&document, |node, key| node.field(key))
                .and_then(JsonValue::as_i64)
        };
        let (Some(allowed), Some(thinking)) = (read(allowance), read(budget)) else {
            panic!("{api:?} wrote neither an allowance nor a budget");
        };
        assert!(
            allowed > thinking,
            "{api:?} asked to think with {thinking} inside an allowance of {allowed}"
        );
    }
}
