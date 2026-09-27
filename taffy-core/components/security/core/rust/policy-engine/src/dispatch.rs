// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The dispatch path as a type state (protocol specification section 12,
//! steps six to ten).
//!
//! # What the borrow buys
//!
//! [`DispatchTicket`] holds `&mut` on the broker that issued it. That single
//! fact is the invariant: while a ticket exists, nothing else can reach the
//! broker, so [`crate::PolicyEngine::user_took_over`] — which needs the same
//! `&mut` — cannot run. A take-over therefore lands either strictly before the
//! ticket exists, in which case the checks refuse the dispatch, or strictly
//! after the command has been sent, in which case the capability is already in
//! flight and the take-over reports it as past recall. There is no third case
//! to get wrong, and no runtime check to forget: the window in which "dispatch
//! after revoke" could be written is a window the borrow checker rejects.
//!
//! ```compile_fail
//! # use bip_types::identity::{MonotonicMillis, TabId};
//! # use policy_engine::capability::CapabilityId;
//! # use policy_engine::engine::DispatchProposal;
//! # use policy_engine::time::SequentialIds;
//! # use policy_engine::{ObservedState, PolicyEngine, RevocationReason};
//! # fn example(
//! #     engine: &mut PolicyEngine<SequentialIds>,
//! #     capability_id: &CapabilityId,
//! #     proposal: &DispatchProposal,
//! #     observed: &ObservedState,
//! #     tab: &TabId,
//! # ) {
//! let ticket = engine.authorize_dispatch(capability_id, proposal, observed, MonotonicMillis(1));
//! // The ticket still holds the broker, so this line does not compile.
//! engine.user_took_over(tab, RevocationReason::UserTookOver);
//! drop(ticket);
//! # }
//! ```
//!
//! # What the moves buy
//!
//! Every transition consumes the value it advances. A ticket authorizes one
//! journal write, a journalled intent authorizes one send, and neither can be
//! copied, cloned, or replayed. The steps also cannot be reordered: there is no
//! method on [`DispatchTicket`] that sends anything, because section 12 records
//! the intent before the effect and the type says so.
//!
//! ```compile_fail
//! # use policy_engine::dispatch::DispatchTicket;
//! # use policy_engine::report::JournalOutcome;
//! # use policy_engine::time::SequentialIds;
//! # fn example(ticket: DispatchTicket<'_, SequentialIds>) {
//! let first = ticket.journal_intent(JournalOutcome::Recorded);
//! // The ticket was consumed by the first call.
//! let second = ticket.journal_intent(JournalOutcome::Recorded);
//! # }
//! ```
//!
//! A ticket cannot be copied either, so authority cannot be handed to two
//! places at once:
//!
//! ```compile_fail
//! # use policy_engine::dispatch::DispatchTicket;
//! # use policy_engine::time::SequentialIds;
//! # fn example(ticket: DispatchTicket<'_, SequentialIds>) {
//! let spare = ticket.clone();
//! # }
//! ```

use bip_types::identity::{MonotonicMillis, SemanticNodeId};
use bip_types::ActionResultCode;

use crate::action_class::ActionClass;
use crate::capability::{Capability, CapabilityError, CapabilityId, ConsumptionReceipt};
use crate::engine::PolicyEngine;
use crate::report::{ConsumptionOutcome, DispatchAck, JournalOutcome, PostconditionReport};
use crate::sequence::{SequenceInput, SequenceState, StaleNodeSequence};
use crate::step::StaleNodeStep;
use crate::time::IdSource;

/// The outcome of asking whether a dispatch may happen now.
#[derive(Debug)]
#[must_use = "a dispatch decision either authorizes a send or ends the action"]
pub enum DispatchDecision<'engine, I: IdSource> {
    /// The broker may journal the intent and send the command.
    Dispatch(DispatchTicket<'engine, I>),
    /// Refused, with the step that stopped it.
    Refuse {
        /// Which step of the section 12 sequence stopped it.
        step: StaleNodeStep,
        /// The result code the action ends with.
        code: ActionResultCode,
    },
}

impl<I: IdSource> DispatchDecision<'_, I> {
    /// Whether the broker may dispatch.
    pub const fn dispatches(&self) -> bool {
        matches!(self, Self::Dispatch(_))
    }

    /// The result code, for a refusal.
    pub const fn result_code(&self) -> Option<ActionResultCode> {
        match self {
            Self::Dispatch(_) => None,
            Self::Refuse { code, .. } => Some(*code),
        }
    }

    /// The step a refusal stopped at.
    pub const fn refusal_step(&self) -> Option<StaleNodeStep> {
        match self {
            Self::Dispatch(_) => None,
            Self::Refuse { step, .. } => Some(*step),
        }
    }
}

/// Authority to journal one intent and send one command.
///
/// Holds the broker. See the module documentation for what that prevents.
#[derive(Debug)]
#[must_use = "a ticket holds authority in flight until it is journalled or abandoned"]
pub struct DispatchTicket<'engine, I: IdSource> {
    engine: &'engine mut PolicyEngine<I>,
    capability_id: CapabilityId,
    action_class: ActionClass,
    node_id: Option<SemanticNodeId>,
    sequence: StaleNodeSequence,
}

