// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The running composer opts in with compiled declarations only.

use model_router::catalog::{CatalogLayer, Endpoint, WireApi};
use model_router::ids::ProviderId;
use model_router::json::{parse, JsonValue};
use task_engine::{EffectiveToolSet, Milestone};

use super::{candidate, transcript, write_body, TEST_CONVERSATION};

#[test]
fn compiled_tool_cache_prefix_stays_identical_when_person_and_page_content_change() {
    let selected = candidate(true);
    let tools = EffectiveToolSet::for_task(Milestone::M8, &[]);
    let conversation = transcript(Vec::new());
    let bodies: Vec<_> = [
        (
            "first page",
            "first personal answer",
            "first standing instruction",
        ),
        (
            "second page",
            "second personal answer",
            "second standing instruction",
        ),
    ]
    .into_iter()
    .map(|(page, answer, system)| {
        let body = super::super::body::write_body_with_system(
            &selected,
            &conversation,
            &tools,
            Some(page),
            Some(answer),
            None,
            system,
            TEST_CONVERSATION,
        )
        .expect("production direct body composes");
        assert_eq!(body.matches("\"cache_control\"").count(), 1);
        parse(&body).expect("composed JSON")
    })
    .collect();
    let first = bodies.first().expect("first body");
    let second = bodies.last().expect("second body");
    assert_eq!(first.field("tools"), second.field("tools"));
    assert_ne!(first.field("messages"), second.field("messages"));
    assert_ne!(first.field("system"), second.field("system"));
    let declarations = first
        .field("tools")
        .and_then(JsonValue::as_array)
        .expect("compiled tools");
    assert!(!declarations.is_empty());
    for declaration in declarations.iter().take(declarations.len() - 1) {
        assert!(declaration.field("cache_control").is_none());
    }
    let final_tool = declarations.last().expect("last compiled tool");
    assert_eq!(
        final_tool.field("cache_control"),
        Some(&parse(r#"{"type":"ephemeral"}"#).expect("marker"))
    );
}

#[test]
fn compatible_vendors_custom_endpoints_and_requests_without_tools_do_not_opt_in() {
    let selected = candidate(true);
    let mut other_vendor = selected.clone();
    other_vendor.model.provider_id = ProviderId::new("compatible-vendor").expect("provider id");
    let mut other_host = selected.clone();
    other_host.endpoint = Endpoint::new("https://compatible.example").expect("endpoint");
    let mut own_endpoint = selected.clone();
    own_endpoint.endpoint_layer = CatalogLayer::UserOverride;
    let mut other_wire = selected;
    other_wire.wire_api = WireApi::OpenAiResponses;
    let tools = EffectiveToolSet::for_task(Milestone::M8, &[]);
    for candidate in [
        other_vendor,
        other_host,
        own_endpoint,
        other_wire,
        candidate(false),
    ] {
        let body = write_body(
            &candidate,
            &transcript(Vec::new()),
            &tools,
            None,
            None,
            TEST_CONVERSATION,
        )
        .expect("body composes");
        assert!(!body.contains("\"cache_control\""));
    }
}
