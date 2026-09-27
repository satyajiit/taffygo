// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Form membership from contains edges: first valid parent, indexed at the bound.

use super::node;
use crate::context::arena::{
    DestinationClass, PageArena, MAX_ARENA_CONTAINS_EDGES, MAX_ARENA_NODES,
};
use bip_types::snapshot::SemanticRole;

#[test]
fn a_contains_edge_from_a_form_region_marks_its_controls() {
    let mut arena = PageArena::new();
    let mut form = node("form-1", Some("Sign in"), &[]);
    form.role = SemanticRole::Region;
    form.destination = DestinationClass::from_bits(1);
    let mut field = node("field-1", Some("Email"), &[]);
    field.role = SemanticRole::TextField;
    assert!(arena.push(form));
    assert!(arena.push(field));
    arena.apply_contains(&[("form-1", "field-1"), ("form-1", "form-1")]);
    let field = arena
        .nodes()
        .iter()
        .find(|candidate| candidate.node_id == "field-1")
        .expect("the field");
    assert_eq!(field.container.as_deref(), Some("form-1"));
    let form = arena
        .nodes()
        .iter()
        .find(|candidate| candidate.node_id == "form-1")
        .expect("the form");
    assert!(form.container.is_none());
}

#[test]
fn a_region_that_leads_nowhere_is_not_a_form() {
    let mut arena = PageArena::new();
    let mut landmark = node("nav-1", Some("Menu"), &[]);
    landmark.role = SemanticRole::Region;
    let mut field = node("field-1", Some("Search"), &[]);
    field.role = SemanticRole::SearchField;
    assert!(arena.push(landmark));
    assert!(arena.push(field));
    arena.apply_contains(&[("nav-1", "field-1")]);
    let field = arena
        .nodes()
        .iter()
        .find(|candidate| candidate.node_id == "field-1")
        .expect("the field");
    assert!(field.container.is_none());
}

#[test]
fn contains_edges_keep_the_first_valid_parent_and_ignore_invalid_edges() {
    let mut arena = PageArena::new();
    for form_id in ["form-first", "form-second"] {
        let mut form = node(form_id, Some("Form"), &[]);
        form.role = SemanticRole::Region;
        form.destination = DestinationClass::from_bits(1);
        assert!(arena.push(form));
    }
    assert!(arena.push(node("field", Some("Field"), &[])));

    arena.apply_contains(&[
        ("missing-parent", "field"),
        ("form-first", "missing-child"),
        ("field", "field"),
        ("form-first", "field"),
        ("form-first", "field"),
        ("form-second", "field"),
    ]);

    assert_eq!(
        arena.nodes()[2].container.as_deref(),
        Some("form-first"),
        "edge order, not identifier order, chooses the first valid parent"
    );
    assert!(arena.nodes()[0].container.is_none());
    assert!(arena.nodes()[1].container.is_none());
}

#[test]
fn reapplying_containment_replaces_form_membership_and_the_query_index() {
    let mut arena = PageArena::new();
    for form_id in ["form-old", "form-new"] {
        let mut form = node(form_id, Some("Form"), &[]);
        form.role = SemanticRole::Region;
        form.destination = DestinationClass::from_bits(1);
        assert!(arena.push(form));
    }
    assert!(arena.push(node("field", Some("Field"), &[])));

    arena.apply_contains(&[("form-old", "field")]);
    assert_eq!(arena.nodes()[2].container.as_deref(), Some("form-old"));
    assert_eq!(arena.query(Some("form-old"), None, None, 8).matched, 2);

    arena.apply_contains(&[("form-new", "field")]);
    assert_eq!(arena.nodes()[2].container.as_deref(), Some("form-new"));
    assert_eq!(
        arena.query(Some("form-old"), None, None, 8).matched,
        1,
        "the previous adjacency must not survive a replacement"
    );
    assert_eq!(arena.query(Some("form-new"), None, None, 8).matched, 2);
}

#[test]
fn contains_index_preserves_duplicate_identifier_semantics() {
    let mut arena = PageArena::new();
    // The first copy owns child lookup position, even when a duplicate
    // follows it. This is the choice the old `find` made.
    assert!(arena.push(node("duplicate-child", Some("First"), &[])));
    assert!(arena.push(node("duplicate-child", Some("Second"), &[])));
    // Any form-shaped copy made an identifier a valid parent before; the
    // non-form copy deliberately arrives first to lock that distinction.
    assert!(arena.push(node("duplicate-parent", Some("Landmark"), &[])));
    let mut form = node("duplicate-parent", Some("Form"), &[]);
    form.role = SemanticRole::Region;
    form.destination = DestinationClass::from_bits(1);
    assert!(arena.push(form));

    arena.apply_contains(&[("duplicate-parent", "duplicate-child")]);

    assert_eq!(
        arena.nodes()[0].container.as_deref(),
        Some("duplicate-parent")
    );
    assert!(arena.nodes()[1].container.is_none());
}

#[test]
fn contains_lookup_work_is_indexed_at_the_supported_node_limit() {
    let mut arena = PageArena::new();
    let mut form = node("form-000", Some("Form"), &[]);
    form.role = SemanticRole::Region;
    form.destination = DestinationClass::from_bits(1);
    assert!(arena.push(form));
    for index in 1..MAX_ARENA_NODES {
        assert!(arena.push(node(&format!("field-{index:04}"), Some("Field"), &[])));
    }
    // A tree: one parent per child. The square of the node ceiling is no
    // longer a representable graph here, and a complete bipartite product
    // at 1500 nodes is more than half a million edges for a relation the
    // renderer never emits.
    let owned_edges: Vec<(String, String)> = (1..MAX_ARENA_NODES)
        .map(|child| ("form-000".to_string(), format!("field-{child:04}")))
        .collect();
    let edges: Vec<(&str, &str)> = owned_edges
        .iter()
        .map(|(parent, child)| (parent.as_str(), child.as_str()))
        .collect();

    let work = arena.apply_contains_counted(&edges);

    assert_eq!(arena.nodes().len(), MAX_ARENA_NODES);
    assert_eq!(work.index_build_visits, MAX_ARENA_NODES);
    assert_eq!(work.ordered_lookups, edges.len() * 2);
    assert!(
        edges.len() < MAX_ARENA_CONTAINS_EDGES,
        "a tree at the node ceiling must still fit the linear contains bound"
    );
    for child in &arena.nodes()[1..] {
        assert_eq!(child.container.as_deref(), Some("form-000"));
    }
    let complete = arena.query(None, None, None, MAX_ARENA_NODES);
    assert!(complete.complete);
}

#[test]
fn a_contains_edge_past_the_ceiling_marks_containment_incomplete() {
    let mut arena = PageArena::new();
    let mut form = node("form-1", Some("Form"), &[]);
    form.role = SemanticRole::Region;
    form.destination = DestinationClass::from_bits(1);
    assert!(arena.push(form));
    assert!(arena.push(node("field-1", Some("Field"), &[])));
    let extra = MAX_ARENA_CONTAINS_EDGES.saturating_add(1);
    let edges: Vec<(&str, &str)> = (0..extra).map(|_| ("form-1", "field-1")).collect();
    arena.apply_contains(&edges);
    let result = arena.query(None, None, None, 8);
    assert!(
        !result.complete,
        "crossing the contains ceiling must not look like a complete tree"
    );
    assert_eq!(arena.nodes()[1].container.as_deref(), Some("form-1"));
}
