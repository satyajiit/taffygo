// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The transient half of a turn: the reply the model sent and the projection
//! it was answering.
//!
//! # This is residency, not state
//!
//! [`TurnResidency`] is the middle of decision 0052 section 5's three layers.
//! It lives for as long as a turn needs it and is never journalled: the
//! durable record of the same turn is a [`TurnDigest`], which holds counts and
//! closed enumerations and no prose. A process that dies mid-turn loses this
//! and keeps that, which is exactly the intended trade — the reducer can then
//! see that turn *n* was paid for and ended in tool calls, and it starts turn
//! *n+1* rather than reconstructing a reply it no longer has.
//!
//! # Nothing free-form survives the boundary
//!
//! A [`ModelToolCall`] carries the name the model wrote and the arguments it
//! parsed into. Neither is trusted: the name is resolved against
//! [`crate::tool::REGISTRY`] and the arguments against the compiled-in schema
//! of the row it resolved to, and a call that fails either is
//! [`NotAttempted`]. What comes out the other side is a proposal built from
//! the *table's* action class and idempotency class, never from anything the
//! reply said about itself (decision 0052 section 3).
//!
//! # A number is not a selector
//!
//! The model designates a node by a number this task issued. Resolving it is
//! [`crate::handle::HandleTable`]'s answer, and a number that names nothing is
//! [`NotAttempted::HandleUnknown`] rather than a near miss — see that module
//! on why a per-view ordinal would have made every precondition pass on the
//! wrong element.

mod page;
mod verdict;

pub use self::page::{CurrentDocument, PersonsPages, TurnPage};
pub use self::verdict::{CallDisposition, CallVerdict, NotAttempted};

use std::collections::BTreeMap;

use super::library_intent::TaskLibrarySearchTranscriptOutcome;
use super::media_probe::MediaProbeTranscriptOutcome;
use super::memory_intent::TaskMemorySearchTranscriptOutcome;
use super::r#loop::LoopOutcome;
use super::task_downloads::{TaskDownloadHandleTable, TaskDownloadTranscriptOutcome};
use super::task_stores::TaskStoreTranscriptOutcome;
use super::task_tabs::{TaskTabHandleTable, TaskTabTranscriptOutcome};
use super::turn::{ModelStopReason, TurnDigest, TurnOverflow, TurnUsage};
use super::ActionOperands;
use crate::ids::ModelCallId;
use crate::tool::{SuppliedArgument, MAX_NESTED_DEPTH};

/// How many tool calls one reply may carry.
///
/// One is dispatched at a time, so a reply naming more than a handful is not a
/// plan — it is a provider or a model behaving in a way nobody designed for.
/// `model-router` bounds the wire side with the same number; this is the
/// bound on what may reach the reducer, and the two are deliberately separate
/// so neither has to trust the other.
pub const MAX_TURN_TOOL_CALLS: usize = 16;

/// Resident result material one loop-local tool call may return to the next turn.
pub const MAX_LOOP_RESULT_BYTES: usize = 32 * 1024;

/// Result pieces one loop-local call may return. Table reshape uses two.
pub const MAX_LOOP_RESULT_PIECES: usize = 4;

/// One tool call the model asked for, in the order the provider declared it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelToolCall {
    /// The name the model wrote. Free-form here and closed one step later:
    /// it resolves to a compiled-in registry row or it is not attempted.
    pub tool_name: String,
    /// The arguments, already parsed into typed values at the boundary that
    /// read the reply. This crate never coerces one.
    pub arguments: Vec<SuppliedArgument>,
}

impl ModelToolCall {
    /// One call.
    pub fn new(tool_name: impl Into<String>, arguments: Vec<SuppliedArgument>) -> Self {
        Self {
            tool_name: tool_name.into(),
            arguments,
        }
    }
}

/// One reply, read into the taxonomy every decision below is made from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelReply {
    /// How the turn ended.
    pub stop: ModelStopReason,
    /// How an overflow was noticed, when one was.
    pub overflow: Option<TurnOverflow>,
    /// What it cost.
    pub usage: TurnUsage,
    /// How many answer segments the reply carried. A count and not the text:
    /// the prose belongs to the arena, and this crate never holds a byte of
    /// it.
    pub answer_segments: u32,
    /// The tool calls, in the order the provider declared them.
    pub tool_calls: Vec<ModelToolCall>,
}

impl ModelReply {
    /// A reply that carried only an answer.
    pub const fn finished(usage: TurnUsage, answer_segments: u32) -> Self {
        Self {
            stop: ModelStopReason::Complete,
            overflow: None,
            usage,
            answer_segments,
            tool_calls: Vec::new(),
        }
    }
}

