// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Commit-gated, generation-resident Library search results.

use bip_types::identity::TaskId;
use bip_types::ActionResultCode;
use loop_kernel::state::LoopState;
use task_engine::action::{ActionIntent, LibraryIntent};
use task_engine::{call_id_for_turn, turn_call_of, Command, TaskLibrarySearchTranscriptOutcome};

use super::super::{BeginSubmit, CoreRuntime, SubmitError, TaskResultSubmission};

/// Model-visible Library retrieval installed only after the exact action
/// outcome transaction is durable.
pub(in crate::runtime) struct PendingTaskLibrarySearchSettlement {
    expected_call_id: task_engine::ModelCallId,
    sequence: u32,
    outcome: TaskLibrarySearchTranscriptOutcome,
}

impl core::fmt::Debug for PendingTaskLibrarySearchSettlement {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("PendingTaskLibrarySearchSettlement")
            .field("expected_call_id", &self.expected_call_id)
            .field("sequence", &self.sequence)
            .finish_non_exhaustive()
    }
}

impl PendingTaskLibrarySearchSettlement {
    pub(in crate::runtime) fn install(self, state: &mut LoopState) {
        let Some(residency) = state
            .residency
            .as_mut()
            .filter(|value| value.call_id() == &self.expected_call_id)
        else {
            return;
        };
        let _recorded = residency.record_library_search_outcome(self.sequence, self.outcome);
    }
}

impl CoreRuntime {
    fn prepare_task_library_search_settlement(
        &self,
        task_id: &TaskId,
        command: &task_engine::CommandEnvelope,
        outcome: TaskLibrarySearchTranscriptOutcome,
    ) -> Result<Option<PendingTaskLibrarySearchSettlement>, SubmitError> {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return Err(SubmitError::UnknownTask);
        };
        let invalid = || SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        };
        let Command::RecordActionOutcome {
            action_id,
            outcome: action_outcome,
        } = &command.command
        else {
            return Err(invalid());
        };
        if action_outcome.code != ActionResultCode::Verified {
            return Err(invalid());
        }
        let Some(facts) = session.task.action_effect_facts(action_id) else {
            return Err(invalid());
        };
        if !matches!(
            facts.proposal.intent(),
            ActionIntent::Library(LibraryIntent::Search { .. })
        ) {
            return Err(invalid());
        }
        let Some((ordinal, sequence)) = turn_call_of(&facts.proposal.idempotency_key) else {
            return Ok(None);
        };
        let expected_call_id = call_id_for_turn(session.task.task_id(), ordinal);
        let Some(residency) = self
            .loop_states
            .get(task_id.as_str())
            .and_then(|state| state.residency.as_ref())
            .filter(|value| value.call_id() == &expected_call_id)
        else {
            return Ok(None);
        };
        if residency
            .call(sequence)
            .is_none_or(|call| call.tool_name != outcome.tool_name())
        {
            return Ok(None);
        }
        Ok(Some(PendingTaskLibrarySearchSettlement {
            expected_call_id,
            sequence,
            outcome,
        }))
    }

    /// Stages a verified Library retrieval beside the exact action outcome;
    /// the content reaches residency only after browser-owned durability.
    pub fn begin_submit_with_task_library_search_result(
        &mut self,
        task_id: &TaskId,
        outcome: TaskLibrarySearchTranscriptOutcome,
        submission: TaskResultSubmission,
    ) -> Result<BeginSubmit, SubmitError> {
        let settlement =
            self.prepare_task_library_search_settlement(task_id, &submission.command, outcome)?;
        let result = self.begin_submit(
            task_id,
            submission.command,
            submission.operation_id,
            submission.deadline,
            submission.now_monotonic_millis,
            submission.now_utc_millis,
        )?;
        if matches!(result, BeginSubmit::AwaitingCommit(_)) {
            let pending = self
                .tasks
                .get_mut(task_id.as_str())
                .and_then(|session| session.pending_submit.as_mut())
                .ok_or(SubmitError::InvalidTaskResult {
                    in_memory_revision: 0,
                })?;
            pending.task_library_search = settlement;
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::identity::TabId;
    use task_engine::{
        HandleTable, ModelCallId, ModelReply, ModelStopReason, ModelToolCall, RenderShape,
        TaskLibraryCitation, TaskLibrarySearchEntry, TurnPage, TurnResidency, TurnUsage,
    };

    fn outcome() -> TaskLibrarySearchTranscriptOutcome {
        let citation =
            TaskLibraryCitation::new("Independent test".to_owned(), "example.test".to_owned())
                .unwrap_or_else(|| unreachable!("valid citation"));
        let entry = TaskLibrarySearchEntry::new(
            "entry-1".to_owned(),
            "Laptop research".to_owned(),
            "battery life".to_owned(),
            "twelve hours".to_owned(),
            vec![citation],
            250,
            false,
        )
        .unwrap_or_else(|| unreachable!("valid saved fact"));
        TaskLibrarySearchTranscriptOutcome::bounded(vec![entry])
    }

    fn residency(call_id: &str) -> TurnResidency {
        let reply = ModelReply {
            stop: ModelStopReason::ToolCall,
            overflow: None,
            usage: TurnUsage::default(),
            answer_segments: 0,
            tool_calls: vec![ModelToolCall::new("library.search", Vec::new())],
        };
        let page = TurnPage::new(
            TabId::new("tab-1"),
            HandleTable::new(),
            RenderShape::empty([0_u8; 32]),
        );
        TurnResidency::read(ModelCallId::new(call_id), page, reply)
            .unwrap_or_else(|| unreachable!("bounded reply"))
    }

    #[test]
    fn a_committed_search_installs_only_into_its_exact_live_reply() {
        let settlement = PendingTaskLibrarySearchSettlement {
            expected_call_id: ModelCallId::new("model-task-1-4"),
            sequence: 0,
            outcome: outcome(),
        };
        let mut exact = LoopState {
            residency: Some(residency("model-task-1-4")),
            ..LoopState::default()
        };

        settlement.install(&mut exact);

        assert!(exact
            .residency
            .as_ref()
            .and_then(|value| value.library_search_outcome(0))
            .is_some());
    }

    #[test]
    fn a_late_search_commit_cannot_enter_another_reply() {
        let settlement = PendingTaskLibrarySearchSettlement {
            expected_call_id: ModelCallId::new("model-task-1-4"),
            sequence: 0,
            outcome: outcome(),
        };
        let mut later = LoopState {
            residency: Some(residency("model-task-1-5")),
            ..LoopState::default()
        };

        settlement.install(&mut later);

        assert!(later
            .residency
            .as_ref()
            .and_then(|value| value.library_search_outcome(0))
            .is_none());
    }
}
