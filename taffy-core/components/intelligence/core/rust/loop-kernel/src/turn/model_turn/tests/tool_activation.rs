// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deferred discovery and activation carried through consecutive model bodies.

use bip_types::identity::TabId;
use task_engine::{
    loop_tool_result, resolve, ArgumentValue, EffectiveToolSet, HandleTable, LoopOutcome,
    Milestone, ModelCallId, ModelReply, ModelStopReason, ModelToolCall, RenderShape,
    SuppliedArgument, TurnPage, TurnResidency, TurnUsage, ACTIVATE_TOOL, SEARCH_TOOLS,
};

use super::{candidate, transcript, write_body, TEST_CONVERSATION};

fn loop_residency(call_id: &str, tool: &str, argument: &str, value: &str) -> TurnResidency {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![ModelToolCall::new(
            tool,
            vec![SuppliedArgument::new(
                argument,
                ArgumentValue::Text(value.to_owned()),
            )],
        )],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    TurnResidency::read(ModelCallId::new(call_id), page, reply)
        .unwrap_or_else(|| unreachable!("one bounded loop call is resident"))
}

#[test]
fn search_then_activation_exposes_exact_callable_names_on_the_next_request() {
    let allowlist = vec![
        SEARCH_TOOLS.to_owned(),
        ACTIVATE_TOOL.to_owned(),
        "page.images".to_owned(),
    ];
    let reviewed = EffectiveToolSet::for_task(Milestone::M6, &allowlist);
    let mut search = loop_residency(
        "model-task_1-1",
        SEARCH_TOOLS,
        "query",
        "image understanding",
    );
    let search_entry = resolve(SEARCH_TOOLS, Milestone::M6)
        .entry()
        .unwrap_or_else(|| unreachable!("tool.search is registered"));
    let (outcome, result) = loop_tool_result(
        search_entry,
        search
            .call(0)
            .unwrap_or_else(|| unreachable!("the search call is resident")),
        &reviewed,
    );
    assert_eq!(outcome, LoopOutcome::Searched { hits: 2 });
    assert_eq!(
        result,
        vec!["matching deferred tools (2 of 2): page.images.describe, page.images.read_text"]
    );
    assert!(search.settle_loop_with_result(0, outcome, result));

    let mut conversation = transcript(Vec::new());
    conversation.overlay_resident_calls(&search);
    let after_search = write_body(
        &candidate(true),
        &conversation,
        &reviewed,
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("the search result composes into the follow-up request");
    assert!(after_search.contains("page.images.describe"));
    assert!(after_search.contains("page.images.read_text"));

    let mut activation = loop_residency(
        "model-task_1-2",
        ACTIVATE_TOOL,
        "name",
        "page.images.describe",
    );
    let activate_entry = resolve(ACTIVATE_TOOL, Milestone::M6)
        .entry()
        .unwrap_or_else(|| unreachable!("tool.activate is registered"));
    let (outcome, result) = loop_tool_result(
        activate_entry,
        activation
            .call(0)
            .unwrap_or_else(|| unreachable!("the activation call is resident")),
        &reviewed,
    );
    assert_eq!(outcome, LoopOutcome::Activated { name_known: true });
    assert_eq!(
        result,
        vec!["activated deferred tool: page.images.describe"]
    );
    assert!(activation.settle_loop_with_result(0, outcome, result));
    assert_eq!(activation.activated_names(), ["page.images.describe"]);
    conversation.overlay_resident_calls(&activation);

    let activated = reviewed.with_activated(activation.activated_names());
    let declarations: Vec<&str> = activated
        .definitions()
        .iter()
        .map(|definition| definition.name)
        .collect();
    assert!(declarations.contains(&"page.images.describe"));
    assert!(declarations.contains(&"page.images.read_text"));
    assert!(!declarations.contains(&"page.images"));
    assert!(!declarations.contains(&"page.images.caption"));
    for name in ["page.images.describe", "page.images.read_text"] {
        assert!(resolve(name, Milestone::M6).is_available(), "{name}");
    }
    assert_eq!(
        resolve("page.images.caption", Milestone::M6),
        task_engine::ToolLookup::Unknown
    );

    let next_request = write_body(
        &candidate(true),
        &conversation,
        &activated,
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("the activated vocabulary composes into the next request");
    assert!(next_request.contains(r#""name":"page.images.describe""#));
    assert!(next_request.contains(r#""name":"page.images.read_text""#));
    assert!(!next_request.contains(r#""name":"page.images""#));
    assert!(!next_request.contains("page.images.caption"));
}
