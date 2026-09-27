// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Action proposals and their lifecycle (domain model section 12.1 and 12.2).
//!
//! # A proposal is not authority
//!
//! Everything in this module is something the task engine wants to happen.
//! `policy-engine` decides whether it may, and the browser broker performs it.
//! The reducer records the decision it was handed; it never makes one, and
//! there is no constructor here that produces an authorized action.
//!
//! # An unknown outcome is never quietly retried
//!
//! [`ActionState::OutcomeUnknown`] is a terminal state for the attempt. What
//! may follow depends on the tool's idempotency class
//! ([`crate::tool::RecoveryRule`]), never on how convenient a retry would be.

mod intent;
mod proposal;

pub use self::intent::{
    ActionIntent, ActionIntentCodecError, BrowserIntent, DisclosureState, DomQueryRole,
    LibraryIntent, LinkHandleOriginKind, MediaOperation, MediaToolIntent, MemoryIntent,
    MemoryScopeIntent, ObservedLinkHandle, ObservedNodeHandle, OpaqueOperandKind, OpaqueOperandRef,
    PythonEntrypoint, ScrollDirection, StoreIntent, StoreKind, TaskTabTarget, ToolJobIntent,
    DEFAULT_LIBRARY_SEARCH_RESULTS, DEFAULT_MEMORY_SEARCH_RESULTS, DEFAULT_STORE_RESULTS,
    MAX_CANONICAL_ACTION_INTENT_BYTES, MAX_LIBRARY_SEARCH_RESULTS, MAX_MEMORY_SEARCH_RESULTS,
    MAX_OPAQUE_OPERAND_HANDLE_BYTES, MAX_STORE_RESULTS,
};
pub use self::proposal::ActionProposal;

use crate::authority::{ActionClass, CapabilityId, ProposalDecision};
use crate::observation::PageObservationEvidence;
use crate::task::{BrowserSessionId, ConsentedSource};
use crate::time::UtcMillis;
use bip_types::identity::{
    ActionId, ApprovalReceiptReference, ContentDigest, DispatchId, MonotonicMillis,
};
use bip_types::result_code::SideEffectCertainty;
use bip_types::ActionResultCode;

/// An action's state (domain model section 12.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ActionState {
    /// The task engine proposed it.
    Proposed,
    /// It needs the user's decision before authority exists.
    WaitingApproval,
    /// Policy issued authority for exactly this proposal.
    Authorized,
    /// The intent is journalled and the authority is being spent.
    Dispatching,
    /// The executor accepted it and the postcondition is being checked.
    Verifying,
    /// Terminal: the postcondition held.
    Verified,
    /// Terminal: it did not happen, or it happened and did not hold.
    Failed,
    /// Terminal: schema, scope, or policy refused it.
    Rejected,
    /// Terminal: authority was revoked, or the task ended, before dispatch.
    Cancelled,
    /// Terminal for the attempt: it may or may not have happened.
    OutcomeUnknown,
}

impl ActionState {
    /// Every state, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Proposed,
        Self::WaitingApproval,
        Self::Authorized,
        Self::Dispatching,
        Self::Verifying,
        Self::Verified,
        Self::Failed,
        Self::Rejected,
        Self::Cancelled,
        Self::OutcomeUnknown,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Proposed => "PROPOSED",
            Self::WaitingApproval => "WAITING_APPROVAL",
            Self::Authorized => "AUTHORIZED",
            Self::Dispatching => "DISPATCHING",
            Self::Verifying => "VERIFYING",
            Self::Verified => "VERIFIED",
            Self::Failed => "FAILED",
            Self::Rejected => "REJECTED",
            Self::Cancelled => "CANCELLED",
            Self::OutcomeUnknown => "OUTCOME_UNKNOWN",
        }
    }

    /// Whether the action has ended.
    pub const fn is_terminal(self) -> bool {
        matches!(
            self,
            Self::Verified | Self::Failed | Self::Rejected | Self::Cancelled | Self::OutcomeUnknown
        )
    }

    /// Whether the action was dispatched, or may have been.
    ///
    /// This is the question recovery asks: an attempt in one of these states
    /// without a terminal event may have reached the page.
    pub const fn may_have_reached_the_page(self) -> bool {
        matches!(self, Self::Dispatching | Self::Verifying)
    }
}

