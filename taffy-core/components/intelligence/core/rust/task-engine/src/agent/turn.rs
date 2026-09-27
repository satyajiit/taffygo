// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable vocabulary of one model turn: how it ended, what it cost, and
//! the shape of the page it was built from.
//!
//! # Nothing here can carry a sentence
//!
//! Every field below is a count, a closed enumeration, or a thirty-two byte
//! digest. [`TurnDigest`] declares no `String` and no byte string, so page
//! text, node names, tool arguments and model messages have no field to travel
//! in — the exclusion is structural rather than a rule a caller is asked to
//! honour. That is decision 0052 section 5: the arena holds the text for as
//! long as a turn needs it, the journal holds the shape, and replay
//! reconstructs the conversation's structure rather than its content.
//!
//! # Why the identity is a turn ordinal and not a position
//!
//! [`call_id_for_turn`] names the *n*-th turn of one task. The delivery
//! plane paid for the general form of the opposite choice: an identifier
//! derived from an artifact's position in a plan collided the moment the
//! artifact ahead of it finished, because a re-planned batch reuses positions.
//! A turn ordinal is not a position in a batch. It only ever increases, it is
//! never re-planned, and the *n*-th turn of a task happens exactly once — so
//! the identifier names which call this is, which is what the browser's
//! effect-identity journal needs in order to refuse a second delivery of the
//! same paid call.

use bip_types::identity::TaskId;

use crate::ids::ModelCallId;

/// The identity of the `ordinal`-th model turn of `task_id`.
///
/// Pure, so a replay that reaches the same turn count derives the same
/// identity and the browser's journal recognises the call it already spent.
pub fn call_id_for_turn(task_id: &TaskId, ordinal: u64) -> ModelCallId {
    ModelCallId::new(format!("model-{}-{ordinal}", task_id.as_str()))
}

/// How one model turn ended, in the reply's own terms rather than the
/// runtime's.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ModelStopReason {
    /// The model finished its answer.
    Complete,
    /// The model asked for one or more tools.
    ToolCall,
    /// The answer was cut off at the token allowance.
    Length,
    /// The provider stopped the model for a reason of its own.
    ProviderStop,
    /// The provider reported an error.
    Error,
}

/// Why one paid sub-attempt follows the initial model request.
///
/// This is durable because a retry of the same candidate and a move to the
/// next disclosed candidate have different sequencing rules. The provider
/// choice and request body remain transient; the journal holds only the shape
/// needed to prove that accounting advanced exactly once.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ModelAttemptKind {
    /// Send the same immutable candidate request again.
    Retry,
    /// Move to the next candidate from the original route plan.
    Failover,
}

impl ModelAttemptKind {
    /// Every kind, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Retry, Self::Failover];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Retry => "retry",
            Self::Failover => "failover",
        }
    }
}

impl ModelStopReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Complete,
        Self::ToolCall,
        Self::Length,
        Self::ProviderStop,
        Self::Error,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Complete => "complete",
            Self::ToolCall => "tool_call",
            Self::Length => "length",
            Self::ProviderStop => "provider_stop",
            Self::Error => "error",
        }
    }
}

/// How a context overflow was noticed.
///
/// Only the first is an error a provider volunteered. The other two are
/// successes that are not, so which one it was is recorded rather than
/// collapsed into a flag.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TurnOverflow {
    /// The provider said so.
    ProviderReported,
    /// The input was larger than the window and nothing said so.
    Silent,
    /// The answer was cut short below the allowance that was asked for.
    Truncation,
}

impl TurnOverflow {
    /// Every kind, in declaration order.
    pub const ALL: &'static [Self] = &[Self::ProviderReported, Self::Silent, Self::Truncation];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ProviderReported => "provider_reported",
            Self::Silent => "silent",
            Self::Truncation => "truncation",
        }
    }
}

