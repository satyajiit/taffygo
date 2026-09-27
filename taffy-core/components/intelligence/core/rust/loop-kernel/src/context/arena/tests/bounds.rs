// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Readability, and the node and byte bounds that refuse rather than trim.

use super::node;
use crate::context::arena::{PageArena, Readability, MAX_ARENA_NODES, MAX_ARENA_TEXT_BYTES};
use task_engine::MAX_RETAINED_BINDINGS;

#[test]
fn a_page_with_content_is_readable() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-1", Some("Heading"), &["Some words."])));
    assert_eq!(arena.readability(), Readability::Readable);
    assert_eq!(arena.text_bytes(), "Some words.".len());
}

#[test]
fn a_genuinely_blank_page_is_empty_and_not_unreadable() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-1", None, &[])));
    assert_eq!(arena.readability(), Readability::Empty);
}

#[test]
fn a_page_that_withheld_everything_is_unreadable_and_never_empty() {
    // The distinction this type exists for. Both arenas carry no content;
    // only one of them describes a page that held none.
    let mut arena = PageArena::new();
    let mut withheld = node("n-1", None, &[]);
    withheld.name_withheld = true;
    withheld.text_withheld = true;
    withheld.declared_text_runs = 4;
    withheld.declared_text_bytes = 900;
    assert!(arena.push(withheld));
    assert_eq!(
        arena.readability(),
        Readability::Unreadable {
            node_count: 1,
            text_bytes: 900,
        }
    );
}

#[test]
fn two_full_observations_keep_every_issued_handle() {
    assert!(
        MAX_RETAINED_BINDINGS >= MAX_ARENA_NODES.saturating_mul(2),
        "a page at the observation ceiling must leave every printed handle resolvable, and one more observation besides"
    );
}

#[test]
fn the_node_bound_refuses_rather_than_trimming() {
    let mut arena = PageArena::new();
    for index in 0..MAX_ARENA_NODES {
        assert!(arena.push(node(&format!("n-{index}"), Some("x"), &[])));
    }
    assert!(!arena.push(node("one-too-many", Some("x"), &[])));
    assert_eq!(arena.nodes().len(), MAX_ARENA_NODES);
}

#[test]
fn the_byte_bound_refuses_rather_than_trimming() {
    let mut arena = PageArena::new();
    let huge = "x".repeat(MAX_ARENA_TEXT_BYTES);
    assert!(arena.push(node("n-1", None, &[&huge])));
    assert!(!arena.push(node("n-2", None, &["one more byte"])));
    assert_eq!(arena.text_bytes(), MAX_ARENA_TEXT_BYTES);
}

#[test]
fn clearing_gives_the_bytes_back() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-1", Some("Heading"), &["Some words."])));
    arena.clear();
    assert!(arena.nodes().is_empty());
    assert_eq!(arena.text_bytes(), 0);
    assert_eq!(arena.readability(), Readability::Empty);
}