/// What an executor reported.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ActionOutcome {
    /// The protocol result code.
    pub code: ActionResultCode,
    /// The dispatch the outcome belongs to, when there was one.
    pub dispatch_id: Option<DispatchId>,
    /// When the outcome arrived, on the monotonic clock.
    ///
    /// Historical metadata only. It is never compared across a service or OS
    /// restart; freshness comes from the correlated generation, dispatch, and
    /// frozen document carried by `observation`.
    pub observed_at: MonotonicMillis,
    /// Typed evidence present only for a verified `ObservePage` action.
    pub observation: Option<PageObservationEvidence>,
    /// Exact browser-issued source reached by this verified action, when it
    /// reached a not-yet-bound tab/origin tuple.
    ///
    /// This is only a candidate carried by the terminal. The reducer admits
    /// it under the task's discovery cap in the same transition that records
    /// the outcome; presence here alone grants nothing.
    pub discovered_source: Option<ConsentedSource>,
}

/// Exact browser receipt durably attached to one visible proposal approval.
#[derive(Clone, Debug, PartialEq)]
pub struct ActionApproval {
    pub receipt: ApprovalReceiptReference,
    pub proposal_digest: ContentDigest,
    pub expires_at: MonotonicMillis,
    /// Absolute expiry retained across utility failure and checked on replay.
    pub expires_at_utc: UtcMillis,
    /// Browser-process session in which the visible approval occurred.
    pub browser_session_id: BrowserSessionId,
}

impl ActionOutcome {
    /// The state an action reaches on this outcome.
    ///
    /// Only a verified postcondition reaches [`ActionState::Verified`]. An
    /// outcome that says the effect may have happened but cannot be confirmed
    /// reaches [`ActionState::OutcomeUnknown`], never `Failed`, because
    /// "failed" would license a retry that "unknown" forbids.
    pub fn resulting_state(&self) -> ActionState {
        match self.code.side_effect() {
            SideEffectCertainty::Performed => ActionState::Verified,
            SideEffectCertainty::Possible => ActionState::OutcomeUnknown,
            SideEffectCertainty::NotPerformed => {
                if self.code.is_user_cancellation() {
                    ActionState::Cancelled
                } else if self.code.requires_user_decision() {
                    ActionState::WaitingApproval
                } else {
                    ActionState::Failed
                }
            }
        }
    }
}

/// One action, from proposal to outcome (domain model sections 12.1 and 12.2).
#[derive(Clone, Debug, PartialEq)]
pub struct ActionRecord {
    pub(crate) action_id: ActionId,
    pub(crate) proposal: ActionProposal,
    pub(crate) state: ActionState,
    pub(crate) capability_id: Option<CapabilityId>,
    pub(crate) dispatch_id: Option<DispatchId>,
    pub(crate) attempts: u32,
    pub(crate) result: Option<ActionResultCode>,
    pub(crate) approval: Option<ActionApproval>,
    pub(crate) observation: Option<PageObservationEvidence>,
    /// The tab this action names has since been landed somewhere else by a
    /// verified move, so whatever this action was about is no longer there.
    ///
    /// Set on the terminal records of a tab when a move of it verifies, and
    /// never cleared. It exists because the reducer's action register is a map
    /// keyed by identifier: `Reducer::actions()` yields `act_10` before
    /// `act_2`, so nothing downstream can tell which of two records came
    /// first. A reader that needs "since the tab last moved" cannot compute it
    /// and must be told, which is what this is. Derived, rebuilt by replay.
    ///
    /// Its one reader is the pre-model bootstrap read's bound, whose subject
    /// is one document and which was counting one tab (decision 0181).
    pub(crate) superseded_by_a_move: bool,
    /// This reading was current when a paid turn opened, so it did the job it
    /// was proposed for.
    ///
    /// Recorded for the same reason the flag above is: the bound that reads it
    /// walks a map keyed by identifier and cannot put two records in order, so
    /// "since the last reading that worked" is not something it can compute.
    /// Set on every currently verified bootstrap read when a model turn is
    /// recorded, and never cleared. Derived, rebuilt by replay (decision
    /// 0197).
    pub(crate) reading_was_used: bool,
}

