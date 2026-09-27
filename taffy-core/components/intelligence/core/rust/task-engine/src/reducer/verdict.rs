// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the reducer says back: acceptance, refusal, and the two rebuild types.
//!
//! Every one of these is a *result*, never a decision. They carry no logic:
//! grouping them here keeps the reducer's answer shapes readable in one place
//! and keeps the fold itself free of the structs it fills in.

use bip_types::identity::ActionId;

use crate::command::CommandKind;
use crate::effect::Effect;
use crate::event::TaskEvent;
use crate::task::TaskState;
use crate::transition::RefusalReason;
use crate::ModelCallId;
/// What a committed command did.
#[derive(Clone, Debug, PartialEq)]
pub struct Accepted {
    /// The state before.
    pub from: TaskState,
    /// The state after, which equals `from` when the command moved nothing.
    pub to: TaskState,
    /// The aggregate revision after.
    pub revision: u64,
    /// What was journalled.
    pub events: Vec<TaskEvent>,
    /// What the runtime has to do, in order.
    pub effects: Vec<Effect>,
    /// Whether this is the original result of a command that was already
    /// applied. A duplicate performs no effect.
    pub duplicate: bool,
}

/// Why a command was not committed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Refusal {
    /// Why.
    pub reason: RefusalReason,
    /// What the task was doing at the time.
    pub state: TaskState,
    /// What was asked for.
    pub command: CommandKind,
    /// The revision the task is actually at.
    pub revision: u64,
}

/// Why a journal could not be replayed.
#[derive(Clone, Debug, PartialEq)]
pub enum ReplayError {
    /// The journal itself is not loadable.
    Journal(crate::journal::JournalError),
    /// A journalled command was refused on replay, so the journal does not
    /// describe a run this reducer could have produced.
    CommandRefused {
        /// Where in the journal.
        sequence: u64,
        /// Why it was refused now.
        refusal: Refusal,
    },
    /// The recomputed state disagrees with the state the journal recorded.
    StateDiverged {
        /// What the journal recorded.
        recorded: TaskState,
        /// What the replay reached.
        recomputed: TaskState,
    },
    /// The recomputed revision disagrees with the journal's.
    RevisionDiverged {
        /// What the journal recorded.
        recorded: u64,
        /// What the replay reached.
        recomputed: u64,
    },
}

impl ReplayError {
    /// A short, compiled-in name for why the journal did not replay.
    ///
    /// A refused command names its refusal, which is the fact a log line
    /// needs: which rule of this build the journal ran into. Nothing from the
    /// journal's content is in it, so it is safe beside the status the browser
    /// already logs (decision 0235).
    pub const fn label(&self) -> &'static str {
        match self {
            Self::Journal(_) => "replay_journal",
            Self::CommandRefused { refusal, .. } => refusal.reason.label(),
            Self::StateDiverged { .. } => "replay_state_diverged",
            Self::RevisionDiverged { .. } => "replay_revision_diverged",
        }
    }
}

/// A journalled command this build would refuse, applied because the build
/// that journalled it admitted it (decision 0235).
///
/// Only a bound on the task's own conduct is crossed this way — see
/// `Reducer::replay` for the closed list — and only while replaying. The
/// command is history: it happened, the effects it asked for were emitted by
/// the reducer that admitted it, and a replay emits none.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HistoryAdmission {
    /// Where in the journal.
    pub sequence: u64,
    /// What was applied.
    pub command: CommandKind,
    /// The refusal this build would have given it live.
    pub bound: RefusalReason,
}

/// What a rebuild concluded (domain model section 18.3).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recovery {
    /// How many journalled commands were re-applied.
    pub commands_replayed: u64,
    /// The state the task is in.
    pub state: TaskState,
    /// The aggregate revision.
    pub revision: u64,
    /// Attempts that were dispatching or verifying with no terminal event.
    /// They are now `OUTCOME_UNKNOWN` and are reconciled, never replayed.
    pub unknown_outcome_actions: Vec<ActionId>,
    /// The model call the previous generation was holding, if it was holding
    /// one. It is now a gap of `OUTCOME_UNKNOWN`: the reply had nowhere to be
    /// delivered, so it will not arrive, and a task that waits for it can
    /// never settle a pause or a stop (decision 0150).
    pub interrupted_model_call: Option<ModelCallId>,
    /// How many actor leases were restored. Always zero: a lease is
    /// process-local authority and is never recreated after process death.
    pub leases_restored: usize,
    /// Whether resuming has to revalidate browser, source, and provider state
    /// and build a new plan revision.
    pub requires_revalidation: bool,
    /// Journalled commands this build would have refused on a bound on the
    /// task's conduct, applied as the history they are (decision 0235). Empty
    /// for every journal this build wrote itself.
    pub admitted_as_history: Vec<HistoryAdmission>,
}
