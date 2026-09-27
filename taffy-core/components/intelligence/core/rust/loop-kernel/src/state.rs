// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Everything the loop keeps between commands, in one place with one drop.
//!
//! Decision 0072: transient loop state has exactly one owner and dies with
//! its generation. There is deliberately no port for it — a port claims
//! substitutability, and the property protected here is that a restore
//! constructs this empty. The journal beneath holds shapes, never content;
//! nothing in this struct may survive into it.

use core::fmt;

use task_engine::{
    ModelCallId, TaskDownloadHandleTable, TaskTabHandleTable, TurnGap, TurnResidency,
};

use crate::context::{LivePage, RecentTurns};
use crate::turn::model_turn::{ComposedModelTurn, ModelReplyStream, ModelTurnError};

/// One call whose turn could not be composed, held until it is recorded.
///
/// It is held rather than discarded because the reducer has already charged
/// the budget and put the turn in flight: dropping the refusal would leave a
/// task waiting for an answer to a request that was never made, forever.
#[derive(Clone, Debug)]
pub struct RefusedModelCall {
    pub call_id: ModelCallId,
    pub gap: TurnGap,
}

impl RefusedModelCall {
    /// Names the durable fact a composition failure becomes.
    ///
    /// Every member reaches the same gap and the match is spelled out anyway,
    /// so a member added to `ModelTurnError` has to be considered here rather
    /// than inheriting a verdict from a catch-all. `Unavailable` is "the route
    /// could not be used at all", and that is exactly what each of these is:
    /// nothing was sent, no provider saw a request, and no money was spent.
    #[must_use]
    pub const fn new(call_id: ModelCallId, error: &ModelTurnError) -> Self {
        let gap = match error {
            ModelTurnError::NoDisclosedRoute
            | ModelTurnError::NoModelRoute
            | ModelTurnError::Route(_)
            | ModelTurnError::CredentialMissing
            | ModelTurnError::Body(_)
            | ModelTurnError::ManagedBody(_)
            | ModelTurnError::EndpointUnusable
            | ModelTurnError::ToolSchemaUnavailable
            | ModelTurnError::OwnEndpointUnusable
            | ModelTurnError::DigestUnavailable
            | ModelTurnError::SubattemptPlanLost
            | ModelTurnError::Overflow => TurnGap::Unavailable,
        };
        Self { call_id, gap }
    }
}

/// What the provider's last definitive answer was, once the retry ladder
/// stopped on it.
///
/// The durable gap says only that no reply came ([`TurnGap`] is in the
/// journal and is not widened for this); the class is what lets the walk turn
/// that gap into the state the person can act on — a refused key fails the
/// task for good, a limit or a lost network pauses it to be resumed. It is
/// transient on purpose: a restore constructs it absent, and a task that dies
/// between the gap and the state it becomes ends under the plain provider
/// failure, which is what it did before the class existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProviderGapClass {
    /// The provider refused the key or the request itself; asking again with
    /// the same set-up would be refused again.
    Refused,
    /// The person's own allowance at the provider was reached — credits,
    /// billing state, or a rate limit on their plan. Later may differ, and so
    /// may a different model.
    ///
    /// Narrowed from "quota or capacity" by decision 0219. Capacity is the
    /// provider's problem and is [`Self::Busy`]; saying the person's limit was
    /// reached when it was not sends them to check an account that is fine.
    Limit,
    /// The provider could not serve the request right now — its own capacity,
    /// not the person's allowance. Resuming in a moment may simply work.
    Busy,
    /// The request never reached the provider.
    Offline,
}

/// One dispatched model call, held from composition until its terminal.
///
/// The plan is retained rather than recomposed, because recomposing would ask
/// the router to choose again — and a second choice is a second plan, made
/// against a ledger and a device state that have both moved. Reading a reply
/// is a comparison against the request that was actually sent, and none of
/// what that needs survives anywhere else.
pub struct HeldModelTurn {
    pub call_id: ModelCallId,
    pub turn: Box<ComposedModelTurn>,
    /// Zero-based attempt within the current candidate.
    pub candidate_attempt: u32,
    /// Zero-based paid attempt across the whole logical turn.
    pub attempt_ordinal: u32,
    /// Zero-based candidate selected by the latest paid attempt.
    pub candidate_ordinal: u32,
    /// Incremental provider reader for this exact plan. `None` is a local
    /// attribution failure, never permission to parse the response elsewhere.
    pub stream: Option<ModelReplyStream>,
    /// What the model has said so far on this call, as the stream lent it,
    /// kept to [`crate::context::MAX_RECENT_SAID_BYTES`] for
    /// [`LoopState::recent_turns`]. Never logged and never durable.
    pub said: String,
}

impl fmt::Debug for HeldModelTurn {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("HeldModelTurn")
            .field("call_id", &self.call_id)
            .field("candidate_attempt", &self.candidate_attempt)
            .field("attempt_ordinal", &self.attempt_ordinal)
            .field("candidate_ordinal", &self.candidate_ordinal)
            .finish_non_exhaustive()
    }
}

/// The whole of what the loop holds for one task between durable commands.
///
/// Each field dies with the profile generation and is reconstructed by the
/// loop's own progress, never by a restore: a restore constructs this with
/// [`Default::default`], which is the structural proof that nothing transient
/// survives a process death. A residency carries what a model said; keeping
/// it out of the durable aggregate is what keeps prose out of the journal.
#[derive(Default)]
pub struct LoopState {
    /// Safe step descriptions from this generation's fresh task only.
    pub recording: crate::recording::FlowRecording,
    /// The last observation of this task, replaced by the next one.
    pub page: LivePage,
    /// A classified person-answer line, consumed by the next compose.
    pub person_answer: Option<String>,
    /// The classified ask subject, shown while the task waits on the person.
    pub ask_prompt: Option<String>,
    /// Loop-local tools activated for this task, append-only in activation
    /// order so the composed prompt prefix stays byte-stable.
    pub activated: Vec<&'static str>,
    /// The nested-pass goal from `run.spawn`, if one is underway.
    pub nested_goal: Option<String>,
    /// Short assistant-owned tab handles for this task and this service
    /// generation. A restore constructs the table empty.
    pub task_tabs: TaskTabHandleTable,
    /// Short content-free download handles for this manager incarnation.
    pub task_downloads: TaskDownloadHandleTable,
    /// The last read reply, held for as long as the turn needs it.
    pub residency: Option<TurnResidency>,
    /// The arguments and words of this generation's last few readings, so a
    /// compose can replay more than the resident turn in full.
    pub recent_turns: RecentTurns,
    /// A call the reducer asked for and no plan could be built for.
    pub refused: Option<RefusedModelCall>,
    /// The provider's answer behind the last recorded gap, consumed by the
    /// next scheduler pass so the failure it names can be told apart.
    pub gap_class: Option<ProviderGapClass>,
    /// The browser answered this task's discovery bootstrap with a definite
    /// refusal that the task has not yet been told about. The walk consumes it
    /// once the task is running, the first state in which the reducer admits
    /// the failure that records it; a restore constructs it clear, and the
    /// restored bootstrap effect is then asked again.
    pub discovery_unavailable: bool,
    /// The plan for the call the browser is holding, until its terminal.
    pub held: Option<HeldModelTurn>,
}

impl fmt::Debug for LoopState {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("LoopState")
            .field("activated", &self.activated)
            .field("refused", &self.refused)
            .finish_non_exhaustive()
    }
}
