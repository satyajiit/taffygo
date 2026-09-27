// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The recent-turn memory, asserted against a written body.
//!
//! The phone's failure these pin: every earlier step reached the model as a
//! tool name with `{}` and no words, so it forgot the page it had reached and
//! searched again. A turn this generation read comes back with its own
//! arguments and what the model said; a restart remembers nothing.

use std::collections::BTreeMap;

use bip_types::identity::TabId;
use model_router::catalog::WireApi;
use model_router::json::JsonValue;
use task_engine::{
    ArgumentValue, HandleTable, ModelCallId, ModelReply, ModelStopReason, ModelToolCall,
    NotAttempted, RenderShape, SuppliedArgument, TurnPage, TurnResidency, TurnUsage,
};

use super::{body, exchange};
use crate::context::transcript::{
    RecentTurns, TaskTranscript, TranscriptBudget, TurnExchange, MAX_RECENT_SAID_BYTES,
    MAX_RECENT_TURNS,
};

fn reading(ordinal: u64, tool: &str, argument: SuppliedArgument) -> TurnResidency {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 1,
        tool_calls: vec![ModelToolCall::new(tool, vec![argument])],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    TurnResidency::read(
        ModelCallId::new(format!("model-task-{ordinal}")),
        page,
        reply,
    )
    .expect("one call")
}

fn conversation() -> TaskTranscript {
    TaskTranscript::new(
        "download my document".to_owned(),
        vec![
            exchange(0, &["browser.search"]),
            exchange(1, &["browser.link.open"]),
            exchange(2, &["browser.dom.read"]),
        ],
        TranscriptBudget::default(),
    )
}

fn arguments_of(transcript: &TaskTranscript, ordinal: usize) -> JsonValue {
    transcript
        .exchanges()
        .get(ordinal)
        .expect("the turn")
        .calls()
        .first()
        .expect("its call")
        .arguments()
        .clone()
}

#[test]
fn an_earlier_turn_comes_back_with_its_own_arguments_and_words() {
    let mut recent = RecentTurns::default();
    recent.remember(
        &reading(
            0,
            "browser.search",
            SuppliedArgument::new("query", ArgumentValue::Text("official portal".to_owned())),
        ),
        "I'll search for the official portal.",
        Vec::new(),
    );
    recent.remember(
        &reading(
            1,
            "browser.link.open",
            SuppliedArgument::new("node", ArgumentValue::Handle(2495)),
        ),
        "  We're on the official download page.  ",
        Vec::new(),
    );
    let mut transcript = conversation();
    transcript.overlay_recent_turns(&recent);

    assert_eq!(
        arguments_of(&transcript, 0),
        JsonValue::Object(BTreeMap::from([(
            "query".to_owned(),
            JsonValue::Text("official portal".to_owned())
        )]))
    );
    assert_eq!(
        arguments_of(&transcript, 1),
        JsonValue::Object(BTreeMap::from([(
            "node".to_owned(),
            JsonValue::Integer(2495)
        )]))
    );
    // A turn this generation never read keeps the journal's `{}`.
    assert_eq!(
        arguments_of(&transcript, 2),
        JsonValue::Object(BTreeMap::new())
    );

    let written = body(&transcript, WireApi::OpenAiCompletions);
    assert!(written.contains("I'll search for the official portal."));
    // Trimmed, so a reply that opened with a blank line is not replayed as one.
    assert!(written.contains("\"We're on the official download page.\""));
    assert!(written.contains("official portal"));
}

#[test]
fn a_restart_remembers_nothing_and_writes_what_the_journal_kept() {
    let mut transcript = conversation();
    transcript.overlay_recent_turns(&RecentTurns::default());
    assert_eq!(transcript, conversation());
    for ordinal in 0..3 {
        assert_eq!(
            arguments_of(&transcript, ordinal),
            JsonValue::Object(BTreeMap::new())
        );
    }
}

