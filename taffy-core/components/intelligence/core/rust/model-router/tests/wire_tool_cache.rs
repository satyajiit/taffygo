// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Explicit tool-prefix retention, independently of later prompt content.

#![allow(clippy::expect_used, clippy::indexing_slicing, clippy::panic)]

mod common;

use model_router::catalog::WireApi;
use model_router::json::{parse, JsonValue};
use model_router::request::CacheRetention;
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{Speaker, ToolDeclaration, Turn, WireRequest};

fn document(api: WireApi, retention: CacheRetention, with_tools: bool) -> JsonValue {
    let schema =
        parse(r#"{"type":"object","properties":{},"required":[]}"#).expect("a schema fixture");
    let tools = [
        ToolDeclaration {
            name: "page.read",
            description: "Read the page",
            parameters: &schema,
        },
        ToolDeclaration {
            name: "user.ask",
            description: "Ask the person",
            parameters: &schema,
        },
    ];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &["a private answer"],
    }];
    let plan = ThinkingPlan::Disabled;
    common::wire_body(
        api,
        &WireRequest {
            tools: if with_tools { &tools } else { &[] },
            tool_cache_retention: retention,
            ..common::wire_request(&plan, &turns)
        },
    )
}

#[test]
fn only_the_final_tool_receives_the_requested_supported_retention() {
    let api = WireApi::AnthropicMessages;
    let baseline = document(api, CacheRetention::None, true);
    let original_tools = baseline
        .field("tools")
        .and_then(JsonValue::as_array)
        .expect("tools");
    for (retention, marker) in [
        (CacheRetention::Short, r#"{"type":"ephemeral"}"#),
        (CacheRetention::Long, r#"{"type":"ephemeral","ttl":"1h"}"#),
    ] {
        let cached = document(api, retention, true);
        let tools = cached
            .field("tools")
            .and_then(JsonValue::as_array)
            .expect("tools");
        assert_eq!(tools.len(), 2);
        assert_eq!(tools[0], original_tools[0]);
        let mut final_tool = tools[1].as_object().expect("a declaration").clone();
        assert_eq!(
            final_tool.remove("cache_control"),
            Some(parse(marker).expect("marker"))
        );
        assert_eq!(JsonValue::Object(final_tool), original_tools[1]);
        assert_eq!(cached.field("system"), baseline.field("system"));
        assert_eq!(cached.field("messages"), baseline.field("messages"));
        assert!(cached.field("cache_control").is_none());
    }
}

#[test]
fn unsupported_families_and_empty_tool_sets_omit_explicit_cache_fields() {
    for api in common::ALL_FAMILIES {
        for retention in [CacheRetention::Short, CacheRetention::Long] {
            assert_eq!(
                document(api, retention, false),
                document(api, CacheRetention::None, false)
            );
            if api != WireApi::AnthropicMessages {
                assert_eq!(
                    document(api, retention, true),
                    document(api, CacheRetention::None, true)
                );
            }
        }
    }
}
