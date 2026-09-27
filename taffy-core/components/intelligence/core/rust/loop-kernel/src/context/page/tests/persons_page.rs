// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A source heading says when that source is the page the person asked from.
//!
//! Every turn shows every current source, so an errand asked from a page shows
//! the person's page beside the site in the task's own tab, and nothing said
//! which of the two a call may act on. On a phone the model, working a site in
//! its own tab, took a link from the person's page for a `browser.link.open`,
//! and the person's tab was the one that moved. The call is refused now; the
//! heading is what keeps the model from reaching for it (decision 0237).

use super::{evidence, node};
use crate::context::arena::PageArena;
use crate::context::page::{LivePage, MAX_RESEARCH_PROJECTION_BYTES};
use crate::context::vocabulary::PERSONS_PAGE_LINE;
use bip_types::identity::{PageEpoch, TabId};
use task_engine::{PersonsPages, SourceId};

const PERSONS_TAB: &str = "tab-1";
const OWN_TAB: &str = "tab-own";

/// One source on `tab`, whose one line is named `name`.
fn observe(page: &mut LivePage, id: u8, tab: &str, name: &str) {
    let mut arena = PageArena::new();
    assert!(arena.push(node(&format!("n-{id}"), name)));
    observe_arena(page, id, tab, arena);
}

fn observe_arena(page: &mut LivePage, id: u8, tab: &str, arena: PageArena) {
    let mut seen = evidence();
    seen.tab_id = TabId::new(tab);
    seen.page_epoch = PageEpoch::new(format!("epoch-{id}"));
    page.replace_source(SourceId::from_bytes([id; 16]), &seen, arena)
        .expect("canonical evidence");
}

/// What a task that holds the person's page and a tab of its own is told.
fn beside_its_own_tab() -> PersonsPages {
    PersonsPages::new([&TabId::new(PERSONS_TAB)], Some(&TabId::new(OWN_TAB)))
}

/// The text of source `number`, up to the next source's heading.
fn section(text: &str, number: usize) -> &str {
    let start = text
        .find(&format!("Source {number}:\n"))
        .unwrap_or_else(|| panic!("source {number} is rendered: {text}"));
    let rest = &text[start..];
    let next = format!("Source {}:\n", number + 1);
    rest.find(&next).map_or(rest, |end| &rest[..end])
}

/// The rule follows the tab and not the position. The person's page sorts
/// second here, because a source's place is its browser-issued identity, a
/// random UUID — so "Source 1" is not a name for the person's page.
#[test]
fn the_persons_page_says_it_may_only_be_read_beside_the_tasks_own_tab() {
    let mut page = LivePage::new();
    observe(&mut page, 3, OWN_TAB, "Download Aadhaar");
    observe(&mut page, 4, PERSONS_TAB, "Learn more");

    let text = page
        .preview_for_empty_tab(None, &beside_its_own_tab())
        .text
        .expect("two observations render");

    assert_eq!(text.matches(PERSONS_PAGE_LINE).count(), 1, "{text}");
    let own = section(&text, 1);
    let persons = section(&text, 2);
    assert!(own.contains("Download Aadhaar") && !own.contains(PERSONS_PAGE_LINE));
    assert!(persons.contains("Learn more"));
    assert!(
        persons.starts_with(&format!(
            "Source 2:\nEvidence status: complete observation.\n{PERSONS_PAGE_LINE}\n"
        )),
        "the line belongs in the person's heading, before the page: {persons}"
    );
}

/// Before anything lands in the task's own tab, the person's page is the only
/// page there is. It is still read-only, and the heading still says so.
#[test]
fn the_persons_page_alone_says_so_while_the_tasks_own_tab_is_blank() {
    let mut page = LivePage::new();
    observe(&mut page, 3, PERSONS_TAB, "Learn more");
    let text = page
        .preview_for_empty_tab(None, &beside_its_own_tab())
        .text
        .expect("one observation renders");
    assert!(section(&text, 1).contains(PERSONS_PAGE_LINE), "{text}");
}

/// The three turns that must render exactly as they did before this line
/// existed, byte for byte.
#[test]
fn a_turn_with_no_page_of_the_persons_beside_its_own_tab_renders_as_before() {
    // A zero-source errand: it never held a page of the person's, and the
    // only page is the one it opened in its own tab.
    let mut zero_source = LivePage::new();
    observe(&mut zero_source, 3, OWN_TAB, "Download Aadhaar");
    let nobody = PersonsPages::new([], Some(&TabId::new(OWN_TAB)));
    assert_eq!(
        zero_source.preview_for_empty_tab(None, &nobody),
        zero_source.preview()
    );

    // An errand asked from a page whose only current page is its own tab.
    assert_eq!(
        zero_source.preview_for_empty_tab(None, &beside_its_own_tab()),
        zero_source.preview()
    );

    // A task with no tab of its own: there is nowhere else to act, so the
    // person's page is not set apart.
    let mut no_own_tab = LivePage::new();
    observe(&mut no_own_tab, 3, PERSONS_TAB, "Learn more");
    let without = PersonsPages::new([&TabId::new(PERSONS_TAB)], None);
    let rendered = no_own_tab.preview_for_empty_tab(None, &without);
    assert_eq!(rendered, no_own_tab.preview());
    assert!(!rendered
        .text
        .unwrap_or_default()
        .contains(PERSONS_PAGE_LINE));
}

/// The line is part of the heading, and the heading is paid for out of its
/// source's share, so four full sources still fit the aggregate bound.
#[test]
fn the_line_is_paid_for_out_of_its_sources_share() {
    let mut page = LivePage::new();
    let long_name = "a line long enough that no source fits its share ".repeat(4);
    for (id, tab) in [(3, "tab-1"), (4, "tab-2"), (5, "tab-3"), (6, OWN_TAB)] {
        let mut arena = PageArena::new();
        for index in 0..1_500 {
            assert!(arena.push(node(&format!("n-{id}-{index}"), &long_name)));
        }
        observe_arena(&mut page, id, tab, arena);
    }
    let three = PersonsPages::new(
        [
            &TabId::new("tab-1"),
            &TabId::new("tab-2"),
            &TabId::new("tab-3"),
        ],
        Some(&TabId::new(OWN_TAB)),
    );
    let text = page
        .preview_for_empty_tab(None, &three)
        .text
        .expect("four observations render");
    assert_eq!(text.matches(PERSONS_PAGE_LINE).count(), 3);
    // Every source overflowed its share, so the bound is being tested at
    // the edge rather than passed with room to spare.
    assert!(
        text.len() > MAX_RESEARCH_PROJECTION_BYTES * 9 / 10
            && text.len() <= MAX_RESEARCH_PROJECTION_BYTES,
        "{} bytes",
        text.len()
    );
}
