// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use crate::agent::{ModelReply, ModelStopReason, ModelToolCall, TurnUsage};
use crate::tool::SuppliedArgument;
use crate::workflow::WorkflowError;

struct Digest;

impl WorkflowDigest for Digest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], WorkflowError> {
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().enumerate() {
            output[index % 32] ^= *byte;
        }
        Ok(output)
    }
}

fn operands() -> ActionOperands {
    ActionOperands::from_reply(&ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![ModelToolCall::new(
            "browser.search",
            vec![SuppliedArgument::new(
                "query",
                ArgumentValue::Text("resident only".to_owned()),
            )],
        )],
    })
}

#[test]
fn a_reference_is_minted_only_after_hashing_bounded_resident_bytes() {
    let operands = operands();
    let reference = operands
        .bind(7, 0, OpaqueOperandKind::SearchQuery, &Digest)
        .expect("resident query");
    assert_eq!(reference.handle(), "turn-7-call-0-search-query");
    assert_eq!(
        operands.resolve(7, 0, &reference, &Digest),
        Ok("resident only")
    );
}

#[test]
fn substitution_and_a_missing_restore_residency_fail_closed() {
    let operands = operands();
    let forged = OpaqueOperandRef::for_call(7, 0, OpaqueOperandKind::SearchQuery, [9; 32]);
    assert_eq!(
        operands.resolve(7, 0, &forged, &Digest),
        Err(OperandResolveError::DigestMismatch)
    );
    let reference = operands
        .bind(7, 0, OpaqueOperandKind::SearchQuery, &Digest)
        .expect("resident query");
    assert_eq!(
        ActionOperands::default().resolve(7, 0, &reference, &Digest),
        Err(OperandResolveError::Missing)
    );
    assert_eq!(
        operands.resolve(8, 0, &reference, &Digest),
        Err(OperandResolveError::ReferenceMismatch)
    );
}
