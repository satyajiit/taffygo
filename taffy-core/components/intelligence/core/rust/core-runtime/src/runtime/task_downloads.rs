// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Commit-gated installation of browser-verified download results.

use bip_types::ActionResultCode;
use loop_kernel::state::LoopState;
use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{
    call_id_for_turn, turn_call_of, Command, TaskDownloadActionResult, TaskDownloadHandleTable,
    TaskDownloadTranscriptOutcome,
};

use super::{BeginSubmit, CoreRuntime, SubmitError, TaskResultSubmission};

pub(super) struct PendingTaskDownloadSettlement {
    expected_call_id: Option<task_engine::ModelCallId>,
    sequence: u32,
    handles: TaskDownloadHandleTable,
    outcome: TaskDownloadTranscriptOutcome,
}

impl core::fmt::Debug for PendingTaskDownloadSettlement {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("PendingTaskDownloadSettlement")
            .field("expected_call_id", &self.expected_call_id)
            .field("sequence", &self.sequence)
            .finish_non_exhaustive()
    }
}

impl PendingTaskDownloadSettlement {
    pub(super) fn install(self, state: &mut LoopState) {
        if let Some(expected) = self.expected_call_id {
            let Some(residency) = state
                .residency
                .as_mut()
                .filter(|value| value.call_id() == &expected)
            else {
                return;
            };
            residency.replace_task_downloads(self.handles.clone());
            let _recorded = residency.record_task_download_outcome(self.sequence, self.outcome);
        }
        state.task_downloads = self.handles;
    }
}

fn result_matches_intent(intent: &ActionIntent, result: &TaskDownloadActionResult) -> bool {
    match (intent, result) {
        (
            ActionIntent::Browser(
                BrowserIntent::DownloadStart {
                    browser_session_id, ..
                }
                | BrowserIntent::DownloadFromLink {
                    browser_session_id, ..
                },
            ),
            TaskDownloadActionResult::Started {
                browser_session_id: result_session,
                ..
            },
        )
        | (
            ActionIntent::Browser(BrowserIntent::DownloadList {
                browser_session_id, ..
            }),
            TaskDownloadActionResult::Listed {
                browser_session_id: result_session,
                ..
            },
        ) => browser_session_id == result_session,
        (
            ActionIntent::Browser(BrowserIntent::DownloadCancel {
                browser_session_id,
                download_id,
                ..
            }),
            TaskDownloadActionResult::Cancelled {
                browser_session_id: result_session,
                download,
            },
        ) => browser_session_id == result_session && download.download_id() == download_id,
        _ => false,
    }
}

impl CoreRuntime {
    fn prepare_task_download_settlement(
        &self,
        task_id: &bip_types::identity::TaskId,
        command: &task_engine::CommandEnvelope,
        result: &TaskDownloadActionResult,
    ) -> Result<Option<PendingTaskDownloadSettlement>, SubmitError> {
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
        let Some(state) = self.loop_states.get(task_id.as_str()) else {
            return Ok(None);
        };
        let (expected_call_id, sequence) = if let Some((ordinal, sequence)) =
            turn_call_of(&facts.proposal.idempotency_key)
        {
            let expected = call_id_for_turn(session.task.task_id(), ordinal);
            let Some(residency) = state
                .residency
                .as_ref()
                .filter(|value| value.call_id() == &expected)
            else {
                return Ok(None);
            };
            if residency
                .call(sequence)
                .is_none_or(|call| call.tool_name != facts.proposal.tool_name())
            {
                return Ok(None);
            }
            (Some(expected), sequence)
        } else if session.task.skill_version_id().is_some() && facts.proposal.plan_step_id.is_some()
        {
            // A selected saved procedure owns no model reply. Its exact
            // verified plan action still updates this task's transient
            // download table, after the same durable outcome commit.
            (None, 0)
        } else {
            return Ok(None);
        };
        let mut handles = state.task_downloads.clone();
        let Ok(outcome) = handles.apply_verified(result) else {
            return Ok(None);
        };
        Ok(Some(PendingTaskDownloadSettlement {
            expected_call_id,
            sequence,
            handles,
            outcome,
        }))
    }

