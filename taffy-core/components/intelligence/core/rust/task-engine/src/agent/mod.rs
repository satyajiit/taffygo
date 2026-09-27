// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The assistant loop, as a pure reducer step (decision 0052).
//!
//! # What "pure" buys, and why nothing else buys it
//!
//! [`Reducer::next_agent_command`][crate::Reducer::next_agent_command] takes
//! `&self`. It mints nothing, applies nothing, reads no clock, draws no random
//! value and performs no work: it reads the task's durable state and the
//! turn's transient residency and answers with the next command, or with
//! nothing. Applying that command is a separate, total function, and the
//! applied command is what the journal records.
//!
//! Three properties follow, and each of them is otherwise unreachable:
//!
//! - **A restart resumes rather than repeats.** The journal holds the commands
//!   that were applied; replaying them reconstructs the state, and this
//!   function then answers the same question it would have answered before the
//!   crash.
//! - **A paid call cannot be made twice.** The browser journals an effect
//!   identity before dispatching and refuses one it has already journalled.
//!   That is only worth something if the thing proposing effects is
//!   deterministic given the same state — a loop that consulted a clock or a
//!   socket would propose a *different* identity after a restart and the
//!   ledger would have nothing to refuse.
//! - **An audit record is a derivation rather than a narration.** Every
//!   decision below is a command with a recorded cause.
//!
//! # The three layers, and only one of them is durable
//!
//! | Layer | Holds | Lives for |
//! |---|---|---|
//! | The arena, in `core-runtime` | rendered page text | as long as a turn needs it |
//! | [`TurnResidency`] | one reply and the projection it answered | the same |
//! | The journal | [`TurnDigest`]: counts, closed enumerations, one render digest | the profile |
//!
//! A model call needs the text. A durable record of what happened does not.
//! So a process that dies mid-turn loses the residency and keeps the digest,
//! and the reducer's answer to "turn *n* was paid for and named three tools,
//! and I no longer have the reply" is to start turn *n + 1* — never to
//! reconstruct a reply it does not have, and never to re-issue the call it
//! already paid for.
//!
//! # One model-requested tool call at a time
//!
//! Four independent reasons, any one of which is sufficient (decision 0052
//! section 4). The decisive one is that `ActorLeaseRegistry` grants one
//! mutating lease per tab and preemption is synchronous: a batch would hold
//! the lease across dispatches, and a preemption in the middle of one would
//! leave no record of where it stopped. The others are that each proposal
//! needs its own policy decision and a denial of the second must be visible
//! before the third is proposed; that prepare and commit are per-effect; and
//! that a per-state effect ceiling would refuse the whole transition rather
//! than the excess call.
//!
//! The deterministic observation prerequisite has one narrow exception:
//! at most four consented source tabs may be read independently before a
//! paid turn. Each read uses its own ordinary proposal and policy decision;
//! the model sees the sources in stable source order only after every read
//! has matching live bytes and a durable verification receipt.
//!
//! # This module has no authority, and adds none
//!
//! A model reaching the product changes *who suggests an action*. It changes
//! nothing about who is allowed to authorize one. Every call that survives
//! this module leaves as an ordinary [`crate::Command::ProposeAction`], whose
//! action class and idempotency class are read from
//! [`crate::tool::REGISTRY`] and never from anything the reply said about
//! itself, and `policy-engine` decides it exactly as it decides every other
//! proposal.

mod ask_view;
mod browser_intent;
mod dispatch;
mod held_values;
mod library_intent;
mod r#loop;
mod media_probe;
mod memory_intent;
mod observation;
mod operands;
mod proposal;
mod reply;
mod store_intent;
mod table;
mod target;
mod task_downloads;
mod task_stores;
mod task_tabs;
mod turn;

