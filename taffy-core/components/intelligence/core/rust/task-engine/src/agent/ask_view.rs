// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bringing a challenge into view before the sheet that asks for its answer
//! (decision 0240).
//!
//! # Why the task scrolls
//!
//! On 2026-09-24 an ask naming the identity number on the myAadhaar form
//! carried the CAPTCHA's answer as a companion (decision 0238). The browser
//! copies a challenge's picture from the part of the page in view, and the
//! CAPTCHA was below it, so the sheet asked for the identity number alone and
//! the person would have been asked a second time. Decision 0215 already says
//! how a picture gets into view: the task scrolls it there as it scrolls
//! anything else, through policy, and the browser never scrolls on its own
//! authority. What was missing is that nobody made that scroll before the ask.
//! The task knows which line is the challenge's answer, because the reading
//! classified it, so the scroll is not a judgement either.
//!
//! # The same scroll, by the same path
//!
//! The scroll is the proposal a model's
//! `browser.dom.scroll { node, direction: "to_node" }` becomes. It leaves as an
//! ordinary [`Command::ProposeAction`] that `policy-engine` decides, and comes
//! back as the same `RecordActionOutcome`. This module proposes; it grants
//! nothing and widens nothing (invariant I-03).
//!
//! # Bounded by construction
//!
//! At most one scroll per ask, ever, under a key that names the turn and the
//! call. Once it has any settled record — in view, refused or failed — the ask
//! goes out, and the sheet leaves off what it still cannot show, as it did
//! before. No model turn is recorded and the model's budget draws nothing.

use bip_types::identity::{ContentDigest, DigestAlgorithm};

use super::reply::TurnResidency;
use super::target::named_number;
use super::AgentError;
use crate::action::{ActionIntent, ActionProposal, BrowserIntent, ScrollDirection};
use crate::command::Command;
use crate::ids::{IdSource, IdempotencyKey};
use crate::proposal::hex_digest;
use crate::reducer::{Reducer, MAX_ACTIONS_PER_TASK};
use crate::time::Clock;
use crate::tool;
use crate::workflow::WorkflowDigest;

const VIEW_KEY_PREFIX: &str = "ask-view-";
const VIEW_DOMAIN: &[u8] = b"taffy.agent.ask-view.proposal.v1";
const SCROLL_TOOL: &str = "browser.dom.scroll";
const ASK_TOOL: &str = "user.request_values";

/// The idempotency key of the scroll that brings the challenge of call
/// `sequence` of turn `ordinal` into view.
///
/// Named for the ask it serves and nothing else, so a replay reaches the same
/// record and no ask is ever preceded by two scrolls.
pub fn ask_view_key(ordinal: u64, sequence: u32) -> IdempotencyKey {
    IdempotencyKey::new(format!("{VIEW_KEY_PREFIX}{ordinal}-{sequence}"))
}

/// The turn and call one key names, when it names an ask's scroll. Strict:
/// both numbers must parse whole.
fn ask_view_of(key: &IdempotencyKey) -> Option<(u64, u32)> {
    let rest = key.as_str().strip_prefix(VIEW_KEY_PREFIX)?;
    let (ordinal, sequence) = rest.split_once('-')?;
    Some((ordinal.parse().ok()?, sequence.parse().ok()?))
}

/// Whether this proposal is the task bringing a challenge into view before an
/// ask, rather than a call the model made.
///
/// The key prefix is minted by [`ask_view_key`] and by nothing else. The
/// repetition register counts the model's calls; a scroll the task made once
/// for one ask is not a call the model repeated.
pub(crate) fn is_ask_view(proposal: &ActionProposal) -> bool {
    ask_view_of(&proposal.idempotency_key).is_some()
}

