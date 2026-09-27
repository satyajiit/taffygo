// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A line that would say nothing is left out, and counted apart from a line
//! the budget had no room for.
//!
//! The phone's failure these pin: 263 of the 381 lines a snapshot of the
//! myAadhaar home page printed were `content (authored by first-party
//! document) — can scroll`, and the page's own buttons were past the budget
//! (decision 0189).

use bip_types::action::ActionType;
use bip_types::snapshot::{NodeState, SemanticRole, Sensitivity};

use super::{node, rendered};
use crate::context::arena::{DestinationClass, PageArena};

fn filler(node_id: &str) -> crate::context::arena::ArenaNode {
    let mut filler = node(node_id, SemanticRole::UnknownContent, None, &[]);
    filler.actions = vec![ActionType::ScrollIntoView];
    filler
}

#[test]
fn a_node_with_nothing_to_say_gets_no_line_and_no_number() {
    let mut arena = PageArena::new();
    assert!(arena.push(filler("n-1")));
    let mut hidden = filler("n-2");
    hidden.states = vec![NodeState::NotVisible];
    assert!(arena.push(hidden));
    let mut image = node("n-3", SemanticRole::Image, None, &[]);
    image.actions = vec![ActionType::ScrollIntoView];
    assert!(arena.push(image));
    let mut button = node("n-4", SemanticRole::Button, Some("Download Aadhaar"), &[]);
    button.actions = vec![ActionType::Activate];
    assert!(arena.push(button));

    let (text, offered, omitted) = rendered(&arena, 4096);
    assert_eq!((offered, omitted), (1, 0));
    // The one line there is to read takes the first number.
    assert_eq!(
        text,
        "[0] button \"Download Aadhaar\" (authored by first-party document) — can activate\n\
         left out: 3 nodes with no words, nowhere to go and nothing to press\n\
         values: no line on this page takes one — a person cannot type here\n"
    );
}

#[test]
fn anything_a_node_says_keeps_its_line() {
    let mut named = filler("named");
    named.name = Some("Check status".to_owned());
    let mut withheld = filler("withheld");
    withheld.name_withheld = true;
    let mut private = filler("private");
    private.sensitivity = Sensitivity::Identity;
    let mut pressable = filler("pressable");
    pressable.actions = vec![ActionType::Focus, ActionType::ScrollIntoView];
    let mut leads = filler("leads");
    leads.destination = DestinationClass::from_bits(1);
    let mut stated = filler("stated");
    stated.states = vec![NodeState::Focused];
    let mut declared = filler("declared");
    declared.declared_text_runs = 1;

    let mut arena = PageArena::new();
    for kept in [named, withheld, private, pressable, leads, stated, declared] {
        assert!(arena.push(kept));
    }
    let (text, offered, omitted) = rendered(&arena, 8192);
    assert_eq!((offered, omitted), (7, 0));
    assert!(!text.contains("left out:"));
}

#[test]
fn a_form_a_control_names_keeps_its_line_however_bare() {
    let mut form = filler("form-1");
    form.actions = Vec::new();
    let mut field = node("n-1", SemanticRole::TextField, Some("Enter Captcha"), &[]);
    field.actions = vec![ActionType::SetText];
    field.container = Some("form-1".to_owned());

    let mut arena = PageArena::new();
    assert!(arena.push(form));
    assert!(arena.push(field));
    let (text, offered, _) = rendered(&arena, 4096);
    assert_eq!(offered, 2);
    assert!(text.contains("in form [0]"), "{text}");
}

#[test]
fn a_quiet_node_is_not_counted_as_one_the_budget_had_no_room_for() {
    let mut arena = PageArena::new();
    for index in 0..40 {
        assert!(arena.push(filler(&format!("quiet-{index}"))));
    }
    for index in 0..40 {
        let mut button = node(
            &format!("b-{index}"),
            SemanticRole::Button,
            Some("Next"),
            &[],
        );
        button.actions = vec![ActionType::Activate];
        assert!(arena.push(button));
    }
    // Room for some of the buttons: forty lines of about seventy bytes are
    // past four times this budget, so the page is cut rather than finished.
    let (text, offered, omitted) = rendered(&arena, 512);
    assert!(offered > 0 && offered < 40, "{offered}");
    assert_eq!(offered + omitted, 40);
    assert!(text.contains(&format!("not shown: {omitted} of 40 nodes")));
    assert!(text.contains("left out: 40 nodes"));
}