#[test]
fn the_memory_keeps_the_newest_turns_and_bounded_words() {
    let mut recent = RecentTurns::default();
    let long = "a".repeat(MAX_RECENT_SAID_BYTES.saturating_mul(2));
    for ordinal in 0..=u64::try_from(MAX_RECENT_TURNS).expect("small") {
        recent.remember(
            &reading(
                ordinal,
                "browser.dom.click",
                SuppliedArgument::new("node", ArgumentValue::Handle(1)),
            ),
            &long,
            Vec::new(),
        );
    }
    assert_eq!(recent.len(), MAX_RECENT_TURNS);

    let mut transcript = TaskTranscript::new(
        "goal".to_owned(),
        vec![
            exchange(0, &["browser.dom.click"]),
            exchange(1, &["browser.dom.click"]),
        ],
        TranscriptBudget::default(),
    );
    transcript.overlay_recent_turns(&recent);
    // Turn 0 was the oldest and went; turn 1 is still remembered.
    assert_eq!(
        arguments_of(&transcript, 0),
        JsonValue::Object(BTreeMap::new())
    );
    assert_ne!(
        arguments_of(&transcript, 1),
        JsonValue::Object(BTreeMap::new())
    );
    let written = body(&transcript, WireApi::OpenAiCompletions);
    assert!(written.contains(&"a".repeat(MAX_RECENT_SAID_BYTES)));
    assert!(!written.contains(&"a".repeat(MAX_RECENT_SAID_BYTES + 1)));
}

#[test]
fn a_call_refused_on_sight_is_told_to_the_model_in_its_own_place() {
    // Turns 0 and 2 became actions; turn 1's only call named a number the
    // page no longer held, so the journal has nothing for it.
    let mut transcript = TaskTranscript::new(
        "goal".to_owned(),
        vec![
            exchange(0, &["browser.dom.read"]),
            exchange(2, &["browser.dom.read"]),
        ],
        TranscriptBudget::default(),
    );
    let mut recent = RecentTurns::default();
    recent.remember(
        &reading(
            1,
            "browser.link.open",
            SuppliedArgument::new("node", ArgumentValue::Handle(34)),
        ),
        "Opening the download service.",
        vec![(0, NotAttempted::HandleUnknown)],
    );
    transcript.overlay_recent_turns(&recent);

    let ordinals: Vec<u64> = transcript
        .exchanges()
        .iter()
        .map(TurnExchange::ordinal)
        .collect();
    assert_eq!(ordinals, [0, 1, 2]);
    let written = body(&transcript, WireApi::OpenAiCompletions);
    assert!(written.contains("that number is no longer available"));
    assert!(written.contains("Opening the download service."));
    // Once, however often the transcript is overlaid.
    transcript.overlay_recent_turns(&recent);
    assert_eq!(transcript.exchanges().len(), 3);
}

#[test]
fn a_refusal_older_than_what_the_ladder_kept_is_not_brought_back() {
    let mut transcript = TaskTranscript::with_floor(
        "goal".to_owned(),
        vec![
            exchange(0, &["browser.dom.read"]),
            exchange(5, &["browser.dom.read"]),
        ],
        TranscriptBudget::default(),
        Some(0),
    );
    assert_eq!(transcript.elided(), 1);
    let mut recent = RecentTurns::default();
    recent.remember(
        &reading(
            3,
            "browser.dom.click",
            SuppliedArgument::new("node", ArgumentValue::Handle(9)),
        ),
        "",
        vec![(0, NotAttempted::HandleUnknown)],
    );
    recent.remember(
        &reading(
            6,
            "browser.dom.click",
            SuppliedArgument::new("node", ArgumentValue::Handle(9)),
        ),
        "",
        vec![(0, NotAttempted::HandleUnknown)],
    );
    transcript.overlay_recent_turns(&recent);
    let ordinals: Vec<u64> = transcript
        .exchanges()
        .iter()
        .map(TurnExchange::ordinal)
        .collect();
    assert_eq!(ordinals, [5, 6]);
}
