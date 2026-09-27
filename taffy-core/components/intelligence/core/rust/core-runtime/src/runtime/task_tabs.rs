// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Commit-gated installation of browser-verified task-tab results.
//!
//! Durable action outcomes remain the reducer's record. This module owns the
//! deliberately transient companion: short handles and content-safe result
//! words are installed only after the outcome's storage transaction commits,
//! and only while the exact model reply that issued the action is resident.

use bip_types::ActionResultCode;
use loop_kernel::state::LoopState;
use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{
    call_id_for_turn, turn_call_of, Command, CommandEnvelope, TaskTabActionResult,
    TaskTabHandleTable, TaskTabTranscriptOutcome,
};

use super::{BeginSubmit, CoreRuntime, SubmitError, TaskResultSubmission};

pub(super) struct PendingTaskTabSettlement {
    expected_call_id: task_engine::ModelCallId,
    sequence: u32,
    handles: TaskTabHandleTable,
    outcome: TaskTabTranscriptOutcome,
}

impl core::fmt::Debug for PendingTaskTabSettlement {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("PendingTaskTabSettlement")
            .field("expected_call_id", &self.expected_call_id)
            .field("sequence", &self.sequence)
            .finish_non_exhaustive()
    }
}

impl PendingTaskTabSettlement {
    pub(super) fn install(self, state: &mut LoopState) {
        let Some(residency) = state
            .residency
            .as_mut()
            .filter(|value| value.call_id() == &self.expected_call_id)
        else {
            return;
        };
        residency.replace_task_tabs(self.handles.clone());
        let _recorded = residency.record_task_tab_outcome(self.sequence, self.outcome);
        state.task_tabs = self.handles;
    }
}

fn result_matches_intent(intent: &ActionIntent, result: &TaskTabActionResult) -> bool {
    match (intent, result) {
        (
            ActionIntent::Browser(BrowserIntent::TabsList {
                browser_session_id, ..
            }),
            TaskTabActionResult::Listed {
                browser_session_id: result_session,
                ..
            },
        ) => browser_session_id == result_session,
        (
            ActionIntent::Browser(BrowserIntent::TabsActivate { target, .. }),
            TaskTabActionResult::Activated {
                target: result_target,
                ..
            },
        )
        | (
            ActionIntent::Browser(BrowserIntent::TabsClose { target, .. }),
            TaskTabActionResult::Closed {
                target: result_target,
                ..
            },
        ) => target == result_target,
        _ => false,
    }
}

impl CoreRuntime {
    fn prepare_task_tab_settlement(
        &self,
        task_id: &bip_types::identity::TaskId,
        command: &CommandEnvelope,
        result: &TaskTabActionResult,
    ) -> Result<Option<PendingTaskTabSettlement>, SubmitError> {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return Err(SubmitError::UnknownTask);
        };
        let invalid = || SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        };
        let Command::RecordActionOutcome { action_id, outcome } = &command.command else {
            return Err(invalid());
        };
        if outcome.code != ActionResultCode::Verified {
            return Err(invalid());
        }
        let Some(facts) = session.task.action_effect_facts(action_id) else {
            return Err(invalid());
        };
        if !result_matches_intent(facts.proposal.intent(), result) {
            return Err(invalid());
        }
        let Some((ordinal, sequence)) = turn_call_of(&facts.proposal.idempotency_key) else {
            return Ok(None);
        };
        let expected_call_id = call_id_for_turn(session.task.task_id(), ordinal);
        let Some(state) = self.loop_states.get(task_id.as_str()) else {
            return Ok(None);
        };
        let Some(residency) = state
            .residency
            .as_ref()
            .filter(|value| value.call_id() == &expected_call_id)
        else {
            return Ok(None);
        };
        if residency
            .call(sequence)
            .is_none_or(|call| call.tool_name != result.tool_name())
        {
            return Ok(None);
        }
        let mut handles = state.task_tabs.clone();
        let Ok(outcome) = handles.apply_verified(result) else {
            // A restored generation intentionally has no short-handle table.
            // The exact durable result is still valid, but it cannot recreate
            // a model number that belonged to lost residency.
            return Ok(None);
        };
        Ok(Some(PendingTaskTabSettlement {
            expected_call_id,
            sequence,
            handles,
            outcome,
        }))
    }

    /// Submits a verified task-tab outcome and stages its transient model
    /// context behind the same browser-owned storage commit.
    pub fn begin_submit_with_task_tab_result(
        &mut self,
        task_id: &bip_types::identity::TaskId,
        result: &TaskTabActionResult,
        submission: TaskResultSubmission,
    ) -> Result<BeginSubmit, SubmitError> {
        let settlement = self.prepare_task_tab_settlement(task_id, &submission.command, result)?;
        let outcome = self.begin_submit(
            task_id,
            submission.command,
            submission.operation_id,
            submission.deadline,
            submission.now_monotonic_millis,
            submission.now_utc_millis,
        )?;
        if matches!(outcome, BeginSubmit::AwaitingCommit(_)) {
            let pending = self
                .tasks
                .get_mut(task_id.as_str())
                .and_then(|session| session.pending_submit.as_mut())
                .ok_or(SubmitError::InvalidTaskResult {
                    in_memory_revision: 0,
                })?;
            pending.task_tab = settlement;
        }
        Ok(outcome)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::identity::{FrameId, GraphRevision, PageEpoch, TabId};
    use task_engine::{BrowserSessionId, ModelCallId, TaskTabSnapshot};

    #[test]
    fn a_late_commit_installs_no_handles_after_its_reply_is_gone() {
        let browser_session_id =
            BrowserSessionId::new("browser-session-1").unwrap_or_else(|_| unreachable!());
        let target = task_engine::TaskTabTarget::new(
            browser_session_id.clone(),
            TabId::new("tab-owned"),
            FrameId::new("frame-owned"),
            PageEpoch::new("epoch-owned"),
            GraphRevision(7),
        );
        let result = TaskTabActionResult::listed(
            browser_session_id,
            vec![TaskTabSnapshot::new(target, false)],
        )
        .unwrap_or_else(|_| unreachable!());
        let mut handles = TaskTabHandleTable::default();
        let outcome = handles
            .apply_verified(&result)
            .unwrap_or_else(|_| unreachable!());
        let settlement = PendingTaskTabSettlement {
            expected_call_id: ModelCallId::new("model-task-1-3"),
            sequence: 0,
            handles,
            outcome,
        };
        let mut state = LoopState::default();

        settlement.install(&mut state);

        assert!(state.task_tabs.is_empty());
        assert!(state.residency.is_none());
    }
}
