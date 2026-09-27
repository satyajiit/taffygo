// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Putting the person's values into their fields, with no model turn between
//! the answer and the fills (decision 0238).
//!
//! # Why the task and not the model
//!
//! On 2026-09-24 an eAadhaar errand asked the person for their identity
//! number, the person typed it into the sheet, and the model was then told the
//! browser held one value and how to fill it. It never did: it read, queried,
//! opened a link, pressed, scrolled, and ended with a partial result half a
//! minute later. Everything the fill needs was already known before that turn
//! began — which request, which position, which field, which tab — and none of
//! it is a judgement. The browser minted each value for one field the sheet
//! showed, and it reports those fields back with the count, in the order the
//! values were minted. Asking a model to reproduce that list from a snapshot is
//! asking it to guess at a fact this process already holds.
//!
//! # The same fill, by the same path
//!
//! A fill here is the proposal a model's `browser.form.fill { node, value_from
//! }` becomes, byte for byte in its intent: the tab the request named, the
//! field the browser reported for that position, the request's identity and
//! the position. It leaves as an ordinary [`Command::ProposeAction`], so
//! `policy-engine` decides it, the browser's preapproval for the fields the
//! sheet showed admits it or refuses it exactly as it would the model's, and
//! the outcome comes back as the same `RecordActionOutcome`. This module
//! proposes; it grants nothing and widens nothing (invariant I-03). The intent
//! names a node and carries no reading of it, as a model's fill does: the
//! browser binds it at dispatch to the reading the person answered on or any
//! later reading of the same document (decision 0194).
//!
//! # Bounded by construction
//!
//! One proposal per held position, ever. The key names the request and the
//! position and nothing else, so a position that has any record — verified,
//! refused, failed, of unknown outcome — is never proposed again, and the
//! first one that did not verify ends the placing: the positions after it are
//! the model's to fill, with the sentence it has always been given for them.
//! No model turn is recorded, none is paid for, and the model's budget draws
//! nothing.

use bip_types::identity::{ContentDigest, DigestAlgorithm};

use super::AgentError;
use crate::action::{ActionIntent, ActionProposal, ActionRecord, ActionState, BrowserIntent};
use crate::command::Command;
use crate::field_values::{FieldValueRequestId, SuppliedFieldValues};
use crate::ids::{IdSource, IdempotencyKey};
use crate::proposal::hex_digest;
use crate::reducer::{Reducer, MAX_ACTIONS_PER_TASK};
use crate::time::Clock;
use crate::tool;
use crate::workflow::WorkflowDigest;

const FILL_KEY_PREFIX: &str = "held-value-fill-";
const FILL_DOMAIN: &[u8] = b"taffy.agent.held-value-fill.proposal.v1";
const FILL_TOOL: &str = "browser.form.fill";

/// The idempotency key of the fill that puts held value `index` of `request`
/// into its field.
///
/// Named for what the fill is about — one request and one position — and
/// never for where it landed in a batch, so a replay reaches the same record
/// and no second fill of the same position can ever be proposed.
pub fn held_value_fill_key(request: &FieldValueRequestId, index: u32) -> IdempotencyKey {
    IdempotencyKey::new(format!("{FILL_KEY_PREFIX}{}-{index}", request.as_str()))
}

/// The request and position one key names, when it names a held-value fill.
///
/// Strict, like [`super::turn_call_of`]: the position must parse whole. The
/// request identity may itself hold `-`, so the position is the last piece.
fn held_value_fill_of(key: &IdempotencyKey) -> Option<(&str, u32)> {
    let rest = key.as_str().strip_prefix(FILL_KEY_PREFIX)?;
    let (request, index) = rest.rsplit_once('-')?;
    if request.is_empty() {
        return None;
    }
    Some((request, index.parse().ok()?))
}

/// Whether this proposal is the task putting one of the person's values into
/// its field, rather than a call the model made.
///
/// The key prefix is minted by [`held_value_fill_key`] and by nothing else, so
/// this is a question about who wrote the proposal. The repetition register
/// counts the model's calls and its ladder is addressed to a model; a fill the
/// task made once and will never make again is not a call the model repeated.
pub(crate) fn is_held_value_fill(proposal: &ActionProposal) -> bool {
    held_value_fill_of(&proposal.idempotency_key).is_some()
}

