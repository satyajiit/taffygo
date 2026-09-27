// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The residency overlay, asserted against a written body.
//!
//! Apart from the ladder tests because the subject is different: these pin
//! what a resident turn adds to the conversation and what a restart erases,
//! not what the byte ladder keeps.

use bip_types::identity::TabId;
use model_router::catalog::WireApi;
use std::collections::BTreeMap;

use model_router::json::{self, JsonValue};
use task_engine::{
    ActionState, ArgumentValue, HandleTable, LoopOutcome, ModelCallId, ModelReply, ModelStopReason,
    ModelToolCall, RenderShape, SuppliedArgument, TaskLibraryCitation, TaskLibrarySearchEntry,
    TaskLibrarySearchTranscriptOutcome, TurnPage, TurnResidency, TurnUsage,
};

use super::{anthropic_pairs, body, exchange};
use crate::context::transcript::{RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};

fn residency_naming(handle: u32) -> TurnResidency {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![ModelToolCall::new(
            "browser.dom.click",
            vec![SuppliedArgument::new("node", ArgumentValue::Handle(handle))],
        )],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    TurnResidency::read(ModelCallId::new("model-task-1"), page, reply).expect("one call")
}

#[test]
fn overlaying_a_resident_turn_fills_only_that_turn_and_a_restart_writes_empty() {
    let mut transcript = TaskTranscript::new(
        "click the download".to_owned(),
        vec![
            exchange(0, &["browser.dom.read"]),
            exchange(1, &["browser.dom.click"]),
        ],
        TranscriptBudget::default(),
    );
    let older = transcript
        .exchanges()
        .first()
        .expect("the earlier turn")
        .calls()
        .first()
        .expect("its call")
        .arguments()
        .clone();
    assert_eq!(older, JsonValue::Object(BTreeMap::new()));

    transcript.overlay_resident_calls(&residency_naming(7));

    let newer = transcript
        .exchanges()
        .last()
        .expect("the later turn")
        .calls()
        .first()
        .expect("its call")
        .arguments();
    let expected = JsonValue::Object(BTreeMap::from([("node".to_owned(), JsonValue::Integer(7))]));
    assert_eq!(newer, &expected);

    let still_older = transcript
        .exchanges()
        .first()
        .expect("the earlier turn")
        .calls()
        .first()
        .expect("its call")
        .arguments();
    assert_eq!(still_older, &JsonValue::Object(BTreeMap::new()));
}

