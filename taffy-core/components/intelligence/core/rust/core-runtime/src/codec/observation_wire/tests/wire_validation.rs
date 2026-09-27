// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use task_engine::ObservationCompleteness;

use super::*;

#[test]
fn a_valid_graph_becomes_only_bounded_structural_evidence() {
    let evidence = decode_page_observation_evidence(9, (&result()).into()).unwrap();

    assert_eq!(evidence.service_generation, 9);
    assert_eq!(evidence.completeness, ObservationCompleteness::Complete);
    assert_eq!(evidence.graph.node_count, 1);
    assert_eq!(evidence.graph.relationship_count, 1);
    assert_eq!(evidence.graph.named_node_count, 1);
    assert_eq!(evidence.graph.text_run_count, 2);
    assert_eq!(evidence.graph.text_byte_count, 14);
}

#[test]
fn incomplete_is_explicit_and_never_promoted_to_complete() {
    let mut input = result();
    input.status = wire::BipObservationStatus::Incomplete;
    input.truncated = true;

    let evidence = decode_page_observation_evidence(9, (&input).into()).unwrap();

    assert_eq!(evidence.completeness, ObservationCompleteness::Incomplete);
    assert!(!evidence.supports_complete_result());
}

#[test]
fn an_incomplete_reading_needs_no_budget_to_blame() {
    // An adapter that could not fully report makes the result incomplete on
    // its own, with nothing truncated and the answer unchanged. Refusing that
    // envelope refused the whole completion and ended the task (decision
    // 0171).
    let mut input = result();
    input.status = wire::BipObservationStatus::Incomplete;
    assert!(!input.truncated && !input.may_change_answer);

    let evidence = decode_page_observation_evidence(9, (&input).into()).unwrap();

    assert_eq!(evidence.completeness, ObservationCompleteness::Incomplete);
    assert!(!evidence.supports_complete_result());
}

#[test]
fn a_page_that_disagrees_with_itself_is_still_a_reading() {
    // The browser delivers a conflicted page as a reading (decision 0207).
    // Refusing it refused the completion of a read that had happened and
    // handed the errand to the person (decision 0234).
    let mut input = result();
    input.status = wire::BipObservationStatus::Conflicted;

    let evidence = decode_page_observation_evidence(9, (&input).into()).unwrap();

    assert_eq!(evidence.completeness, ObservationCompleteness::Incomplete);
    assert!(!evidence.supports_complete_result());
}

#[test]
fn non_success_and_contradictory_completeness_fail_closed() {
    let mut unsupported = result();
    unsupported.status = wire::BipObservationStatus::StalePageEpoch;
    assert_eq!(
        decode_page_observation_evidence(9, (&unsupported).into()),
        Err(ObservationWireError::UnsupportedStatus)
    );

    let mut contradictory = result();
    contradictory.truncated = true;
    assert_eq!(
        decode_page_observation_evidence(9, (&contradictory).into()),
        Err(ObservationWireError::InvalidEnvelope)
    );
}

#[test]
fn envelope_graph_disagreement_fails_closed() {
    let mut wrong_count = result();
    wrong_count.node_count = 2;
    assert_eq!(
        decode_page_observation_evidence(9, (&wrong_count).into()),
        Err(ObservationWireError::GraphMismatch)
    );

    let mut wrong_size = result();
    wrong_size.total_bytes = wrong_size.total_bytes.saturating_add(1);
    assert_eq!(
        decode_page_observation_evidence(9, (&wrong_size).into()),
        Err(ObservationWireError::InvalidEnvelope)
    );
}

#[test]
fn unknown_closed_values_wrong_frame_and_trailing_bytes_are_malformed() {
    let role_offset = 1 + 2 + PROTOCOL_VERSION.len() + 4 + 2 + 6 + 2 + 7;
    let mut unknown_role = result();
    unknown_role.graph_payload[role_offset] = 0xff;
    assert_malformed(unknown_role);

    let mut wrong_frame = result();
    wrong_frame.frame_id = "frame-2".to_owned();
    assert_malformed(wrong_frame);

    let mut trailing = result();
    trailing.graph_payload.push(0);
    trailing.total_bytes = u32::try_from(trailing.graph_payload.len()).unwrap();
    assert_malformed(trailing);
}

#[test]
fn duplicate_node_identifiers_are_refused_without_allocating_validation_copies() {
    let mut payload = vec![4];
    short(&mut payload, PROTOCOL_VERSION);
    u32(&mut payload, 2);
    push_node(&mut payload, "node-1");
    push_node(&mut payload, "node-1");
    u32(&mut payload, 0);
    let mut duplicate = with_graph(payload);
    duplicate.node_count = 2;

    assert_malformed(duplicate);
}

#[test]
fn every_truncated_graph_prefix_is_refused_without_guessing() {
    let complete = result();
    for length in 0..complete.graph_payload.len() {
        let mut prefix = complete.clone();
        prefix.graph_payload.truncate(length);
        prefix.total_bytes = u32::try_from(length).unwrap();
        assert!(decode_page_observation_evidence(9, (&prefix).into()).is_err());
    }
}