/// Why a model turn produced no readable reply.
///
/// A cancelled or unsettled turn is journalled as one of these rather than
/// left as a conversation ending on an unanswered request, because money may
/// already have been spent and the record has to say so.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TurnGap {
    /// The provider refused the request.
    Refused,
    /// The route could not be used at all.
    Unavailable,
    /// The turn was cancelled. A positive fact, not an absence.
    Cancelled,
    /// The request left and nothing came back. Money may have been spent and
    /// the ledger must say it does not know.
    OutcomeUnknown,
    /// A reply arrived and could not be read into the taxonomy.
    Unreadable,
}

impl TurnGap {
    /// Every gap, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Refused,
        Self::Unavailable,
        Self::Cancelled,
        Self::OutcomeUnknown,
        Self::Unreadable,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Refused => "refused",
            Self::Unavailable => "unavailable",
            Self::Cancelled => "cancelled",
            Self::OutcomeUnknown => "outcome_unknown",
            Self::Unreadable => "unreadable",
        }
    }
}

/// The verdict on the page projection a turn was built from.
///
/// A blank page and a page this build was not allowed to read produce the same
/// counts, and collapsing them would let one travel on as evidence that
/// nothing was there. `core-runtime`'s arena makes the same split for the same
/// reason; this is the durable spelling of it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PageReadability {
    /// Content crossed into the projection.
    Readable,
    /// The page really was blank.
    Empty,
    /// The page was not trivial and none of it crossed.
    Unreadable,
}

impl PageReadability {
    /// Every verdict, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Readable, Self::Empty, Self::Unreadable];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Readable => "readable",
            Self::Empty => "empty",
            Self::Unreadable => "unreadable",
        }
    }

    /// Whether this verdict supports the claim that the page held nothing.
    ///
    /// True for exactly one member, for the reason
    /// [`crate::tool::EmptyReason::is_evidence_of_absence`] gives.
    pub const fn is_evidence_of_absence(self) -> bool {
        matches!(self, Self::Empty)
    }
}

/// What one turn cost, in the provider's own units.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TurnUsage {
    /// Units sent.
    pub input_units: u64,
    /// Units received.
    pub output_units: u64,
    /// Units served from the provider's cache.
    pub cache_read_units: u64,
    /// Units written into the provider's cache.
    pub cache_write_units: u64,
}

/// The shape of the projection the model was shown.
///
/// The digest names the exact projection in thirty-two bytes, which replay
/// compares and nobody can read a page back out of.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RenderShape {
    /// What the page turned out to be.
    pub readability: PageReadability,
    /// Nodes that got a line and a number.
    pub offered_nodes: u32,
    /// Nodes the render budget kept out. This projection could have shown
    /// them and chose not to, which is not the same as withholding them.
    pub omitted_nodes: u32,
    /// Nodes present in the graph that no projection could show.
    pub unreadable_nodes: u32,
    /// Text those unreadable nodes declared.
    pub unreadable_text_bytes: u64,
    /// The digest of the projection itself.
    pub digest: [u8; 32],
}

impl RenderShape {
    /// A shape for a projection that showed nothing and had nothing to show.
    pub const fn empty(digest: [u8; 32]) -> Self {
        Self {
            readability: PageReadability::Empty,
            offered_nodes: 0,
            omitted_nodes: 0,
            unreadable_nodes: 0,
            unreadable_text_bytes: 0,
            digest,
        }
    }
}

/// The shape of one model turn, and never its content.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TurnDigest {
    /// How the turn ended.
    pub stop: ModelStopReason,
    /// How an overflow was noticed, when one was.
    pub overflow: Option<TurnOverflow>,
    /// What it cost.
    pub usage: TurnUsage,
    /// How many answer segments the reply carried.
    pub answer_segments: u32,
    /// How many tool calls the reply named.
    pub tool_calls: u32,
    /// How many of them the reducer refused to attempt when it read the reply.
    ///
    /// The count is taken at *reading* time, so it holds the refusals a reply
    /// carries on its face — a truncated argument list, an unregistered name,
    /// arguments that do not match the compiled-in schema, a number that names
    /// no node. A call refused later, by policy or by the browser, is not
    /// counted here: it is an action record in its own right, and the calls
    /// after it are visible as calls the turn named and never proposed.
    pub refused_tool_calls: u32,
    /// The page the turn was built from.
    pub render: RenderShape,
}

