// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A DOM query borrows retained nodes in document order and never claims completeness it lacks.

use super::node;
use crate::context::arena::{ArenaNode, PageArena, MAX_ARENA_NODES};
use bip_types::snapshot::SemanticRole;
use task_engine::action::DomQueryRole;

#[test]
fn a_dom_query_filters_role_text_subtree_and_limit_in_document_order() {
    let mut arena = PageArena::new();
    let mut region = node("region-1", Some("Plans"), &[]);
    region.role = SemanticRole::Region;
    let mut first = node("button-1", Some("Choose Basic"), &[]);
    first.role = SemanticRole::Button;
    let mut second = node("button-2", Some("Choose Pro"), &[]);
    second.role = SemanticRole::Button;
    let mut outside = node("button-3", Some("Choose Enterprise"), &[]);
    outside.role = SemanticRole::Button;
    assert!(arena.push(region));
    assert!(arena.push(first));
    assert!(arena.push(second));
    assert!(arena.push(outside));
    arena.apply_contains(&[("region-1", "button-1"), ("region-1", "button-2")]);

    let result = arena.query(
        Some("region-1"),
        Some(DomQueryRole::Button),
        Some("CHOOSE"),
        1,
    );

    assert_eq!(result.matched, 2);
    assert!(!result.complete, "the explicit limit omitted one match");
    assert_eq!(result.retained(), 1);
    let retained: Vec<&ArenaNode> = result.nodes(&arena).collect();
    let Some(retained_node) = retained.first().copied() else {
        panic!("one retained node was required")
    };
    let Some(source_node) = arena.nodes().get(1) else {
        panic!("the source button was required")
    };
    assert_eq!(retained_node.node_id, "button-1");
    assert!(std::ptr::eq(retained_node, source_node));
}

#[test]
fn a_maximum_query_borrows_every_retained_node_without_cloning_payloads() {
    let mut arena = PageArena::new();
    for index in 0..MAX_ARENA_NODES {
        assert!(arena.push(node(
            &format!("node-{index:03}"),
            Some("matching content"),
            &["already-redacted text"]
        )));
    }

    let result = arena.query(None, None, None, MAX_ARENA_NODES);

    assert_eq!(result.matched, MAX_ARENA_NODES);
    assert_eq!(result.retained(), MAX_ARENA_NODES);
    let mut retained = result.nodes(&arena);
    let Some(first_retained) = retained.next() else {
        panic!("a first retained node was required")
    };
    let Some(last_retained) = retained.last() else {
        panic!("a last retained node was required")
    };
    let Some(first_source) = arena.nodes().first() else {
        panic!("the source arena was required")
    };
    let Some(last_source) = arena.nodes().last() else {
        panic!("the source arena was required")
    };
    assert!(std::ptr::eq(first_retained, first_source));
    assert!(std::ptr::eq(last_retained, last_source));
}

#[test]
fn withheld_candidate_text_makes_a_zero_match_query_incomplete() {
    let mut arena = PageArena::new();
    let mut hidden = node("button-1", None, &[]);
    hidden.role = SemanticRole::Button;
    hidden.name_withheld = true;
    assert!(arena.push(hidden));

    let result = arena.query(None, Some(DomQueryRole::Button), Some("download"), 8);

    assert_eq!(result.matched, 0);
    assert!(!result.complete);
    assert!(result.is_empty());
}

#[test]
fn an_absent_within_node_is_an_incomplete_empty_result() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("button-1", Some("Download"), &[])));

    let result = arena.query(Some("stale-node"), None, None, 8);

    assert_eq!(result.matched, 0);
    assert!(!result.complete);
    assert!(result.is_empty());
}
