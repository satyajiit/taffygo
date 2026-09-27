// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::snapshot::{ContentSignal, ContentTrust};

use super::super::decode_page_observation;
use super::*;
use crate::context::PageArena;

/// Framing 4 adds authorship and canonical signal sets to a node and each of
/// its text runs. This fixture deliberately exercises different labels at the
/// two levels so a decoder that keeps only the node's answer cannot satisfy
/// the arena assertions that follow.
fn labelled_graph_with(
    node_trust: u8,
    node_signals: &[u8],
    first_run_trust: u8,
    first_run_signals: &[u8],
    second_run_trust: u8,
    second_run_signals: &[u8],
) -> Vec<u8> {
    let mut out = vec![4]; // framing version
    short(&mut out, PROTOCOL_VERSION);
    u32(&mut out, 1);
    short(&mut out, "node-1");
    short(&mut out, "frame-1");
    u16(&mut out, 0); // DOCUMENT
    out.push(0); // NOT_SENSITIVE
    out.push(0); // no node flags
    out.push(9); // UNKNOWN value kind
    short(&mut out, "Public heading");
    u32(&mut out, 2);
    u64(&mut out, 14);
    u32(&mut out, 0); // no actions
    u32(&mut out, 0); // no states
    out.push(0); // no destination
    out.push(node_trust);
    u32(&mut out, u32::try_from(node_signals.len()).unwrap());
    out.extend_from_slice(node_signals);
    u32(&mut out, 2); // two emitted runs
    short(&mut out, "Public ");
    out.push(0); // RENDERED_TEXT
    out.push(0); // NOT_SENSITIVE
    out.push(0); // not truncated
    out.push(first_run_trust);
    u32(&mut out, u32::try_from(first_run_signals.len()).unwrap());
    out.extend_from_slice(first_run_signals);
    short(&mut out, "heading");
    out.push(0);
    out.push(0);
    out.push(0);
    out.push(second_run_trust);
    u32(&mut out, u32::try_from(second_run_signals.len()).unwrap());
    out.extend_from_slice(second_run_signals);
    u32(&mut out, 0); // no edges
    out
}

fn labelled_graph() -> Vec<u8> {
    labelled_graph_with(
        3,    // USER_GENERATED_CONTENT
        &[4], // IMPERATIVE_INSTRUCTION_SHAPE
        2,    // FIRST_PARTY_DOCUMENT
        &[],
        3,       // USER_GENERATED_CONTENT
        &[0, 4], // HIDDEN_BY_STYLE, IMPERATIVE_INSTRUCTION_SHAPE
    )
}

#[test]
fn framing_four_content_labels_decode_instead_of_disappearing() {
    let mut arena = PageArena::new();
    let input = with_graph(labelled_graph());
    decode_page_observation(9, (&input).into(), Some(&mut arena)).expect("framing 4 decodes");
    let node = arena.nodes().first().expect("one node");
    assert_eq!(node.content_trust, ContentTrust::UserGeneratedContent);
    assert_eq!(
        node.content_signals,
        vec![ContentSignal::ImperativeInstructionShape]
    );
    assert_eq!(node.text[0].content_trust, ContentTrust::FirstPartyDocument);
    assert!(node.text[0].content_signals.is_empty());
    assert_eq!(
        node.text[1].content_trust,
        ContentTrust::UserGeneratedContent
    );
    assert_eq!(
        node.text[1].content_signals,
        vec![
            ContentSignal::HiddenByStyle,
            ContentSignal::ImperativeInstructionShape
        ]
    );
}

#[test]
fn privileged_authorship_and_noncanonical_signal_sets_fail_closed() {
    // Labels only the task kernel or model adapter may mint are independently
    // refused here, even though the browser framing rejects them too.
    for privileged in [0, 1, 5] {
        assert_malformed(with_graph(labelled_graph_with(
            privileged,
            &[],
            2,
            &[],
            3,
            &[],
        )));
        assert_malformed(with_graph(labelled_graph_with(
            2,
            &[],
            privileged,
            &[],
            3,
            &[],
        )));
    }
    // Unknown labels/signals, duplicates, descending order, and a set past
    // the framing's bound are all malformed rather than shortened or sorted.
    assert_malformed(with_graph(labelled_graph_with(99, &[], 2, &[], 3, &[])));
    for signals in [&[0, 0][..], &[4, 0][..], &[99][..], &[0; 9][..]] {
        assert_malformed(with_graph(labelled_graph_with(2, signals, 2, &[], 3, &[])));
    }
}