impl TurnDigest {
    /// Whether every tool call the reply named was refused on sight.
    pub const fn every_call_refused_on_sight(&self) -> bool {
        self.tool_calls > 0 && self.refused_tool_calls >= self.tool_calls
    }
}

/// Where one model turn has got to.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TurnPhase {
    /// Journalled and dispatched. No completion has come back.
    InFlight,
    /// A reply was read and its shape recorded.
    Recorded(TurnDigest),
    /// No readable reply, for a stated reason.
    Gap(TurnGap),
}

impl TurnPhase {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::InFlight => "in_flight",
            Self::Recorded(_) => "recorded",
            Self::Gap(_) => "gap",
        }
    }

    /// Whether the browser is still holding this call.
    pub const fn is_in_flight(self) -> bool {
        matches!(self, Self::InFlight)
    }
}

/// One model turn, as the reducer holds it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelTurn {
    call_id: ModelCallId,
    ordinal: u64,
    phase: TurnPhase,
    asked_person: bool,
    attempts_started: u32,
    candidate_ordinal: u32,
    last_attempt: Option<ModelAttemptKind>,
}

impl ModelTurn {
    /// A turn that has just been journalled and dispatched.
    pub const fn in_flight(call_id: ModelCallId, ordinal: u64) -> Self {
        Self {
            call_id,
            ordinal,
            phase: TurnPhase::InFlight,
            asked_person: false,
            attempts_started: 1,
            candidate_ordinal: 0,
            last_attempt: None,
        }
    }

    /// Which call this is.
    pub const fn call_id(&self) -> &ModelCallId {
        &self.call_id
    }

    /// Which turn of the task this is, counting from zero.
    pub const fn ordinal(&self) -> u64 {
        self.ordinal
    }

    /// Where it has got to.
    pub const fn phase(&self) -> TurnPhase {
        self.phase
    }

    /// Whether this turn has already reached for the person.
    ///
    /// A call that asks the person or hands the page back is the last thing a
    /// turn does: the task leaves `RUNNING`, and what comes back is an answer
    /// to a question rather than the rest of a reply. Without this flag the
    /// reducer would walk the same reply again on the way back in and ask the
    /// same question forever.
    pub const fn asked_person(&self) -> bool {
        self.asked_person
    }

    /// How many paid attempts this logical turn has durably started.
    pub const fn attempts_started(&self) -> u32 {
        self.attempts_started
    }

    /// Zero-based candidate selected by the latest paid attempt.
    pub const fn candidate_ordinal(&self) -> u32 {
        self.candidate_ordinal
    }

    /// What the latest paid attempt was, when it was not the first call: a
    /// retry of the same candidate or a move to the next one. A surface reads
    /// it to say which, while the attempt is in flight.
    pub const fn last_attempt(&self) -> Option<ModelAttemptKind> {
        self.last_attempt
    }

    pub(crate) fn start_attempt(
        &mut self,
        attempt_ordinal: u32,
        candidate_ordinal: u32,
        kind: ModelAttemptKind,
    ) -> bool {
        if !self.phase.is_in_flight() || attempt_ordinal != self.attempts_started {
            return false;
        }
        let candidate_is_next = match kind {
            ModelAttemptKind::Retry => candidate_ordinal == self.candidate_ordinal,
            ModelAttemptKind::Failover => self
                .candidate_ordinal
                .checked_add(1)
                .is_some_and(|next| candidate_ordinal == next),
        };
        let Some(attempts_started) = self.attempts_started.checked_add(1) else {
            return false;
        };
        if !candidate_is_next {
            return false;
        }
        self.attempts_started = attempts_started;
        self.candidate_ordinal = candidate_ordinal;
        self.last_attempt = Some(kind);
        true
    }

    pub(crate) fn record(&mut self, digest: TurnDigest) {
        self.phase = TurnPhase::Recorded(digest);
    }

    pub(crate) fn record_gap(&mut self, gap: TurnGap) {
        self.phase = TurnPhase::Gap(gap);
    }

    pub(crate) fn note_person_asked(&mut self) {
        self.asked_person = true;
    }
}
