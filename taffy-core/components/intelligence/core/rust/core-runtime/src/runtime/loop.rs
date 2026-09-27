// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The loop's transient state: one owner and one drop point (decision 0072).
//!
//! The map lives beside the task sessions rather than inside them, so the
//! restore path constructs a session with no loop state at all — the
//! structural proof that nothing transient survives a generation. An entry is
//! created lazily by the first access, dropped with its task, and dropped
//! wholesale when the runtime itself is torn down.

use std::collections::BTreeMap;

use loop_kernel::state::LoopState;
use task_engine::action::{ActionIntent, BrowserIntent, LibraryIntent, MemoryIntent, StoreIntent};
use task_engine::{turn_call_of, ActionProposal, WorkflowDigest, WorkflowError};

use crate::account::{DigestError, Sha256Port};
use crate::ports::TaskEnginePort;

use super::CoreRuntime;

struct TransientOperandDigest<'a>(&'a dyn Sha256Port);

impl WorkflowDigest for TransientOperandDigest<'_> {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], WorkflowError> {
        self.0
            .sha256(input)
            .map_err(|DigestError::Unavailable| WorkflowError::DigestUnavailable)
    }
}

impl CoreRuntime {
    /// The loop state for one open task, if any command has created it.
    #[must_use]
    pub fn loop_state(&self, task_id: &str) -> Option<&LoopState> {
        self.loop_states.get(task_id)
    }

    /// The loop state for one task, created empty on first use.
    pub fn loop_state_mut(&mut self, task_id: &str) -> &mut LoopState {
        self.loop_states.entry(task_id.to_owned()).or_default()
    }