    pub fn begin_submit_with_task_download_result(
        &mut self,
        task_id: &bip_types::identity::TaskId,
        result: &TaskDownloadActionResult,
        submission: TaskResultSubmission,
    ) -> Result<BeginSubmit, SubmitError> {
        let settlement =
            self.prepare_task_download_settlement(task_id, &submission.command, result)?;
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
            pending.task_download = settlement;
        }
        Ok(outcome)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::identity::TabId;
    use task_engine::{
        BrowserSessionId, ModelCallId, TaskDownloadDirectoryClass, TaskDownloadMediaType,
        TaskDownloadSnapshot, TaskDownloadState,
    };

    fn browser_session(value: &str) -> BrowserSessionId {
        BrowserSessionId::new(value).unwrap_or_else(|_| unreachable!())
    }

    fn snapshot() -> TaskDownloadSnapshot {
        TaskDownloadSnapshot::new(
            "opaque-manager-guid".to_owned(),
            TaskDownloadState::Complete,
            TaskDownloadMediaType::Application,
            512,
            TaskDownloadDirectoryClass::Undecided,
        )
        .unwrap_or_else(|_| unreachable!())
    }

    #[test]
    fn a_late_commit_installs_no_download_handles_after_its_reply_is_gone() {
        let result = TaskDownloadActionResult::listed(
            browser_session("browser-session-1"),
            vec![snapshot()],
            false,
        )
        .unwrap_or_else(|_| unreachable!());
        let mut handles = TaskDownloadHandleTable::default();
        let outcome = handles
            .apply_verified(&result)
            .unwrap_or_else(|_| unreachable!());
        let settlement = PendingTaskDownloadSettlement {
            expected_call_id: Some(ModelCallId::new("model-task-1-3")),
            sequence: 0,
            handles,
            outcome,
        };
        let mut state = LoopState::default();

        settlement.install(&mut state);

        assert!(state.task_downloads.is_empty());
        assert!(state.residency.is_none());
    }

    #[test]
    fn a_selected_procedure_keeps_verified_download_progress_without_a_model_reply() {
        let mut state = LoopState::default();
        let result =
            TaskDownloadActionResult::started(browser_session("browser-session-1"), snapshot());
        let mut handles = TaskDownloadHandleTable::default();
        let outcome = handles
            .apply_verified(&result)
            .unwrap_or_else(|_| unreachable!());
        let settlement = PendingTaskDownloadSettlement {
            expected_call_id: None,
            sequence: 0,
            handles,
            outcome,
        };
        assert_eq!(state.task_downloads.completed_task_downloads(), 0);
        settlement.install(&mut state);
        assert_eq!(state.task_downloads.completed_task_downloads(), 1);
        assert!(state.residency.is_none());
    }

    #[test]
    fn a_download_result_cannot_change_operation_or_browser_session() {
        let expected_session = browser_session("browser-session-1");
        let other_session = browser_session("browser-session-2");
        let tab = TabId::new("tab-1");
        let start = ActionIntent::Browser(BrowserIntent::DownloadStart {
            tab: tab.clone(),
            address: "https://downloads.example/item".to_owned(),
            browser_session_id: expected_session.clone(),
        });
        let list = ActionIntent::Browser(BrowserIntent::DownloadList {
            tab,
            browser_session_id: expected_session.clone(),
        });
        let cancel = ActionIntent::Browser(BrowserIntent::DownloadCancel {
            tab: TabId::new("tab-1"),
            browser_session_id: expected_session.clone(),
            download_id: "opaque-manager-guid".to_owned(),
        });
        let started = TaskDownloadActionResult::started(expected_session.clone(), snapshot());
        let wrong_session = TaskDownloadActionResult::started(other_session, snapshot());
        let listed = TaskDownloadActionResult::listed(expected_session, Vec::new(), false)
            .unwrap_or_else(|_| unreachable!());
        let cancelled = TaskDownloadActionResult::cancelled(
            browser_session("browser-session-1"),
            TaskDownloadSnapshot::new(
                "opaque-manager-guid".to_owned(),
                TaskDownloadState::Cancelled,
                TaskDownloadMediaType::Application,
                512,
                TaskDownloadDirectoryClass::Undecided,
            )
            .unwrap_or_else(|_| unreachable!()),
        )
        .unwrap_or_else(|_| unreachable!());

        assert!(result_matches_intent(&start, &started));
        assert!(!result_matches_intent(&start, &wrong_session));
        assert!(!result_matches_intent(&start, &listed));
        assert!(!result_matches_intent(&list, &started));
        assert!(result_matches_intent(&cancel, &cancelled));
    }
}