/// One turn's reply and the page it was answering, held for as long as the
/// turn needs it and never journalled.
#[derive(Clone, Debug, PartialEq)]
pub struct TurnResidency {
    call_id: ModelCallId,
    page: TurnPage,
    reply: ModelReply,
    action_operands: ActionOperands,
    task_tabs: TaskTabHandleTable,
    task_downloads: TaskDownloadHandleTable,
    task_tab_outcomes: BTreeMap<u32, TaskTabTranscriptOutcome>,
    task_download_outcomes: BTreeMap<u32, TaskDownloadTranscriptOutcome>,
    library_search_outcomes: BTreeMap<u32, TaskLibrarySearchTranscriptOutcome>,
    memory_search_outcomes: BTreeMap<u32, TaskMemorySearchTranscriptOutcome>,
    store_outcomes: BTreeMap<u32, TaskStoreTranscriptOutcome>,
    media_probe_outcomes: BTreeMap<u32, MediaProbeTranscriptOutcome>,
    pub(super) loop_outcomes: BTreeMap<u32, LoopOutcome>,
    pub(super) loop_results: BTreeMap<u32, Vec<String>>,
    pub(super) activated: Vec<&'static str>,
    pub(super) nested_goal: Option<String>,
    pub(super) nested_depth: u32,
}

impl TurnResidency {
    /// The reply to `call_id`, or `None` when it names more calls than one
    /// turn may carry.
    ///
    /// Refusing at the ceiling rather than truncating: a truncated call list
    /// is the truncation row's hazard reached by a different route, and this
    /// crate would be the thing doing the truncating.
    pub fn read(call_id: ModelCallId, page: TurnPage, reply: ModelReply) -> Option<Self> {
        if reply.tool_calls.len() > MAX_TURN_TOOL_CALLS {
            return None;
        }
        let action_operands = ActionOperands::from_reply(&reply);
        Some(Self {
            call_id,
            page,
            reply,
            action_operands,
            task_tabs: TaskTabHandleTable::default(),
            task_downloads: TaskDownloadHandleTable::default(),
            task_tab_outcomes: BTreeMap::new(),
            task_download_outcomes: BTreeMap::new(),
            library_search_outcomes: BTreeMap::new(),
            memory_search_outcomes: BTreeMap::new(),
            store_outcomes: BTreeMap::new(),
            media_probe_outcomes: BTreeMap::new(),
            loop_outcomes: BTreeMap::new(),
            loop_results: BTreeMap::new(),
            activated: Vec::new(),
            nested_goal: None,
            nested_depth: 0,
        })
    }

    /// Which call this answers.
    pub const fn call_id(&self) -> &ModelCallId {
        &self.call_id
    }

    /// The page the turn was built from.
    pub const fn page(&self) -> &TurnPage {
        &self.page
    }

    /// What the reply said.
    pub const fn reply(&self) -> &ModelReply {
        &self.reply
    }

    /// Model-authored action bytes, resident only while this turn is alive.
    pub const fn action_operands(&self) -> &ActionOperands {
        &self.action_operands
    }

    /// Marks the depth of the model call whose reply this residency holds.
    ///
    /// The value is transient by design: a restored task starts a fresh turn
    /// at depth zero rather than reconstructing nested work the journal never
    /// retained. A caller cannot install a depth beyond the compiled ceiling.
    #[must_use]
    pub fn with_nested_depth(mut self, nested_depth: u32) -> Option<Self> {
        if nested_depth > MAX_NESTED_DEPTH {
            return None;
        }
        self.nested_depth = nested_depth;
        Some(self)
    }

    /// Installs the generation-resident task-tab handles that existed before
    /// this reply was read. The browser identities remain outside the reply
    /// and are never journalled.
    #[must_use]
    pub fn with_task_tabs(mut self, task_tabs: TaskTabHandleTable) -> Self {
        self.task_tabs = task_tabs;
        self
    }

    /// The task-tab numbers this reply may resolve.
    pub const fn task_tabs(&self) -> &TaskTabHandleTable {
        &self.task_tabs
    }

    /// Replaces the handle view after a verified list, activation, or close in
    /// this same reply.
    pub fn replace_task_tabs(&mut self, task_tabs: TaskTabHandleTable) {
        self.task_tabs = task_tabs;
    }

    #[must_use]
    pub fn with_task_downloads(mut self, task_downloads: TaskDownloadHandleTable) -> Self {
        self.task_downloads = task_downloads;
        self
    }

    pub const fn task_downloads(&self) -> &TaskDownloadHandleTable {
        &self.task_downloads
    }

    pub fn replace_task_downloads(&mut self, task_downloads: TaskDownloadHandleTable) {
        self.task_downloads = task_downloads;
    }

    pub fn record_task_download_outcome(
        &mut self,
        sequence: u32,
        outcome: TaskDownloadTranscriptOutcome,
    ) -> bool {
        if self
            .call(sequence)
            .is_none_or(|call| !outcome.matches_tool(&call.tool_name))
        {
            return false;
        }
        self.task_download_outcomes.insert(sequence, outcome);
        true
    }

    pub fn task_download_outcome(&self, sequence: u32) -> Option<&TaskDownloadTranscriptOutcome> {
        self.task_download_outcomes.get(&sequence)
    }