pub use self::ask_view::ask_view_key;
pub(crate) use self::ask_view::is_ask_view;
pub use self::dispatch::{artifact_id_for_call, turn_call_of};
pub(crate) use self::held_values::is_held_value_fill;
pub use self::held_values::{held_value_fill_key, HeldValuesPlaced};
pub use self::library_intent::{
    TaskLibraryCitation, TaskLibrarySearchEntry, TaskLibrarySearchTranscriptOutcome,
    MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES,
};
pub use self::media_probe::{
    MediaProbeResultError, MediaProbeTranscriptOutcome, MAX_MEDIA_PROBE_TRANSCRIPT_RESULT_BYTES,
};
pub use self::memory_intent::{
    TaskMemorySearchEntry, TaskMemorySearchTranscriptOutcome, MAX_MEMORY_TRANSCRIPT_RESULT_BYTES,
};
pub(crate) use self::observation::{is_agent_observation, lands_the_tab_somewhere_new};
pub use self::observation::{
    LiveSourceObservation, PreModelObservation, MAX_PARALLEL_SOURCE_READS,
    MAX_SOURCE_BOOTSTRAP_READS,
};
pub use self::operands::{ActionOperands, OperandResolveError};
pub use self::r#loop::{loop_tool_result, LoopOutcome};
pub use self::reply::{
    CallDisposition, CallVerdict, CurrentDocument, ModelReply, ModelToolCall, NotAttempted,
    PersonsPages, TurnPage, TurnResidency, MAX_LOOP_RESULT_BYTES, MAX_LOOP_RESULT_PIECES,
    MAX_TURN_TOOL_CALLS,
};
pub use self::task_downloads::{
    TaskDownloadActionResult, TaskDownloadDirectoryClass, TaskDownloadHandleTable,
    TaskDownloadMediaType, TaskDownloadResultError, TaskDownloadSnapshot, TaskDownloadState,
    TaskDownloadTranscriptEntry, TaskDownloadTranscriptOutcome, MAX_TASK_DOWNLOAD_RESULTS,
};
pub use self::task_stores::{
    TaskStoreActionResult, TaskStoreResultError, TaskStoreRow, TaskStoreTranscriptOutcome,
    MAX_STORE_ROW_FIELD_BYTES, MAX_STORE_TRANSCRIPT_RESULT_BYTES, MAX_TASK_STORE_RESULTS,
    STORE_TOOLS,
};
pub use self::task_tabs::{
    TaskTabActionResult, TaskTabHandleTable, TaskTabResultError, TaskTabSnapshot,
    TaskTabTranscriptEntry, TaskTabTranscriptOutcome, MAX_TASK_TAB_RESULTS,
};

const MAX_TASK_LIBRARY_SEARCH_RESULTS: usize = crate::action::MAX_LIBRARY_SEARCH_RESULTS as usize;
const MAX_TASK_MEMORY_SEARCH_RESULTS: usize = crate::action::MAX_MEMORY_SEARCH_RESULTS as usize;
pub use self::turn::{
    call_id_for_turn, ModelAttemptKind, ModelStopReason, ModelTurn, PageReadability, RenderShape,
    TurnDigest, TurnGap, TurnOverflow, TurnPhase, TurnUsage,
};

/// Whether `proposal` is one of the task's own moves rather than a model's
/// call: the walk's whole-document read (decision 0196), its fill of a value
/// the person supplied (decision 0238), or its scroll of a challenge into view
/// before an ask (decision 0240). Each is bounded by its own key, so none is
/// the model repeating anything.
pub(crate) fn is_task_move(proposal: &crate::action::ActionProposal) -> bool {
    is_agent_observation(proposal) || is_held_value_fill(proposal) || is_ask_view(proposal)
}

/// Why the loop could not truthfully answer.
///
/// Every member is a refusal to guess. None of them is a task failure: the
/// caller decides what to do about a residency that does not belong to the
/// turn in flight, and this module will not pick the nearest plausible
/// meaning for it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AgentError {
    /// The local digest adapter was unavailable, so no proposal can be bound
    /// to its own material.
    DigestUnavailable,
    /// The residency answers a different call than the one this task is
    /// waiting on. Reading it anyway would attribute one turn's reply to
    /// another turn's request.
    ResidencyMismatch,
    /// The reply's stop reason and its call list contradict each other — a
    /// finished answer that carries tool calls, or a tool-call ending that
    /// carries none. Neither is a shape a conforming reader produces, and
    /// choosing which half to believe would be inventing a reply.
    ContradictoryReply,
    /// The bounded canonical proposal material overflowed.
    ProposalEncodingOverflow,
    /// The registry resolved the call and then would not name it. Unreachable
    /// while `ToolEntry::canonical_name` and `ToolEntry::matches` agree, and
    /// present so that the one field a model's own string used to reach —
    /// `ActionProposal::tool_name`, which is journalled — has no path that
    /// falls back to that string when they ever disagree.
    UnnameableTool,
    /// The agent route did not retain one exact consented live source that a
    /// whole-document observation can be bound to.
    InvalidObservationSource,
}

impl AgentError {
    /// Every error, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::DigestUnavailable,
        Self::ResidencyMismatch,
        Self::ContradictoryReply,
        Self::ProposalEncodingOverflow,
        Self::UnnameableTool,
        Self::InvalidObservationSource,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::DigestUnavailable => "digest_unavailable",
            Self::ResidencyMismatch => "residency_mismatch",
            Self::ContradictoryReply => "contradictory_reply",
            Self::ProposalEncodingOverflow => "proposal_encoding_overflow",
            Self::UnnameableTool => "unnameable_tool",
            Self::InvalidObservationSource => "invalid_observation_source",
        }
    }
}
