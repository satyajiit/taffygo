// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

/// A graph built like [`graph`] but with the node's text block replaced.
///
/// The rest of the row is fixed, so each case below differs from a payload that
/// decodes in exactly one way, and a refusal names that one way.
fn graph_with_text(node_flags: u8, emitted: &[(&str, u8, u8, u8)]) -> Vec<u8> {
    let mut out = vec![4];
    short(&mut out, PROTOCOL_VERSION);
    u32(&mut out, 1);
    short(&mut out, "node-1");
    short(&mut out, "frame-1");
    u16(&mut out, 0);
    out.push(0); // NOT_SENSITIVE
    out.push(node_flags);
    out.push(9);
    short(
        &mut out,
        if node_flags & 0x01 == 0 {
            "Public heading"
        } else {
            ""
        },
    );
    u32(&mut out, 2); // the node had two runs
    u64(&mut out, 14);
    u32(&mut out, 0); // no actions
    u32(&mut out, 0); // no states
    out.push(0); // no destination
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(&mut out, 0); // no node signals
    u32(&mut out, u32::try_from(emitted.len()).unwrap());
    for (text, source_kind, sensitivity, flags) in emitted {
        short(&mut out, text);
        out.push(*source_kind);
        out.push(*sensitivity);
        out.push(*flags);
        out.push(2); // FIRST_PARTY_DOCUMENT
        u32(&mut out, 0); // no run signals
    }
    u32(&mut out, 0); // no edges
    out
}

/// Every way the text block may not arrive. Each is a rule the encoder applies
/// and this parser exists to stop trusting.
#[test]
fn a_text_block_that_disagrees_with_its_own_node_is_refused() {
    // Carrying fewer runs than the node declared, without saying so.
    assert_malformed(with_graph(graph_with_text(
        0,
        &[("Public heading", 0, 0, 0)],
    )));
    // Saying text was withheld while carrying all of it.
    assert_malformed(with_graph(graph_with_text(
        0x20,
        &[("Public ", 0, 0, 0), ("heading", 0, 0, 0)],
    )));
    // More runs than the node ever had.
    assert_malformed(with_graph(graph_with_text(
        0,
        &[("a", 0, 0, 0), ("b", 0, 0, 0), ("c", 0, 0, 0)],
    )));
    // Text on a node whose own name was withheld: a node too sensitive to
    // label is too sensitive to quote, and the encoder never emits this.
    assert_malformed(with_graph(graph_with_text(
        0x01,
        &[("Public ", 0, 0, 0), ("heading", 0, 0, 0)],
    )));
    // A run that admits it is sensitive. It should never have crossed, so
    // meeting one here means the bytes did not come from the encoder.
    assert_malformed(with_graph(graph_with_text(
        0,
        &[("Public ", 0, 0, 0), ("heading", 0, 2, 0)],
    )));
    // A source kind outside the closed enumeration, and a run flag this build
    // does not know.
    assert_malformed(with_graph(graph_with_text(
        0,
        &[("Public ", 200, 0, 0), ("heading", 0, 0, 0)],
    )));
    assert_malformed(with_graph(graph_with_text(
        0,
        &[("Public ", 0, 0, 0), ("heading", 0, 0, 0x80)],
    )));
}

#[test]
fn a_node_that_withheld_its_text_says_so_and_decodes() {
    // The honest shape of a partial answer: one run of two, and the flag.
    let evidence = decode_page_observation_evidence(
        9,
        (&with_graph(graph_with_text(0x20, &[("Public ", 0, 0, 0)]))).into(),
    )
    .expect("a node may carry less text than it had, provided it says so");
    // The two measurements stay the page's own, so a consumer can see that
    // fourteen bytes existed and seven arrived.
    assert_eq!(evidence.graph.text_run_count, 2);
    assert_eq!(evidence.graph.text_byte_count, 14);
}
