// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Commit-gated installation of browser-verified media-probe facts.

use loop_kernel::state::LoopState;
use task_engine::action::{ActionIntent, MediaOperation};
use task_engine::{
    call_id_for_turn, job_id_for_action, turn_call_of, Command, MediaProbeTranscriptOutcome,
    ToolJobStatus,
};

use super::{BeginSubmit, CoreRuntime, SubmitError, TaskResultSubmission};

/// Scalar probe facts installed only after their exact tool outcome commits.
pub(super) struct PendingMediaProbeSettlement {
    expected_call_id: task_engine::ModelCallId,
    sequence: u32,
    outcome: MediaProbeTranscriptOutcome,
}

impl core::fmt::Debug for PendingMediaProbeSettlement {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("PendingMediaProbeSettlement")
            .field("expected_call_id", &self.expected_call_id)
            .field("sequence", &self.sequence)
            .finish_non_exhaustive()
    }
}

impl PendingMediaProbeSettlement {
    pub(super) fn install(self, state: &mut LoopState) {
        let Some(residency) = state
            .residency
            .as_mut()
            .filter(|value| value.call_id() == &self.expected_call_id)
        else {
            return;
        };
        let _recorded = residency.record_media_probe_outcome(self.sequence, self.outcome);
    }
}

fn result_matches_intent(intent: &ActionIntent, result: &MediaProbeTranscriptOutcome) -> bool {
    matches!(
        intent,
        ActionIntent::MediaTool(media)
            if media.operation == MediaOperation::Probe && media.tool_name == result.tool_name()
    )
}

impl CoreRuntime {
    fn prepare_media_probe_settlement(
        &self,
        task_id: &bip_types::identity::TaskId,
        command: &task_engine::CommandEnvelope,
        result: MediaProbeTranscriptOutcome,
    ) -> Result<Option<PendingMediaProbeSettlement>, SubmitError> {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return Err(SubmitError::UnknownTask);
        };
        let invalid = || SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        };
        let Command::RecordToolJobOutcome {
            action_id,
            job_id,
            outcome,
        } = &command.command
        else {
            return Err(invalid());
        };
        if outcome.status != ToolJobStatus::Succeeded
            || job_id != &job_id_for_action(session.task.task_id(), action_id)
        {
            return Err(invalid());
        }
        let Some(facts) = session.task.action_effect_facts(action_id) else {
            return Err(invalid());
        };
        if !result_matches_intent(facts.proposal.intent(), &result) {
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
            .is_none_or(|call| call.tool_name != result.tool_name())
        {
            return Ok(None);
        }
        Ok(Some(PendingMediaProbeSettlement {
            expected_call_id,
            sequence,
            outcome: result,
        }))
    }

    /// Stages probe facts beside the exact durable tool-job outcome. The
    /// transcript receives them only after browser-owned storage commits.
    pub fn begin_submit_with_media_probe_result(
        &mut self,
        task_id: &bip_types::identity::TaskId,
        result: MediaProbeTranscriptOutcome,
        submission: TaskResultSubmission,
    ) -> Result<BeginSubmit, SubmitError> {
        let settlement =
            self.prepare_media_probe_settlement(task_id, &submission.command, result)?;
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
            pending.media_probe = settlement;
        }
        Ok(outcome)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::identity::TabId;
    use task_engine::action::MediaToolIntent;
    use task_engine::{
        BrowserSessionId, IdempotencyClass, ModelCallId, ModelReply, ModelStopReason,
        ModelToolCall, RenderShape, TurnPage, TurnResidency, TurnUsage,
    };

    fn outcome() -> MediaProbeTranscriptOutcome {
        MediaProbeTranscriptOutcome::new(2_000, 1, 0, 0, 0)
            .unwrap_or_else(|_| unreachable!("bounded reading"))
    }

    fn residency(call_id: &str, tool: &str) -> TurnResidency {
        let reply = ModelReply {
            stop: ModelStopReason::ToolCall,
            overflow: None,
            usage: TurnUsage::default(),
            answer_segments: 0,
            tool_calls: vec![ModelToolCall::new(tool, Vec::new())],
        };
        let page = TurnPage::new(
            TabId::new("tab-1"),
            task_engine::HandleTable::new(),
            RenderShape::empty([0_u8; 32]),
        );
        TurnResidency::read(ModelCallId::new(call_id), page, reply)
            .unwrap_or_else(|| unreachable!("bounded reply"))
    }

    fn media_intent(operation: MediaOperation) -> ActionIntent {
        ActionIntent::MediaTool(MediaToolIntent {
            tool_name: operation.tool_name().to_owned(),
            operation,
            source_id: "download-opaque-1".to_owned(),
            source_browser_session_id: BrowserSessionId::new("browser-session-1")
                .unwrap_or_else(|_| unreachable!("valid session")),
            source_bytes: 1_024,
            tab: TabId::new("tab-1"),
            node: None,
            max_frames: 0,
            idempotency: IdempotencyClass::PureRead,
        })
    }

    #[test]
    fn only_the_probe_operation_accepts_probe_facts() {
        assert!(result_matches_intent(
            &media_intent(MediaOperation::Probe),
            &outcome()
        ));
        assert!(!result_matches_intent(
            &media_intent(MediaOperation::ExtractAudio),
            &outcome()
        ));
    }

    #[test]
    fn a_committed_probe_installs_only_into_its_exact_live_reply() {
        let settlement = PendingMediaProbeSettlement {
            expected_call_id: ModelCallId::new("model-task-1-4"),
            sequence: 0,
            outcome: outcome(),
        };
        let mut exact = LoopState {
            residency: Some(residency("model-task-1-4", "media.probe")),
            ..LoopState::default()
        };
        settlement.install(&mut exact);
        assert!(exact
            .residency
            .as_ref()
            .and_then(|value| value.media_probe_outcome(0))
            .is_some());

        let late = PendingMediaProbeSettlement {
            expected_call_id: ModelCallId::new("model-task-1-4"),
            sequence: 0,
            outcome: outcome(),
        };
        let mut later = LoopState {
            residency: Some(residency("model-task-1-5", "media.probe")),
            ..LoopState::default()
        };
        late.install(&mut later);
        assert!(later
            .residency
            .as_ref()
            .and_then(|value| value.media_probe_outcome(0))
            .is_none());
    }
}