/// What became of putting the person's held values into their fields.
///
/// Read by the next turn's opening, which tells the model what is already in
/// the page and what is still its to fill.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeldValuesPlaced {
    /// The task did not place them, and the model fills them as it always
    /// has: the fields are not known — the answer was rebuilt from the
    /// journal, which does not record them — or this task may not fill a
    /// field at all.
    NotPlaced,
    /// Every held value is in the field it was minted for.
    Placed {
        /// How many values that is.
        count: u32,
    },
    /// The first `placed` are in their fields and the rest are not: the fill
    /// of position `placed` was refused or failed, or the task was rebuilt
    /// before it got that far. Positions `placed..count` are the model's.
    Partly {
        /// How many leading values are in their fields.
        placed: u32,
        /// How many the person gave.
        count: u32,
    },
}

/// The next thing placing held values asks of the decision table.
pub(super) enum PlacementStep {
    /// Nothing to place, or everything settled: the model is asked next.
    Done,
    /// A fill is between proposal and outcome.
    Waiting,
    /// This fill is next.
    Propose(Command),
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// What became of putting the person's held values into their fields.
    ///
    /// Derived from the fill records, which the journal holds, so a task
    /// rebuilt after some of its fills still says which positions are placed
    /// even though it no longer knows where the rest would go.
    pub fn held_values_placed(&self) -> HeldValuesPlaced {
        let Some(supplied) = self.supplied_field_values() else {
            return HeldValuesPlaced::NotPlaced;
        };
        let count = supplied.count().get();
        if count == 0 {
            return HeldValuesPlaced::NotPlaced;
        }
        let mut placed = 0_u32;
        let mut attempted = false;
        for index in 0..count {
            let Some(action) = self.held_value_fill(supplied.request_id(), index) else {
                break;
            };
            attempted = true;
            if action.state() != ActionState::Verified {
                break;
            }
            placed += 1;
        }
        if !attempted && !self.places_held_values(supplied) {
            HeldValuesPlaced::NotPlaced
        } else if placed == count {
            HeldValuesPlaced::Placed { count }
        } else {
            HeldValuesPlaced::Partly { placed, count }
        }
    }

    /// Whether held value `index` is still the model's to fill: the person
    /// supplied it, and the task has not already put it into its field.
    ///
    /// A value is spent once. A model that fills a position the task already
    /// placed is refused by the browser, a turn later; refused here, it is
    /// told before the call leaves this process.
    pub(super) fn holds_unplaced_value(&self, index: u32) -> bool {
        self.supplied_field_values().is_some_and(|supplied| {
            supplied.contains(index)
                && self
                    .held_value_fill(supplied.request_id(), index)
                    .is_none_or(|action| action.state() != ActionState::Verified)
        })
    }

    /// Row 12a of the decision table: the next fill of a held value, before
    /// the model is asked for another turn.
    pub(super) fn next_held_value_fill(
        &self,
        digest: &dyn WorkflowDigest,
    ) -> Result<PlacementStep, AgentError> {
        let Some(supplied) = self.supplied_field_values() else {
            return Ok(PlacementStep::Done);
        };
        if !self.places_held_values(supplied) {
            return Ok(PlacementStep::Done);
        }
        let Some(placement) = supplied.placement() else {
            return Ok(PlacementStep::Done);
        };
        for index in 0..supplied.count().get() {
            match self.held_value_fill(supplied.request_id(), index) {
                None => {
                    let Some(field) = placement.fields().get(index) else {
                        return Ok(PlacementStep::Done);
                    };
                    // The register's one insert refuses past its ceiling, and
                    // a refused command is a walk that stops. Past it the
                    // model is told what is still held instead.
                    if self.action_count() >= MAX_ACTIONS_PER_TASK {
                        return Ok(PlacementStep::Done);
                    }
                    let intent = ActionIntent::Browser(BrowserIntent::FormFill {
                        tab: placement.tab_id().clone(),
                        field: field.clone(),
                        value_request: supplied.request_id().clone(),
                        value_from: index,
                    });
                    return self
                        .held_value_fill_proposal(intent, supplied.request_id(), index, digest)
                        .map(PlacementStep::Propose);
                }
                Some(action) if action.state() == ActionState::Verified => {}
                Some(action) if !action.state().is_terminal() => {
                    return Ok(PlacementStep::Waiting);
                }
                // Refused, failed, cancelled or of unknown outcome. Never
                // proposed again: the rest are the model's (decision 0238).
                Some(_) => return Ok(PlacementStep::Done),
            }
        }
        Ok(PlacementStep::Done)
    }

