// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a reading says about the numbers the model already holds.
//!
//! A page epoch change retires every number issued for the frame before it,
//! and the model reading a transcript cannot tell a retired number from a live
//! one — every reading printed the same kind of line. On 2026-09-20 errand
//! `86c83682` named one eight times, including on the only
//! `browser.form.inspect` it ever tried (decision 0220).

use bip_types::identity::PageEpoch;
use bip_types::snapshot::SemanticRole;
use task_engine::handle::HandleTable;
use task_engine::{CurrentDocument, RenderShape, TurnPage};

use super::{node, page};
use crate::context::arena::{PageArena, PageIdentity};
use crate::context::render::{render, PageProjection, RenderBudget};

/// The line this footer prints.
const RETIRED: &str = "numbers from before this reading";

fn arena() -> PageArena {
    let mut arena = PageArena::new();
    assert!(arena.push(node(
        "field",
        SemanticRole::TextField,
        Some("Aadhaar Number"),
        &[]
    )));
    assert!(arena.push(node("send", SemanticRole::Button, Some("Send OTP"), &[])));
    arena
}

fn read(handles: &mut HandleTable, identity: &PageIdentity) -> String {
    match render(&arena(), identity, RenderBudget::new(8192), handles) {
        PageProjection::Rendered(page) => page.text,
        other => panic!("expected a rendered page, got {other:?}"),
    }
}

fn at_epoch(epoch: &str) -> PageIdentity {
    PageIdentity {
        page_epoch: PageEpoch::new(epoch),
        ..page()
    }
}

/// The reading after a navigation says the numbers before it are gone.
#[test]
fn a_reading_of_a_new_document_says_the_older_numbers_are_gone() {
    let mut handles = HandleTable::new();
    let first = read(&mut handles, &page());
    assert!(
        !first.contains(RETIRED),
        "the first reading of a task retires nothing: {first}"
    );

    let second = read(&mut handles, &at_epoch("epoch-2"));

    assert!(
        second.contains(RETIRED),
        "the second reading says the first one's numbers are gone: {second}"
    );
    assert!(
        second.contains("use one from this reading"),
        "and the move that works: {second}"
    );
}

/// Reading the same document again takes nothing away.
///
/// A number from an earlier reading of the *same* document is admitted on
/// purpose (decision 0188), so a line saying otherwise would be false and
/// would send the model to re-read a page it can already act on.
#[test]
fn a_second_reading_of_the_same_document_retires_nothing() {
    let mut handles = HandleTable::new();
    let _ = read(&mut handles, &page());

    let second = read(&mut handles, &page());

    assert!(
        !second.contains(RETIRED),
        "the same document retires nothing: {second}"
    );
}

/// The line says it when the refusal would, and not otherwise.
///
/// The footer exists to say in advance what `names_a_page_its_tab_left` will
/// say after the fact, so the two are asserted against each other rather than
/// each against something written here. A line that appeared when nothing was
/// stale would be worse than no line: it would be advice about numbers that
/// work.
#[test]
fn the_line_appears_exactly_when_the_refusal_would_refuse() {
    let mut handles = HandleTable::new();
    let _ = read(&mut handles, &page());
    let _ = read(&mut handles, &at_epoch("epoch-2"));
    let current = at_epoch("epoch-3");
    let text = read(&mut handles, &current);

    let turn = TurnPage::new(
        current.tab_id.clone(),
        handles.clone(),
        RenderShape::empty([0_u8; 32]),
    )
    .with_current_documents(vec![CurrentDocument {
        tab: current.tab_id.clone(),
        frame: current.frame_id.clone(),
        page_epoch: current.page_epoch.clone(),
    }]);
    let would_refuse = handles
        .bindings()
        .any(|binding| turn.names_a_page_its_tab_left(binding.node()));

    assert!(would_refuse, "two readings of two nodes are now behind");
    assert_eq!(text.contains(RETIRED), would_refuse);
}

/// The line carries no number, and that is load-bearing.
///
/// It first printed a count — `earlier numbers: 333 …` — and in a projection
/// where every integer is a handle a model read it as one: errand `f8356d76`
/// was refused `handle_unknown` on 8 of 43 turns against 1 of 72 before the
/// line existed (decision 0220). Asserted over a reading that retires enough
/// numbers for a count to be conspicuous, so restoring one fails here rather
/// than on a phone.
#[test]
fn the_line_puts_no_number_where_a_handle_would_go() {
    let mut handles = HandleTable::new();
    for _ in 0..4 {
        let _ = read(&mut handles, &page());
    }
    let text = read(&mut handles, &at_epoch("epoch-2"));

    let line = text
        .lines()
        .find(|line| line.starts_with(RETIRED))
        .expect("the retired-numbers line");

    assert!(
        !line.chars().any(|character| character.is_ascii_digit()),
        "no digit may sit beside an instruction to pick a number: {line}"
    );
}

/// A number issued for another tab is neither named nor refused.
///
/// The check is per tab and frame on both sides. Drawing this line for a
/// second tab's numbers would tell the model that handles it can still act on
/// are gone, which is the mistake this footer exists to prevent, inverted.
#[test]
fn numbers_from_another_tab_do_not_draw_the_line() {
    let mut handles = HandleTable::new();
    let other = PageIdentity {
        tab_id: bip_types::identity::TabId::new("tab-2"),
        ..page()
    };
    let _ = read(&mut handles, &other);

    let text = read(&mut handles, &at_epoch("epoch-2"));

    assert!(
        !text.contains(RETIRED),
        "another tab's numbers are still that tab's: {text}"
    );
}