impl<'engine, I: IdSource> DispatchTicket<'engine, I> {
    /// Builds a ticket for a capability the checks have just passed.
    ///
    /// Crate-private: a ticket is evidence that steps one to six ran, and
    /// evidence a caller could construct is not evidence.
    pub(crate) fn new(
        engine: &'engine mut PolicyEngine<I>,
        capability_id: CapabilityId,
        action_class: ActionClass,
        node_id: Option<SemanticNodeId>,
        sequence: StaleNodeSequence,
    ) -> Self {
        Self {
            engine,
            capability_id,
            action_class,
            node_id,
            sequence,
        }
    }

    /// The capability now in flight.
    pub const fn capability_id(&self) -> &CapabilityId {
        &self.capability_id
    }

    /// The class of effect authorized.
    pub const fn action_class(&self) -> ActionClass {
        self.action_class
    }

    /// The node the effect targets, for a node-targeted class.
    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        self.node_id.as_ref()
    }

    /// The broker's own record of the authority in flight.
    pub fn capability(&self) -> Option<&Capability> {
        self.engine.capability(&self.capability_id)
    }

    /// Step 7 — record the dispatching intent before any side effect.
    pub fn journal_intent(self, outcome: JournalOutcome) -> Journalled<'engine, I> {
        let mut sequence = self.sequence;
        sequence.step(SequenceInput::Journal(outcome));
        match outcome {
            JournalOutcome::Recorded => Journalled::Recorded(JournalledIntent {
                engine: self.engine,
                capability_id: self.capability_id,
                action_class: self.action_class,
                node_id: self.node_id,
                sequence,
            }),
            JournalOutcome::WriteFailed | JournalOutcome::NotAttempted => {
                Journalled::Settled(SettledDispatch {
                    capability_id: self.capability_id,
                    sequence,
                })
            }
        }
    }

    /// Gives the authority back without sending anything.
    ///
    /// For a broker that decided not to proceed after the checks passed. The
    /// capability is spent, because authority that was put in flight is never
    /// returned to the issued state, and the action ends as a cancellation
    /// rather than as a failure: nothing reached the page.
    pub fn abandon(self, now: MonotonicMillis) -> SequenceOutcome {
        let Self {
            engine,
            capability_id,
            mut sequence,
            ..
        } = self;
        sequence.step(SequenceInput::Journal(JournalOutcome::NotAttempted));
        SettledDispatch {
            capability_id,
            sequence,
        }
        .record(engine, now)
    }
}

/// What step 7 did.
#[derive(Debug)]
#[must_use = "the journal outcome decides whether anything may be sent"]
pub enum Journalled<'engine, I: IdSource> {
    /// The intent is durable. The command may be sent.
    Recorded(JournalledIntent<'engine, I>),
    /// The write failed, so nothing was sent and the action is settled.
    Settled(SettledDispatch),
}

/// A journalled intent: authority to send exactly one command.
#[derive(Debug)]
#[must_use = "a journalled intent holds authority in flight until it is dispatched"]
pub struct JournalledIntent<'engine, I: IdSource> {
    engine: &'engine mut PolicyEngine<I>,
    capability_id: CapabilityId,
    action_class: ActionClass,
    node_id: Option<SemanticNodeId>,
    sequence: StaleNodeSequence,
}

impl<I: IdSource> JournalledIntent<'_, I> {
    /// The capability in flight.
    pub const fn capability_id(&self) -> &CapabilityId {
        &self.capability_id
    }

    /// The broker's own record of the authority being spent.
    ///
    /// This is what a browser broker reads to build the narrower one-use
    /// renderer command: the scope names the frame, the node, and the origin,
    /// and nothing in the record is transferable authority.
    pub fn capability(&self) -> Option<&Capability> {
        self.engine.capability(&self.capability_id)
    }

    /// The class of effect authorized.
    pub const fn action_class(&self) -> ActionClass {
        self.action_class
    }

    /// The node the effect targets.
    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        self.node_id.as_ref()
    }

    /// Step 8 — the command went down the normal input path.
    ///
    /// The broker is released here. From this point a take-over can run again,
    /// and it correctly reports this capability as in flight rather than
    /// withdrawing it: the effect is past recall and has to be reconciled from
    /// what is observed.
    pub fn dispatch(self, ack: DispatchAck) -> Dispatched {
        let mut sequence = self.sequence;
        sequence.step(SequenceInput::Dispatch(ack));
        match ack {
            DispatchAck::AcceptedByExecutor => Dispatched::Accepted(DispatchedAction {
                capability_id: self.capability_id,
                sequence,
            }),
            _ => Dispatched::Settled(SettledDispatch {
                capability_id: self.capability_id,
                sequence,
            }),
        }
    }
}

