// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Commit-gated installation of browser-verified store rows.
//!
//! The durable outcome of a store read is the reducer's record; the rows the
//! browser answered are its transient companion. They are installed beside
//! the model reply that asked for them only after the outcome's storage
//! transaction commits, and only while that reply is still resident, so a
//! restored generation never sees rows it did not ask for.

use bip_types::ActionResultCode;
use loop_kernel::state::LoopState;
use task_engine::action::ActionIntent;
use task_engine::{
    call_id_for_turn, turn_call_of, Command, CommandEnvelope, TaskStoreActionResult,
    TaskStoreTranscriptOutcome,
};

use super::{BeginSubmit, CoreRuntime, SubmitError, TaskResultSubmission};

pub(super) struct PendingTaskStoreSettlement {
    expected_call_id: task_engine::ModelCallId,
    sequence: u32,
    outcome: TaskStoreTranscriptOutcome,
}

impl core::fmt::Debug for PendingTaskStoreSettlement {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("PendingTaskStoreSettlement")
            .field("expected_call_id", &self.expected_call_id)
            .field("sequence", &self.sequence)
            .finish_non_exhaustive()
    }
}

impl PendingTaskStoreSettlement {
    pub(super) fn install(self, state: &mut LoopState) {
        let Some(residency) = state
            .residency
            .as_mut()
            .filter(|value| value.call_id() == &self.expected_call_id)
        else {
            return;
        };
        let _recorded = residency.record_store_outcome(self.sequence, self.outcome);
    }
}

impl CoreRuntime {
    fn prepare_task_store_settlement(
        &self,
        task_id: &bip_types::identity::TaskId,
        command: &CommandEnvelope,
        result: &TaskStoreActionResult,
    ) -> Result<Option<PendingTaskStoreSettlement>, SubmitError> {
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
        let intent_matches = matches!(facts.proposal.intent(), ActionIntent::Store(intent)
            if intent.tool_name() == result.tool_name());
        if !intent_matches {
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
        Ok(Some(PendingTaskStoreSettlement {
            expected_call_id,
            sequence,
            outcome: result.transcript_outcome(),
        }))
    }

    /// Submits a verified store-read outcome and stages its bounded rows as
    /// model context behind the same browser-owned storage commit.
    pub fn begin_submit_with_task_store_result(
        &mut self,
        task_id: &bip_types::identity::TaskId,
        result: &TaskStoreActionResult,
        submission: TaskResultSubmission,
    ) -> Result<BeginSubmit, SubmitError> {
        let settlement =
            self.prepare_task_store_settlement(task_id, &submission.command, result)?;
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
            pending.task_store = settlement;
        }
        Ok(outcome)
    }
}
