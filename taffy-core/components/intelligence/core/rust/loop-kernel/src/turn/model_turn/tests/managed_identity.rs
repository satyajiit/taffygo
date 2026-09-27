// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

/// A deterministic local stand-in for the browser's SHA-256 adapter.
///
/// Local rather than shared: `kernel-test-support` depends on this crate, so
/// its double cannot be used from here. Distinctness and determinism are all
/// these tests need.
#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct FoldDigest;

impl Sha256Port for FoldDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().copied().enumerate() {
            let slot = index % output.len();
            if let Some(cell) = output.get_mut(slot) {
                *cell = cell.rotate_left(1) ^ byte;
            }
        }
        Ok(output)
    }
}

#[test]
fn the_managed_body_is_the_canonical_schema_with_a_minted_identity() {
    let request_id =
        managed_request_id(&FoldDigest, "model-task-1-3").expect("an identity derives");
    let body = write_managed_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        &request_id,
        None,
        None,
    )
    .expect("a spoken transcript writes");
    assert!(
        body.contains("\"schema_version\":3"),
        "the canonical version is stated: {body}"
    );
    assert!(
        body.contains("\"stream\":true"),
        "version three requests the canonical response stream: {body}"
    );
    assert!(
        body.contains(&request_id),
        "the minted identity rides the body: {body}"
    );
    assert!(
        body.contains("find the download link"),
        "the goal is the opening turn: {body}"
    );
    assert!(
        body.contains("\"tools\""),
        "the canonical schema carries the tool vocabulary (decision 0092): {body}"
    );
}

#[test]
fn a_transcript_with_tool_exchanges_now_rides_the_managed_body() {
    // This test used to assert the opposite, and the sentence it asserted is
    // worth keeping: at schema version 1 the canonical wire had no tool
    // vocabulary, so a replayed tool turn was refused by the writer on its own
    // account rather than by route selection having refused first. Version 2
    // carries tools (decision 0092), so what is under test is now that the
    // exchange survives the crossing with the identity the core minted — the
    // property that keeps a recorded procedure replayable on either route.
    let body = write_managed_body(
        &candidate(true),
        &transcript(vec![exchange(1, "browser.dom.read")]),
        &tools(),
        "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00",
        None,
        None,
    )
    .expect("a tool exchange rides the canonical streaming schema");
    assert!(
        body.contains("browser.dom.read"),
        "the call the model made is in the body: {body}"
    );
    assert!(
        body.contains("turn-1-call-0"),
        "the identity is the core's own, never a provider's: {body}"
    );
}

#[test]
fn a_candidate_that_cannot_call_a_tool_is_handed_none_on_the_managed_route_either() {
    // The direct route's rule, applied here rather than assumed: a model the
    // catalog says cannot answer with a tool call is offered no tools, so the
    // body agrees with the candidate instead of relying on route selection
    // having refused it.
    let body = write_managed_body(
        &candidate(false),
        &transcript(Vec::new()),
        &tools(),
        "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00",
        None,
        None,
    )
    .expect("a spoken transcript writes");
    // The key is absent rather than empty. An empty array and no array say the
    // same thing to the Worker, and the shorter one is what the writer emits.
    assert!(
        !body.contains("\"tools\""),
        "no tool is offered to a model that cannot call one: {body}"
    );
}

#[test]
fn the_managed_request_identity_is_a_stable_version_four_uuid() {
    let one = managed_request_id(&FoldDigest, "model-task-1-3").expect("derives");
    let again = managed_request_id(&FoldDigest, "model-task-1-3").expect("derives");
    let other = managed_request_id(&FoldDigest, "model-task-1-4").expect("derives");
    // Stable across a retry of the same call, distinct across calls: the
    // worker's exactly-once metering is keyed on exactly this behaviour.
    assert_eq!(one, again);
    assert_ne!(one, other);

    let bytes = one.as_bytes();
    assert_eq!(one.len(), 36);
    assert!(
        [8, 13, 18, 23]
            .into_iter()
            .all(|index| bytes.get(index) == Some(&b'-')),
        "hyphens where a UUID puts them: {one}"
    );
    assert_eq!(bytes.get(14), Some(&b'4'), "version four: {one}");
    assert!(
        matches!(bytes.get(19), Some(b'8' | b'9' | b'a' | b'b')),
        "an RFC 4122 variant: {one}"
    );
    assert!(
        one.bytes()
            .all(|byte| matches!(byte, b'0'..=b'9' | b'a'..=b'f' | b'-')),
        "lowercase hex, the one spelling the worker admits: {one}"
    );
}