/// What step 8 did.
#[derive(Debug)]
#[must_use = "a dispatched action still has to be verified and recorded"]
pub enum Dispatched {
    /// The executor accepted the command. The postconditions are checked next.
    Accepted(DispatchedAction),
    /// Nothing was accepted, and the action is settled.
    Settled(SettledDispatch),
}

/// A command the executor accepted, waiting on its postconditions.
///
/// Deliberately holds no reference to the broker: step 9 is the long wait, and
/// a take-over during it must be able to run. It cannot cause another dispatch,
/// because there is no method here that sends anything.
#[derive(Debug)]
#[must_use = "an accepted dispatch has to be observed and recorded"]
pub struct DispatchedAction {
    capability_id: CapabilityId,
    sequence: StaleNodeSequence,
}

impl DispatchedAction {
    /// The capability in flight.
    pub const fn capability_id(&self) -> &CapabilityId {
        &self.capability_id
    }

    /// Step 9 — one report from the postcondition verifier.
    ///
    /// A report where every declared effect held but only the renderer says so
    /// leaves the action here: that is a dispatch, not a verification.
    pub fn observe(self, report: PostconditionReport) -> Observation {
        let mut sequence = self.sequence;
        sequence.step(SequenceInput::Postcondition(report));
        if report == PostconditionReport::AcknowledgedOnly {
            Observation::StillWaiting(Self {
                capability_id: self.capability_id,
                sequence,
            })
        } else {
            Observation::Settled(SettledDispatch {
                capability_id: self.capability_id,
                sequence,
            })
        }
    }
}

/// What one verifier report did.
#[derive(Debug)]
#[must_use = "an observation either settles the action or keeps waiting"]
pub enum Observation {
    /// Nothing browser-owned has corroborated the effect yet.
    StillWaiting(DispatchedAction),
    /// The action is settled.
    Settled(SettledDispatch),
}

/// An action whose result is decided and not yet recorded.
#[derive(Debug)]
#[must_use = "a settled action has to be recorded so its capability is spent"]
pub struct SettledDispatch {
    capability_id: CapabilityId,
    sequence: StaleNodeSequence,
}

impl SettledDispatch {
    /// The capability to spend.
    pub const fn capability_id(&self) -> &CapabilityId {
        &self.capability_id
    }

    /// The code the action ends with, unless the ledger disagrees at step 10.
    pub fn settled_code(&self) -> Option<ActionResultCode> {
        match self.sequence.state() {
            SequenceState::AwaitingRecord { code } | SequenceState::Ended { code } => Some(code),
            _ => None,
        }
    }

    /// The step the result was decided at.
    pub fn decided_at(&self) -> StaleNodeStep {
        self.sequence.furthest_step()
    }

    /// Step 10 — record the terminal result and consume the capability.
    pub fn record<I: IdSource>(
        self,
        engine: &mut PolicyEngine<I>,
        now: MonotonicMillis,
    ) -> SequenceOutcome {
        let consumption = consume(engine, &self.capability_id, now);
        self.finish(consumption)
    }

    /// Applies a consumption result to the sequence.
    fn finish(
        mut self,
        consumption: (ConsumptionOutcome, Option<ConsumptionReceipt>),
    ) -> SequenceOutcome {
        let (outcome, receipt) = consumption;
        self.sequence.step(SequenceInput::Record(outcome));
        SequenceOutcome {
            code: self
                .sequence
                .result()
                .unwrap_or(ActionResultCode::InternalError),
            consumption: outcome,
            receipt,
            decided_at: self.sequence.furthest_step(),
            refusal_step: self.sequence.refusal_step(),
        }
    }
}

/// What the whole section 12 sequence produced.
#[derive(Clone, Debug, PartialEq)]
pub struct SequenceOutcome {
    /// The recorded terminal code.
    pub code: ActionResultCode,
    /// What the capability ledger did.
    pub consumption: ConsumptionOutcome,
    /// Evidence the authority was spent, when it was.
    pub receipt: Option<ConsumptionReceipt>,
    /// The last step the sequence reached.
    pub decided_at: StaleNodeStep,
    /// The step a refusal stopped at, when one did.
    pub refusal_step: Option<StaleNodeStep>,
}

impl SequenceOutcome {
    /// Whether the action may be reported as having happened as authorized.
    pub fn is_verified(&self) -> bool {
        self.code == ActionResultCode::Verified
    }
}

/// Spends a capability and classifies what the ledger did.
fn consume<I: IdSource>(
    engine: &mut PolicyEngine<I>,
    capability_id: &CapabilityId,
    now: MonotonicMillis,
) -> (ConsumptionOutcome, Option<ConsumptionReceipt>) {
    match engine.consume_capability(capability_id, now) {
        Ok(receipt) => (ConsumptionOutcome::Spent, Some(receipt)),
        Err(CapabilityError::AlreadyConsumed) => (ConsumptionOutcome::AlreadySpent, None),
        Err(_) => (ConsumptionOutcome::NothingToSpend, None),
    }
}
