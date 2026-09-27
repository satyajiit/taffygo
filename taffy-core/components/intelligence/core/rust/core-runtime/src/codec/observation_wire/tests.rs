// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::PROTOCOL_VERSION;
use core_service_types as wire;

use super::{decode_page_observation_evidence, ObservationWireError};

mod arena_projection;
mod content_labels;
mod media;
mod text_integrity;
mod wire_validation;

fn u16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn u64(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn short(out: &mut Vec<u8>, value: &str) {
    u16(out, u16::try_from(value.len()).unwrap());
    out.extend_from_slice(value.as_bytes());
}

fn push_node(out: &mut Vec<u8>, node_id: &str) {
    short(out, node_id);
    short(out, "frame-1");
    u16(out, 0); // DOCUMENT
    out.push(0); // NOT_SENSITIVE
    out.push(0); // no node flags
    out.push(9); // UNKNOWN value kind
    short(out, "Public heading");
    u32(out, 2);
    u64(out, 14);
    u32(out, 1); // one available action
    u16(out, 1); // FOCUS
    u32(out, 1); // one state
    out.push(0); // VISIBLE, which describes the node and not its value
    out.push(0); // no destination
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(out, 0); // no node signals
                 // Framing 4: both runs the node declared, carried. Emitting fewer than the
                 // declared count without the withheld flag is refused, and so is the
                 // reverse, so this fixture has to agree with itself.
    u32(out, 2); // two emitted runs
    short(out, "Public ");
    out.push(0); // RENDERED_TEXT
    out.push(0); // NOT_SENSITIVE
    out.push(0); // not truncated
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(out, 0); // no run signals
    short(out, "heading");
    out.push(0);
    out.push(0);
    out.push(0);
    out.push(2);
    u32(out, 0);
}

fn graph() -> Vec<u8> {
    let mut out = vec![4]; // framing version
    short(&mut out, PROTOCOL_VERSION);
    u32(&mut out, 1);
    push_node(&mut out, "node-1");
    u32(&mut out, 1);
    short(&mut out, "node-1");
    short(&mut out, "node-1");
    u16(&mut out, 0); // CONTAINS
    out.push(0);
    out
}

fn result() -> wire::ObservationEffectResult {
    let payload = graph();
    wire::ObservationEffectResult {
        status: wire::BipObservationStatus::Ok,
        schema_version: PROTOCOL_VERSION.to_owned(),
        tab_id: "tab-1".to_owned(),
        frame_id: "frame-1".to_owned(),
        page_epoch: "epoch-1".to_owned(),
        graph_revision: 7,
        origin: "https://example.test".to_owned(),
        is_potentially_trustworthy: true,
        private_profile: false,
        node_count: 1,
        total_bytes: u32::try_from(payload.len()).unwrap(),
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 1,
        suppressed_secret_value_count: 2,
        sensitive_zone_count: 3,
        policy_filtered_frame_count: 4,
        highest_sensitivity: wire::BipSensitivity::NotSensitive,
        graph_encoding: wire::BipGraphEncoding::BipContract,
        graph_payload: payload,
        media: None,
    }
}

fn with_graph(payload: Vec<u8>) -> wire::ObservationEffectResult {
    let mut input = result();
    input.total_bytes = u32::try_from(payload.len()).unwrap();
    input.graph_payload = payload;
    input
}

fn assert_malformed(mut input: wire::ObservationEffectResult) {
    input.total_bytes = u32::try_from(input.graph_payload.len()).unwrap();
    assert_eq!(
        decode_page_observation_evidence(9, (&input).into()),
        Err(ObservationWireError::MalformedGraph)
    );
}
