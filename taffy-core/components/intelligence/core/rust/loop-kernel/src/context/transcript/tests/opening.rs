// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The preface: compiled-in pieces that follow the goal in the opening turn.

use model_router::wire::{Speaker, Turn};

use crate::context::transcript::{TaskTranscript, TranscriptBudget};

fn transcript() -> TaskTranscript {
    TaskTranscript::new(
        "the goal".to_owned(),
        Vec::new(),
        TranscriptBudget::default(),
    )
}

#[test]
fn the_preface_follows_the_goal_and_precedes_the_page() {
    let transcript = transcript().with_preface(vec!["first piece".to_owned(), "second".to_owned()]);
    assert_eq!(transcript.preface(), ["first piece", "second"]);
    let views = transcript.views(Some("the page"), Some("the answer"));
    let turns = views.turns();
    let Some(Turn::Said { speaker, text }) = turns.first() else {
        panic!("the opening turn is the person's");
    };
    assert_eq!(*speaker, Speaker::User);
    // Goal, then the preface, then what the person answered, then the page:
    // the pieces a person's turn is assembled from, in the order a reader
    // would want them, and never a second turn in the same role.
    assert_eq!(
        *text,
        [
            "the goal",
            "first piece",
            "second",
            "the answer",
            "the page"
        ]
    );
    assert_eq!(turns.len(), 1);
}

#[test]
fn a_transcript_without_a_preface_opens_exactly_as_before() {
    let transcript = transcript();
    assert!(transcript.preface().is_empty());
    let views = transcript.views(None, None);
    let turns = views.turns();
    let Some(Turn::Said { text, .. }) = turns.first() else {
        panic!("the opening turn is the person's");
    };
    assert_eq!(*text, ["the goal"]);
}

#[test]
fn the_preface_is_counted_in_the_material_a_request_carries() {
    let bare = transcript();
    let prefaced = transcript().with_preface(vec!["x".repeat(40)]);
    assert_eq!(prefaced.text_bytes(), bare.text_bytes() + 40);
}