impl ActionRecord {
    /// A newly proposed action.
    pub const fn proposed(action_id: ActionId, proposal: ActionProposal) -> Self {
        Self {
            action_id,
            proposal,
            state: ActionState::Proposed,
            capability_id: None,
            dispatch_id: None,
            attempts: 0,
            result: None,
            approval: None,
            observation: None,
            superseded_by_a_move: false,
            reading_was_used: false,
        }
    }

    /// Identity.
    pub const fn action_id(&self) -> &ActionId {
        &self.action_id
    }

    /// What was proposed.
    pub const fn proposal(&self) -> &ActionProposal {
        &self.proposal
    }

    /// Where the action is.
    pub const fn state(&self) -> ActionState {
        self.state
    }

    /// The capability policy issued, when it issued one.
    pub const fn capability_id(&self) -> Option<&CapabilityId> {
        self.capability_id.as_ref()
    }

    /// The dispatch, once the intent was journalled.
    pub const fn dispatch_id(&self) -> Option<&DispatchId> {
        self.dispatch_id.as_ref()
    }

    /// How many attempts have been made.
    pub const fn attempts(&self) -> u32 {
        self.attempts
    }

    /// The result code, once one arrived.
    pub const fn result(&self) -> Option<ActionResultCode> {
        self.result
    }

    /// Whether a verified move has since landed this action's tab elsewhere.
    ///
    /// See [`Self::superseded_by_a_move`] for why this is recorded rather than
    /// derived by a reader.
    pub const fn superseded_by_a_move(&self) -> bool {
        self.superseded_by_a_move
    }

    /// Whether this reading was current when a paid turn opened.
    ///
    /// See [`Self::reading_was_used`] for why this is recorded rather than
    /// derived by a reader.
    pub const fn reading_was_used(&self) -> bool {
        self.reading_was_used
    }

    /// Durable structural observation evidence, when this was a verified read.
    pub const fn observation(&self) -> Option<&PageObservationEvidence> {
        self.observation.as_ref()
    }

    /// Browser receipt recorded only after the exact visible decision.
    pub const fn approval(&self) -> Option<&ActionApproval> {
        self.approval.as_ref()
    }

    /// Freezes the receipt against the proposal digest before policy runs.
    pub fn record_approval(
        &mut self,
        receipt: ApprovalReceiptReference,
        expires_at: MonotonicMillis,
        expires_at_utc: UtcMillis,
        browser_session_id: BrowserSessionId,
    ) {
        self.approval = Some(ActionApproval {
            receipt,
            proposal_digest: self.proposal.proposal_digest.clone(),
            expires_at,
            expires_at_utc,
            browser_session_id,
        });
        self.state = ActionState::Proposed;
    }

    /// Whether the postcondition held.
    pub const fn is_verified(&self) -> bool {
        matches!(self.state, ActionState::Verified)
    }

    /// Records what policy decided.
    ///
    /// `authority_is_closed` is not advice. A task that is no longer running
    /// issues no new actions, so authority arriving after it stopped executing
    /// cancels the action whatever the decision said. The invariant is
    /// enforced here rather than trusted to a caller.
    pub fn apply_policy_decision(
        &mut self,
        decision: &ProposalDecision,
        authority_is_closed: bool,
    ) {
        if authority_is_closed {
            self.state = ActionState::Cancelled;
            return;
        }
        match decision {
            ProposalDecision::Authorize(authorization) => {
                self.capability_id = Some(authorization.capability_id.clone());
                self.state = ActionState::Authorized;
            }
            ProposalDecision::RequireApproval => self.state = ActionState::WaitingApproval,
            ProposalDecision::Deny(denial) => {
                self.result = Some(denial.code);
                self.state = ActionState::Rejected;
            }
        }
    }

