// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::decode_page_observation;
use super::*;
use crate::context::{PageArena, Readability};

#[test]
fn the_arena_carries_what_the_payload_was_allowed_to_say() {
    let mut arena = PageArena::new();
    let input = result();
    decode_page_observation(9, (&input).into(), Some(&mut arena)).expect("the fixture decodes");

    let nodes = arena.nodes();
    assert_eq!(nodes.len(), 1);
    let node = nodes.first().expect("one node");
    assert_eq!(node.node_id, "node-1");
    assert_eq!(node.name.as_deref(), Some("Public heading"));
    assert_eq!(
        node.text
            .iter()
            .map(|run| run.text.as_str())
            .collect::<Vec<_>>(),
        vec!["Public ", "heading"]
    );
    assert!(!node.text_withheld);
    // The page's own measurements, not the arena's. Fourteen bytes existed and
    // fourteen arrived, and a consumer can see that because both numbers are
    // here rather than one of them being inferred from the other.
    assert_eq!(node.declared_text_runs, 2);
    assert_eq!(node.declared_text_bytes, 14);
    assert!(node.container.is_none());
    assert_eq!(arena.readability(), Readability::Readable);
}

fn form_graph() -> Vec<u8> {
    let mut out = vec![4];
    short(&mut out, PROTOCOL_VERSION);
    u32(&mut out, 2);
    short(&mut out, "form-1");
    short(&mut out, "frame-1");
    u16(&mut out, 1); // REGION
    out.push(0);
    out.push(0);
    out.push(9);
    short(&mut out, "Sign in");
    u32(&mut out, 0);
    u64(&mut out, 0);
    u32(&mut out, 0);
    u32(&mut out, 0);
    out.push(1); // destination present
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(&mut out, 0); // no node signals
    u32(&mut out, 0);
    short(&mut out, "field-1");
    short(&mut out, "frame-1");
    u16(&mut out, 12); // TEXT_FIELD
    out.push(0);
    out.push(0);
    out.push(9);
    short(&mut out, "Email");
    u32(&mut out, 0);
    u64(&mut out, 0);
    u32(&mut out, 0);
    u32(&mut out, 0);
    out.push(0);
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(&mut out, 0); // no node signals
    u32(&mut out, 0);
    u32(&mut out, 1);
    short(&mut out, "form-1");
    short(&mut out, "field-1");
    u16(&mut out, 0); // CONTAINS
    out.push(0);
    out
}

#[test]
fn a_contains_edge_from_a_form_region_reaches_the_arena() {
    let mut arena = PageArena::new();
    let mut input = with_graph(form_graph());
    input.node_count = 2;
    decode_page_observation(9, (&input).into(), Some(&mut arena))
        .expect("the form fixture decodes");
    let field = arena
        .nodes()
        .iter()
        .find(|node| node.node_id == "field-1")
        .expect("the field");
    assert_eq!(field.container.as_deref(), Some("form-1"));
}

#[test]
fn a_page_whose_text_was_all_withheld_is_unreadable_rather_than_empty() {
    // A node with a name withheld and its text withheld with it: exactly the
    // shape a sensitive form produces, and exactly the shape that would
    // otherwise be indistinguishable from a blank page.
    let mut payload = vec![4];
    short(&mut payload, PROTOCOL_VERSION);
    u32(&mut payload, 1);
    short(&mut payload, "node-1");
    short(&mut payload, "frame-1");
    u16(&mut payload, 0);
    payload.push(2); // PERSONAL
    payload.push(0x21); // NAME_WITHHELD | TEXT_WITHHELD
    payload.push(9);
    short(&mut payload, "");
    u32(&mut payload, 3); // three runs the page had
    u64(&mut payload, 640);
    u32(&mut payload, 0);
    u32(&mut payload, 0);
    payload.push(0);
    payload.push(2); // FIRST_PARTY_DOCUMENT
    u32(&mut payload, 0); // no node signals
    u32(&mut payload, 0); // and none of them carried
    u32(&mut payload, 0);

    let mut arena = PageArena::new();
    decode_page_observation(9, (&with_graph(payload)).into(), Some(&mut arena))
        .expect("withholding everything is a valid payload, not a malformed one");
    assert_eq!(
        arena.readability(),
        Readability::Unreadable {
            node_count: 1,
            text_bytes: 640,
        }
    );
}