    /// Records one bounded local retrieval only against the exact Library
    /// call that authored it.
    pub fn record_library_search_outcome(
        &mut self,
        sequence: u32,
        outcome: TaskLibrarySearchTranscriptOutcome,
    ) -> bool {
        if self
            .call(sequence)
            .is_none_or(|call| call.tool_name != outcome.tool_name())
        {
            return false;
        }
        self.library_search_outcomes.insert(sequence, outcome);
        true
    }

    /// The saved-Library result while its authoring reply remains resident.
    pub fn library_search_outcome(
        &self,
        sequence: u32,
    ) -> Option<&TaskLibrarySearchTranscriptOutcome> {
        self.library_search_outcomes.get(&sequence)
    }

    /// Records one bounded Memory retrieval only against the exact call that
    /// authored it. The result remains generation-resident and is never
    /// journalled with the durable task.
    pub fn record_memory_search_outcome(
        &mut self,
        sequence: u32,
        outcome: TaskMemorySearchTranscriptOutcome,
    ) -> bool {
        if self
            .call(sequence)
            .is_none_or(|call| call.tool_name != outcome.tool_name())
        {
            return false;
        }
        self.memory_search_outcomes.insert(sequence, outcome);
        true
    }

    /// The task-safe Memory result while its authoring reply remains resident.
    pub fn memory_search_outcome(
        &self,
        sequence: u32,
    ) -> Option<&TaskMemorySearchTranscriptOutcome> {
        self.memory_search_outcomes.get(&sequence)
    }

    /// Records the bounded rows one store read answered, only against the
    /// exact call that asked. They remain generation-resident and are never
    /// journalled with the durable task.
    pub fn record_store_outcome(
        &mut self,
        sequence: u32,
        outcome: TaskStoreTranscriptOutcome,
    ) -> bool {
        if self
            .call(sequence)
            .is_none_or(|call| call.tool_name != outcome.tool_name())
        {
            return false;
        }
        self.store_outcomes.insert(sequence, outcome);
        true
    }

    /// The store rows for one call while its reply remains resident.
    pub fn store_outcome(&self, sequence: u32) -> Option<&TaskStoreTranscriptOutcome> {
        self.store_outcomes.get(&sequence)
    }

    /// Records browser-verified probe facts only beside the exact call that
    /// authored them. They remain generation-resident and are never journalled.
    pub fn record_media_probe_outcome(
        &mut self,
        sequence: u32,
        outcome: MediaProbeTranscriptOutcome,
    ) -> bool {
        if self
            .call(sequence)
            .is_none_or(|call| call.tool_name != outcome.tool_name())
        {
            return false;
        }
        self.media_probe_outcomes.insert(sequence, outcome);
        true
    }

    /// The probe result for one still-resident authoring call.
    pub fn media_probe_outcome(&self, sequence: u32) -> Option<&MediaProbeTranscriptOutcome> {
        self.media_probe_outcomes.get(&sequence)
    }

    /// Records the content-safe result for one action call while its reply is
    /// still resident. A sequence outside this frozen reply is refused.
    pub fn record_task_tab_outcome(
        &mut self,
        sequence: u32,
        outcome: TaskTabTranscriptOutcome,
    ) -> bool {
        if self
            .call(sequence)
            .is_none_or(|call| call.tool_name != outcome.tool_name())
        {
            return false;
        }
        self.task_tab_outcomes.insert(sequence, outcome);
        true
    }

    /// The verified task-tab result for one call, when still resident.
    pub fn task_tab_outcome(&self, sequence: u32) -> Option<&TaskTabTranscriptOutcome> {
        self.task_tab_outcomes.get(&sequence)
    }

    /// The call at `sequence`, when the reply named one there.
    pub fn call(&self, sequence: u32) -> Option<&ModelToolCall> {
        usize::try_from(sequence)
            .ok()
            .and_then(|index| self.reply.tool_calls.get(index))
    }

    /// How many calls the reply named.
    pub fn call_count(&self) -> u32 {
        u32::try_from(self.reply.tool_calls.len()).unwrap_or(u32::MAX)
    }

    /// The durable shape of this turn, given the dispositions the reducer
    /// reached for its calls.
    ///
    /// Built here rather than by the caller so that the counts and the
    /// verdicts cannot disagree: a digest whose `refused_tool_calls` was
    /// filled in separately would be a second description of the same reply.
    pub fn digest(&self, dispositions: &[CallDisposition]) -> TurnDigest {
        let refused = dispositions
            .iter()
            .filter(|disposition| match disposition.verdict {
                CallVerdict::Attemptable => false,
                CallVerdict::NotAttempted(reason) => reason.is_visible_on_the_face_of_the_reply(),
            })
            .count();
        TurnDigest {
            stop: self.reply.stop,
            overflow: self.reply.overflow,
            usage: self.reply.usage,
            answer_segments: self.reply.answer_segments,
            tool_calls: self.call_count(),
            refused_tool_calls: u32::try_from(refused).unwrap_or(u32::MAX),
            render: self.page.shape,
        }
    }
}