    /// Records that the intent is journalled and the action is being sent.
    pub fn begin_dispatch(&mut self, dispatch_id: DispatchId) -> bool {
        if self.state != ActionState::Authorized {
            return false;
        }
        self.dispatch_id = Some(dispatch_id);
        self.attempts = self.attempts.saturating_add(1);
        self.state = ActionState::Dispatching;
        true
    }

    /// Records what came back.
    pub fn record_outcome(&mut self, outcome: &ActionOutcome) {
        self.result = Some(outcome.code);
        if let Some(dispatch_id) = outcome.dispatch_id.clone() {
            self.dispatch_id = Some(dispatch_id);
        }
        self.observation.clone_from(&outcome.observation);
        self.state = outcome.resulting_state();
    }

    /// Settles the one dispatched tool-job attempt with its terminal state.
    ///
    /// The tool-job counterpart of [`Self::record_outcome`], without a page
    /// result code: a job's durable record is the status, digest and counts
    /// its command carries, so the record here moves state and nothing else.
    /// Refused unless a dispatch is actually in flight, for the reason
    /// [`Self::accepts_outcome`] refuses: a terminal with no attempt behind
    /// it belongs to nothing.
    pub fn settle_tool_job(&mut self, state: ActionState) -> bool {
        if self.state != ActionState::Dispatching {
            return false;
        }
        self.state = state;
        true
    }

    /// Whether a terminal belongs to the one attempt currently in flight.
    pub fn accepts_outcome(&self, outcome: &ActionOutcome) -> bool {
        let in_flight = matches!(
            self.state,
            ActionState::Dispatching | ActionState::Verifying
        );
        let definitive_reconciliation = self.state == ActionState::OutcomeUnknown
            && outcome.code.is_terminal()
            && !outcome.code.has_uncertain_side_effect();
        if (!in_flight && !definitive_reconciliation)
            || outcome.dispatch_id.as_ref() != self.dispatch_id.as_ref()
        {
            return false;
        }
        if outcome.code != ActionResultCode::Verified {
            return outcome.observation.is_none();
        }
        match self.proposal.intent() {
            ActionIntent::Browser(
                BrowserIntent::TabsList { .. } | BrowserIntent::DownloadList { .. },
            ) => outcome.observation.is_none(),
            _ if self.proposal.action_class() == ActionClass::ObservePage => {
                outcome.observation.as_ref().is_some_and(|evidence| {
                    evidence.is_valid() && &evidence.tab_id == self.proposal.tab_id()
                })
            }
            _ => outcome.observation.is_none(),
        }
    }

    /// Cancels an action that has not been dispatched.
    ///
    /// An action that may have reached the page is left where it is: its effect
    /// cannot be ruled out, so calling it cancelled would be a claim the
    /// runtime cannot support.
    pub fn cancel_if_undispatched(&mut self) -> bool {
        if self.state.may_have_reached_the_page() || self.state.is_terminal() {
            return false;
        }
        self.state = ActionState::Cancelled;
        true
    }

    /// Records that an attempt that was in flight when the process died can no
    /// longer be confirmed either way (domain model section 18.3).
    ///
    /// This is the only way an action reaches [`ActionState::OutcomeUnknown`]
    /// without an executor saying so, and it is deliberately not "failed":
    /// failure would license a retry that an unconfirmed side effect forbids.
    pub fn mark_outcome_unknown(&mut self) -> bool {
        if !self.state.may_have_reached_the_page() {
            return false;
        }
        self.state = ActionState::OutcomeUnknown;
        true
    }

    /// What the runtime may do about an attempt whose outcome is unknown.
    pub const fn recovery_rule(&self) -> crate::tool::RecoveryRule {
        self.proposal.idempotency().recovery_rule()
    }
}

#[cfg(test)]
mod tests;