    /// Resolves the live-only query bound by one exact search proposal.
    ///
    /// A restored runtime deliberately has no residency, so it returns none
    /// rather than reconstructing model-authored bytes from the durable
    /// proposal. The opaque reference is rechecked against both its derived
    /// turn/call slot and its SHA-256 digest before any bytes leave Rust.
    #[must_use]
    pub fn transient_search_query<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<&'a str> {
        let ActionIntent::Browser(BrowserIntent::Search { query, .. }) = proposal.intent() else {
            return None;
        };
        let (ordinal, sequence) = turn_call_of(&proposal.idempotency_key)?;
        let residency = self.loop_state(task_id)?.residency.as_ref()?;
        residency
            .action_operands()
            .resolve(
                ordinal,
                sequence,
                query,
                &TransientOperandDigest(self.components.digest.as_ref()),
            )
            .ok()
    }

    /// Resolves the generation-resident query bound to one Library search.
    ///
    /// The durable action retains only the opaque reference and digest. A
    /// restored service therefore refuses the read instead of inventing a
    /// query or widening it to the whole Library.
    #[must_use]
    pub fn transient_library_query<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<&'a str> {
        let ActionIntent::Library(LibraryIntent::Search { query, .. }) = proposal.intent() else {
            return None;
        };
        let (ordinal, sequence) = turn_call_of(&proposal.idempotency_key)?;
        let residency = self.loop_state(task_id)?.residency.as_ref()?;
        residency
            .action_operands()
            .resolve(
                ordinal,
                sequence,
                query,
                &TransientOperandDigest(self.components.digest.as_ref()),
            )
            .ok()
    }

    /// Resolves the generation-resident query bound to one search over a
    /// person's attached store. A listing has no query and answers none; a
    /// restored service has no residency and refuses the read rather than
    /// widening it to the whole store.
    #[must_use]
    pub fn transient_store_query<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<&'a str> {
        let ActionIntent::Store(
            StoreIntent::HistorySearch { query, .. } | StoreIntent::BookmarksSearch { query, .. },
        ) = proposal.intent()
        else {
            return None;
        };
        self.resolve_transient_operand(task_id, proposal, query)
    }

    /// Resolves the generation-resident query bound to one Memory search.
    #[must_use]
    pub fn transient_memory_query<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<&'a str> {
        let ActionIntent::Memory(MemoryIntent::Search { query, .. }) = proposal.intent() else {
            return None;
        };
        self.resolve_transient_operand(task_id, proposal, query)
    }

    /// Resolves the generation-resident statement bound to one explicitly
    /// approved Memory save or update.
    #[must_use]
    pub fn transient_memory_statement<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<&'a str> {
        let ActionIntent::Memory(intent) = proposal.intent() else {
            return None;
        };
        let reference = match intent {
            MemoryIntent::Save { statement, .. } | MemoryIntent::Update { statement, .. } => {
                statement
            }
            MemoryIntent::Search { .. } | MemoryIntent::Delete { .. } => return None,
        };
        self.resolve_transient_operand(task_id, proposal, reference)
    }

    fn resolve_transient_operand<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
        reference: &task_engine::action::OpaqueOperandRef,
    ) -> Option<&'a str> {
        let (ordinal, sequence) = turn_call_of(&proposal.idempotency_key)?;
        let residency = self.loop_state(task_id)?.residency.as_ref()?;
        residency
            .action_operands()
            .resolve(
                ordinal,
                sequence,
                reference,
                &TransientOperandDigest(self.components.digest.as_ref()),
            )
            .ok()
    }

    /// Resolves the two model-authored values bound to one registered Python
    /// operation. A restart has no turn residency and therefore returns none;
    /// it never recreates worker input from durable metadata.
    #[must_use]
    pub fn transient_python_arguments<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<(&'a str, &'a str)> {
        let ActionIntent::ToolJob(intent) = proposal.intent() else {
            return None;
        };
        let title = self.resolve_transient_operand(task_id, proposal, &intent.title)?;
        let content = self.resolve_transient_operand(task_id, proposal, &intent.content)?;
        Some((title, content))
    }

    /// Resolves the live-only text filter bound by one exact DOM query.
    ///
    /// The browser never receives these bytes. Losing the residency is a
    /// refusal, not permission to reinterpret the action as an unfiltered
    /// document read.
    #[must_use]
    pub fn transient_dom_query_text<'a>(
        &'a self,
        task_id: &str,
        proposal: &ActionProposal,
    ) -> Option<&'a str> {
        let ActionIntent::Browser(BrowserIntent::DomQuery {
            text: Some(query), ..
        }) = proposal.intent()
        else {
            return None;
        };
        let (ordinal, sequence) = turn_call_of(&proposal.idempotency_key)?;
        let residency = self.loop_state(task_id)?.residency.as_ref()?;
        residency
            .action_operands()
            .resolve(
                ordinal,
                sequence,
                query,
                &TransientOperandDigest(self.components.digest.as_ref()),
            )
            .ok()
    }

    /// A page mutation, a new lease or a learned step needs fresh evidence.
    /// Called only after the exact commit; keep the conversation and handles.
    pub(super) fn invalidate_changed_page(
        &mut self,
        task_id: &bip_types::identity::TaskId,
        events: &[task_engine::TaskEvent],
    ) {
        let changed_tab = self.tasks.get(task_id.as_str()).and_then(|session| {
            let record = session
                .task
                .journal()
                .entries()
                .iter()
                .rev()
                .find_map(task_engine::JournalEntry::as_command)?;
            let task_engine::Command::RecordActionOutcome { action_id, .. } =
                &record.envelope.command
            else {
                return None;
            };
            let action = session.task.action_effect_facts(action_id)?;
            let ActionIntent::Browser(intent) = action.proposal.intent() else {
                return None;
            };
            // Even a failed or uncertain attempt may have changed the page.
            // Only its exact committed outcome retires the transient bytes;
            // fresh observation still needs ordinary policy and verification.
            match intent {
                BrowserIntent::Navigate { new_tab: false, .. }
                | BrowserIntent::Search { .. }
                | BrowserIntent::HistoryBack { .. }
                | BrowserIntent::HistoryForward { .. }
                | BrowserIntent::Reload { .. }
                | BrowserIntent::StopLoading { .. }
                | BrowserIntent::LinkOpen { .. }
                | BrowserIntent::DomClick { .. }
                | BrowserIntent::DomFocus { .. }
                | BrowserIntent::DomScroll { .. }
                | BrowserIntent::FormFill { .. }
                | BrowserIntent::FormSelect { .. }
                | BrowserIntent::FormToggle { .. }
                | BrowserIntent::FormSubmit { .. } => Some(intent.tab_id().clone()),
                BrowserIntent::TabsActivate { target, .. }
                | BrowserIntent::TabsClose { target, .. } => Some(target.tab_id().clone()),
                BrowserIntent::Navigate { new_tab: true, .. }
                | BrowserIntent::TabsOpen { .. }
                | BrowserIntent::TabsList { .. }
                | BrowserIntent::DomQuery { .. }
                | BrowserIntent::DomRead { .. }
                | BrowserIntent::FormInspect { .. }
                | BrowserIntent::DownloadStart { .. }
                | BrowserIntent::DownloadFromLink { .. }
                | BrowserIntent::DownloadList { .. }
                | BrowserIntent::DownloadCancel { .. }
                | BrowserIntent::SelectionRead { .. }
                | BrowserIntent::ImageDescribe { .. }
                | BrowserIntent::ImageReadText { .. }
                | BrowserIntent::VideoInspect { .. }
                | BrowserIntent::PdfInspect { .. }
                | BrowserIntent::PageScreenshotInspect { .. } => None,
            }
        });
        let learned_step_changed = self.tasks.get(task_id.as_str()).is_some_and(|session| {
            session.task.skill_version_id().is_some()
                && session
                    .task
                    .journal()
                    .entries()
                    .iter()
                    .rev()
                    .find_map(task_engine::JournalEntry::as_command)
                    .is_some_and(|record| {
                        matches!(
                            record.envelope.command,
                            task_engine::Command::AdvanceStep {
                                to: task_engine::StepState::Succeeded,
                                ..
                            }
                        )
                    })
                && session
                    .task
                    .view_facts()
                    .ok()
                    .and_then(|view| view.plan_progress)
                    .is_some_and(|plan| plan.total_steps.saturating_sub(plan.finished_steps) > 1)
        }) && events
            .iter()
            .any(|event| event.kind == task_engine::EventKind::PlanStepAdvanced);
        let handover_changed = events.iter().any(|event| {
            matches!(
                event.kind,
                task_engine::EventKind::HandoverRequested
                    | task_engine::EventKind::HandoverCompleted
            )
        });
        if learned_step_changed || handover_changed {
            if let Some(state) = self.loop_states.get_mut(task_id.as_str()) {
                state.page.invalidate_observations();
            }
        } else if let Some(tab_id) = changed_tab {
            if let Some(state) = self.loop_states.get_mut(task_id.as_str()) {
                state.page.invalidate_tab_observation(&tab_id);
            }
        }
    }

    /// Drops everything transient this task held. Called where the task
    /// session itself is removed; the two lifetimes are one lifetime.
    pub(super) fn drop_loop_state(&mut self, task_id: &str) {
        self.loop_states.remove(task_id);
    }

    /// Both surfaces the walk needs, borrowed disjointly: the task's read
    /// surface and its mutable loop state.
    pub fn walk_surfaces(
        &mut self,
        task_id: &str,
    ) -> Option<(&dyn TaskEnginePort, &mut LoopState)> {
        let session = self.tasks.get(task_id)?;
        let state = self.loop_states.entry(task_id.to_owned()).or_default();
        Some((session.task.as_ref(), state))
    }

    /// The three surfaces one compose needs, borrowed disjointly: the task's
    /// read surface, its mutable loop state, and the router port.
    pub fn compose_surfaces(
        &mut self,
        task_id: &str,
    ) -> Option<(
        &dyn TaskEnginePort,
        &mut loop_kernel::state::LoopState,
        &mut dyn crate::ports::ModelRouterPort,
    )> {
        let session = self.tasks.get(task_id)?;
        let state = self.loop_states.entry(task_id.to_owned()).or_default();
        Some((
            session.task.as_ref(),
            state,
            self.components.models.as_mut(),
        ))
    }

    /// Every staged ask prompt, keyed by task id, for the status projection.
    #[must_use]
    pub fn staged_ask_prompts(&self) -> BTreeMap<String, String> {
        self.loop_states
            .iter()
            .filter_map(|(task_id, state)| {
                state
                    .ask_prompt
                    .as_ref()
                    .map(|prompt| (task_id.clone(), prompt.clone()))
            })
            .collect()
    }
}