fn length_stop_residency(ordinal: u64, calls: Vec<ModelToolCall>) -> TurnResidency {
    let reply = ModelReply {
        stop: ModelStopReason::Length,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: calls,
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    TurnResidency::read(
        ModelCallId::new(format!("model-task_1-{ordinal}")),
        page,
        reply,
    )
    .expect("calls under the turn ceiling")
}

#[test]
fn a_length_stop_overlay_pairs_truncated_calls_with_a_reissue_result() {
    let mut transcript = TaskTranscript::new(
        "find the cheapest one".to_owned(),
        Vec::new(),
        TranscriptBudget::default(),
    );
    transcript.overlay_resident_calls(&length_stop_residency(
        4,
        vec![
            ModelToolCall::new(
                "browser.dom.read",
                vec![SuppliedArgument::new("node", ArgumentValue::Handle(3))],
            ),
            ModelToolCall::new(
                "browser.dom.click",
                vec![SuppliedArgument::new("node", ArgumentValue::Handle(7))],
            ),
        ],
    ));

    let document = json::parse(&body(&transcript, WireApi::AnthropicMessages)).expect("json");
    // One Called turn then one Returned: the family groups every tool_use
    // in the assistant message and every tool_result in the next user
    // message. Pairing is by identity, not by interleaving.
    assert_eq!(
        anthropic_pairs(&document),
        vec![
            (
                "assistant".to_owned(),
                "tool_use".to_owned(),
                "turn-4-call-0".to_owned()
            ),
            (
                "assistant".to_owned(),
                "tool_use".to_owned(),
                "turn-4-call-1".to_owned()
            ),
            (
                "user".to_owned(),
                "tool_result".to_owned(),
                "turn-4-call-0".to_owned()
            ),
            (
                "user".to_owned(),
                "tool_result".to_owned(),
                "turn-4-call-1".to_owned()
            ),
        ]
    );
    let written = body(&transcript, WireApi::AnthropicMessages);
    assert!(
        written.contains("Re-issue this tool call with complete arguments."),
        "the model is told to re-issue: {written}"
    );
    assert!(
        written.contains("\"is_error\":true") || written.contains("\"is_error\": true"),
        "the result is an error: {written}"
    );
}

#[test]
fn a_length_stop_with_no_calls_does_not_append_an_exchange() {
    let mut transcript = TaskTranscript::new(
        "find the cheapest one".to_owned(),
        Vec::new(),
        TranscriptBudget::default(),
    );
    transcript.overlay_resident_calls(&length_stop_residency(1, Vec::new()));
    assert!(transcript.exchanges().is_empty());
}

#[test]
fn settled_loop_calls_are_returned_to_the_next_model_request() {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![
            ModelToolCall::new(
                "tool.search",
                vec![SuppliedArgument::new(
                    "query",
                    ArgumentValue::Text("tables".to_owned()),
                )],
            ),
            ModelToolCall::new(
                "tool.activate",
                vec![SuppliedArgument::new(
                    "name",
                    ArgumentValue::Text("browser.dom.query".to_owned()),
                )],
            ),
        ],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    let mut residency = TurnResidency::read(ModelCallId::new("model-task_1-9"), page, reply)
        .expect("bounded calls");
    assert!(residency.settle_loop(0, LoopOutcome::Searched { hits: 4 }));
    assert!(residency.settle_loop(1, LoopOutcome::Activated { name_known: false }));

    let mut transcript = TaskTranscript::new(
        "inspect the tables".to_owned(),
        Vec::new(),
        TranscriptBudget::default(),
    );
    transcript.overlay_resident_calls(&residency);

    let written = body(&transcript, WireApi::AnthropicMessages);
    assert!(
        written.contains("found several deferred tools"),
        "{written}"
    );
    assert!(
        written.contains("no deferred tool has that name"),
        "{written}"
    );
    assert!(written.contains("\"query\":\"tables\"") || written.contains("\"query\": \"tables\""));
    assert!(written.contains("\"is_error\":true") || written.contains("\"is_error\": true"));
}

#[test]
fn a_loop_result_is_inserted_beside_durable_action_calls_in_sequence_order() {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![
            ModelToolCall::new(
                "tool.search",
                vec![SuppliedArgument::new(
                    "query",
                    ArgumentValue::Text("download".to_owned()),
                )],
            ),
            ModelToolCall::new("browser.dom.read", Vec::new()),
        ],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    let mut residency = TurnResidency::read(ModelCallId::new("model-task_1-3"), page, reply)
        .expect("bounded calls");
    assert!(residency.settle_loop(0, LoopOutcome::Searched { hits: 1 }));
    let durable_action = RecordedCall::new(
        "turn-3-call-1".to_owned(),
        "browser.dom.read".to_owned(),
        JsonValue::Object(BTreeMap::new()),
        ActionState::Verified,
    );
    let mut transcript = TaskTranscript::new(
        "download it".to_owned(),
        vec![TurnExchange::new(3, vec![durable_action])
            .unwrap_or_else(|| unreachable!("one bounded action"))],
        TranscriptBudget::default(),
    );

    transcript.overlay_resident_calls(&residency);

    let calls = transcript.exchanges().last().expect("turn").calls();
    assert_eq!(calls.len(), 2);
    assert_eq!(calls[0].tool(), "tool.search");
    assert_eq!(calls[1].tool(), "browser.dom.read");
}

#[test]
fn a_library_result_is_visible_only_while_its_exact_reply_is_resident() {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![ModelToolCall::new(
            "library.search",
            vec![SuppliedArgument::new(
                "query",
                ArgumentValue::Text("battery".to_owned()),
            )],
        )],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    let mut residency = TurnResidency::read(ModelCallId::new("model-task_1-4"), page, reply)
        .expect("bounded reply");
    let entry = TaskLibrarySearchEntry::new(
        "entry-1".to_owned(),
        "Laptop research".to_owned(),
        "battery life".to_owned(),
        "twelve hours".to_owned(),
        vec![
            TaskLibraryCitation::new("Independent test".to_owned(), "example.test".to_owned())
                .expect("valid citation"),
        ],
        250,
        false,
    )
    .expect("valid saved fact");
    assert!(residency.record_library_search_outcome(
        0,
        TaskLibrarySearchTranscriptOutcome::bounded(vec![entry])
    ));
    let exchange = || {
        TurnExchange::new(
            4,
            vec![RecordedCall::new(
                "turn-4-call-0".to_owned(),
                "library.search".to_owned(),
                JsonValue::Object(BTreeMap::new()),
                ActionState::Verified,
            )],
        )
        .expect("one bounded action")
    };
    let mut resident = TaskTranscript::new(
        "compare battery life".to_owned(),
        vec![exchange()],
        TranscriptBudget::default(),
    );
    resident.overlay_resident_calls(&residency);
    let resident_body = body(&resident, WireApi::AnthropicMessages);
    assert!(resident_body.contains("battery life: twelve hours"));
    assert!(resident_body.contains("Independent test (example.test)"));
    assert!(resident_body.contains("Last checked 250 ms ago"));

    let restored = TaskTranscript::new(
        "compare battery life".to_owned(),
        vec![exchange()],
        TranscriptBudget::default(),
    );
    let restored_body = body(&restored, WireApi::AnthropicMessages);
    assert!(!restored_body.contains("twelve hours"));
    assert!(!restored_body.contains("example.test"));
}
