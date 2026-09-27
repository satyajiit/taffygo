// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical binding material for one accepted model tool call.

use crate::action::ActionIntent;
use crate::ids::IdempotencyKey;

use super::AgentError;

/// The bounded canonical material one proposal's digest is taken over.
///
/// Length-prefixed field by field, so two different splittings of the same
/// bytes cannot produce the same digest. It names the task, the paid call, the
/// position within that call's frozen reply, the registered tool, what it acts
/// on, and the idempotency key.
pub(super) fn proposal_material(
    task_id: &str,
    call_id: &str,
    sequence: u32,
    intent: &ActionIntent,
    key: &IdempotencyKey,
) -> Result<Vec<u8>, AgentError> {
    let mut out = b"taffy.agent.tool-call.proposal.v2".to_vec();
    proposal_field(&mut out, 0, task_id.as_bytes())?;
    proposal_field(&mut out, 1, call_id.as_bytes())?;
    proposal_field(&mut out, 2, &sequence.to_le_bytes())?;
    let intent = intent
        .encode_canonical()
        .map_err(|_| AgentError::ProposalEncodingOverflow)?;
    proposal_field(&mut out, 3, &intent)?;
    proposal_field(&mut out, 4, key.as_str().as_bytes())?;
    Ok(out)
}

fn proposal_field(out: &mut Vec<u8>, tag: u8, bytes: &[u8]) -> Result<(), AgentError> {
    let length = u64::try_from(bytes.len()).map_err(|_| AgentError::ProposalEncodingOverflow)?;
    out.push(tag);
    out.extend_from_slice(&length.to_le_bytes());
    out.extend_from_slice(bytes);
    Ok(())
}