/// The next thing bringing a challenge into view asks of the walk.
pub(super) enum ViewStep {
    /// Nothing to bring into view, or it has settled: the ask goes out.
    Ask,
    /// The scroll is between proposal and outcome.
    Waiting,
    /// This scroll is next.
    Propose(Command),
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Before call `sequence` of this turn becomes a request for values: the
    /// scroll that brings the challenge it asks about into view, if there is
    /// one and it has not been made.
    pub(super) fn next_ask_view(
        &self,
        ordinal: u64,
        residency: &TurnResidency,
        sequence: u32,
        digest: &dyn WorkflowDigest,
    ) -> Result<ViewStep, AgentError> {
        let Some(call) = residency.call(sequence) else {
            return Ok(ViewStep::Ask);
        };
        if call.tool_name != ASK_TOOL || !self.admits_tool(SCROLL_TOOL) {
            return Ok(ViewStep::Ask);
        }
        let key = ask_view_key(ordinal, sequence);
        if let Some(action) = self
            .actions()
            .find(|action| action.proposal().idempotency_key == key)
        {
            return Ok(if action.state().is_terminal() {
                ViewStep::Ask
            } else {
                ViewStep::Waiting
            });
        }
        let milestone = self.task().snapshot().milestone;
        let Some(entry) = tool::resolve(ASK_TOOL, milestone).entry() else {
            return Ok(ViewStep::Ask);
        };
        let Some(value) = named_number(entry, call) else {
            return Ok(ViewStep::Ask);
        };
        let handles = residency.page().handles();
        // The same companions the ask will carry, so the scroll is for a line
        // the sheet will be asked to show and for no other.
        let companions = handles.person_only_companions(
            value,
            crate::field_values::MAX_REQUESTED_FIELDS as usize - 1,
        );
        let Some(challenge) = handles.challenge_answer_among(value, &companions) else {
            return Ok(ViewStep::Ask);
        };
        // The register's one insert refuses past its ceiling; past it the ask
        // goes out as it did before, and the sheet shows what it can.
        if self.action_count() >= MAX_ACTIONS_PER_TASK {
            return Ok(ViewStep::Ask);
        }
        let intent = ActionIntent::Browser(BrowserIntent::DomScroll {
            tab: challenge.tab_id.clone(),
            direction: ScrollDirection::ToNode,
            target: Some(challenge.node_id.clone()),
        });
        self.ask_view_proposal(intent, key, digest)
            .map(ViewStep::Propose)
    }

    fn ask_view_proposal(
        &self,
        intent: ActionIntent,
        key: IdempotencyKey,
        digest: &dyn WorkflowDigest,
    ) -> Result<Command, AgentError> {
        // The classes are the registry's, exactly as a model's call has them,
        // so `Guard::ToolAvailable` sees the same proposal either way.
        let milestone = self.task().snapshot().milestone;
        let Some(entry) = tool::resolve(SCROLL_TOOL, milestone).entry() else {
            return Err(AgentError::ContradictoryReply);
        };
        if entry.dispatch.action_class() != Some(intent.action_class())
            || entry.idempotency != intent.idempotency()
        {
            return Err(AgentError::ContradictoryReply);
        }
        let material = view_material(self.task().task_id().as_str(), &intent, &key)?;
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

/// The bounded canonical material one scroll's digest is taken over: the
/// task, the canonical intent and the key, each length-prefixed, under a
/// domain of its own so this scroll and a model's can never share a digest.
fn view_material(
    task_id: &str,
    intent: &ActionIntent,
    key: &IdempotencyKey,
) -> Result<Vec<u8>, AgentError> {
    let intent = intent
        .encode_canonical()
        .map_err(|_| AgentError::ProposalEncodingOverflow)?;
    let mut out = VIEW_DOMAIN.to_vec();
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
    use super::{ask_view_key, ask_view_of};
    use crate::ids::IdempotencyKey;

    #[test]
    fn a_view_key_names_its_turn_and_call_and_reads_back() {
        let key = ask_view_key(7, 2);
        assert_eq!(key.as_str(), "ask-view-7-2");
        assert_eq!(ask_view_of(&key), Some((7, 2)));
    }

    #[test]
    fn a_key_from_any_other_minting_names_no_view() {
        for key in [
            "turn-7-call-2",
            "held-value-fill-turn-7-values-2-0",
            "agent-observation-abc",
            "ask-view-",
            "ask-view-7",
            "ask-view-7-",
            "ask-view-x-2",
        ] {
            assert_eq!(ask_view_of(&IdempotencyKey::new(key)), None, "{key}");
        }
    }
}