    /// Whether the task may place this answer's values itself: it knows the
    /// field for every one of them, and it may fill a field at all.
    fn places_held_values(&self, supplied: &SuppliedFieldValues) -> bool {
        supplied.placement().is_some_and(|placement| {
            !supplied.count().is_empty() && placement.fields().len() == supplied.count().get()
        }) && self.admits_tool(FILL_TOOL)
    }

    /// The fill record for held value `index` of `request`, if one exists.
    fn held_value_fill(&self, request: &FieldValueRequestId, index: u32) -> Option<&ActionRecord> {
        let key = held_value_fill_key(request, index);
        self.actions()
            .find(|action| action.proposal().idempotency_key == key)
    }

    fn held_value_fill_proposal(
        &self,
        intent: ActionIntent,
        request: &FieldValueRequestId,
        index: u32,
        digest: &dyn WorkflowDigest,
    ) -> Result<Command, AgentError> {
        // The classes are the registry's, exactly as a model's call has them,
        // so `Guard::ToolAvailable` sees the same proposal either way.
        let milestone = self.task().snapshot().milestone;
        let Some(entry) = tool::resolve(FILL_TOOL, milestone).entry() else {
            return Err(AgentError::ContradictoryReply);
        };
        if entry.dispatch.action_class() != Some(intent.action_class())
            || entry.idempotency != intent.idempotency()
        {
            return Err(AgentError::ContradictoryReply);
        }
        let key = held_value_fill_key(request, index);
        let material = fill_material(self.task().task_id().as_str(), &intent, &key)?;
        let encoded = hex_digest(
            &digest
                .sha256(&material)
                .map_err(|_| AgentError::DigestUnavailable)?,
        );
        Ok(Command::ProposeAction(Box::new(ActionProposal::new(
            intent,
            None,
            key,
            false,
            None,
            ContentDigest {
                algorithm: DigestAlgorithm::Sha256,
                value: encoded,
            },
        ))))
    }
}

/// The bounded canonical material one fill's digest is taken over: the task,
/// the canonical intent — which names the request and the position — and the
/// key, each length-prefixed, under a domain of its own so a fill and a
/// model's call can never share a digest.
fn fill_material(
    task_id: &str,
    intent: &ActionIntent,
    key: &IdempotencyKey,
) -> Result<Vec<u8>, AgentError> {
    let intent = intent
        .encode_canonical()
        .map_err(|_| AgentError::ProposalEncodingOverflow)?;
    let mut out = FILL_DOMAIN.to_vec();
    for field in [
        task_id.as_bytes(),
        intent.as_slice(),
        key.as_str().as_bytes(),
    ] {
        let length =
            u64::try_from(field.len()).map_err(|_| AgentError::ProposalEncodingOverflow)?;
        out.extend_from_slice(&length.to_le_bytes());
        out.extend_from_slice(field);
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::{held_value_fill_key, held_value_fill_of};
    use crate::field_values::{field_value_request_id_for_call, FieldValueRequestId};
    use crate::ids::IdempotencyKey;

    #[test]
    fn a_fill_key_names_its_request_and_position_and_reads_back() {
        let request = field_value_request_id_for_call(7, 2);
        let key = held_value_fill_key(&request, 3);
        assert_eq!(key.as_str(), "held-value-fill-turn-7-values-2-3");
        assert_eq!(held_value_fill_of(&key), Some(("turn-7-values-2", 3)));
        assert_ne!(key, held_value_fill_key(&request, 2));
    }

    #[test]
    fn a_key_from_any_other_minting_names_no_fill() {
        for key in [
            "turn-7-call-2",
            "agent-observation-abc",
            "held-value-fill-",
            "held-value-fill--1",
            "held-value-fill-request-x",
        ] {
            assert_eq!(held_value_fill_of(&IdempotencyKey::new(key)), None, "{key}");
        }
        let Ok(request) = FieldValueRequestId::new("r") else {
            unreachable!("a one-byte identity is valid")
        };
        assert!(held_value_fill_of(&held_value_fill_key(&request, 0)).is_some());
    }
}
