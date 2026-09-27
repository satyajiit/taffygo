// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! How a task ends, and what the user does with what it produced.
//!
//! The invariant this module carries is the crate's fourth: **complete means
//! complete.** `COMPLETED` is reachable only through
//! [`crate::task::TaskResult::is_complete`], checked here rather than asserted
//! by the caller, so a result with a labelled gap becomes `PARTIAL` and cannot
//! be promoted by choosing a different command.
//!
//! Every terminal edge releases the task's tabs. That is the point at which
//! the task stops being able to act at all, so it is written on the edge
//! rather than left to the runtime to remember.

use super::outcome::Outcome;
use super::Reducer;
use crate::artifact::{ArtifactKind, ArtifactRecord};
use crate::command::{Command, CommandKind};
use crate::effect::Effect;
use crate::event::{EventKind, EventSubject, TaskEvent};
use crate::ids::{ArtifactId, IdSource};
use crate::plan::PlanStatus;
use crate::records::{FactId, SourceId};
use crate::task::{FailureReason, StateReason, TaskActivityKind, TaskResult, TaskState};
use crate::time::Clock;
use crate::transition::RefusalReason;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Routes the result family apart from the reducer's main command index.
    pub(super) fn execute_result_command(
        &mut self,
        command: &Command,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        match command {
            Command::ResultCandidateReady => Ok(Self::on_result_candidate_ready()),
            Command::CompleteResultValidated(result) => self.on_complete_result_validated(result),
            Command::PartialResultValidated(result) => Ok(self.on_partial_result_validated(result)),
            Command::FailTask { reason } => Ok(self.on_fail_task(*reason)),
            Command::CorrectFact { fact_id } => Ok(Self::on_correct_fact(*fact_id, kind)),
            Command::ExcludeSource { source_id } => Ok(self.on_exclude_source(source_id, kind)),
            Command::RequestArtifact {
                artifact_id,
                format,
                workspace_revision,
            } => self.on_request_artifact(artifact_id, *format, *workspace_revision, kind),
            Command::AcceptArtifact { artifact_id } => {
                Ok(self.on_accept_artifact(artifact_id, kind))
            }
            Command::ExportArtifact {
                artifact_id,
                format,
            } => self.on_export_artifact(artifact_id, *format, kind),
            _ => Err(RefusalReason::JournalRefused),
        }
    }

    /// A result candidate exists and is being validated.
    pub(super) fn on_result_candidate_ready() -> Outcome {
        Outcome::moves(TaskState::Completing, StateReason::ResultCandidateReady)
    }

    /// A validated result with no unmet requirement completes the task.
    pub(super) fn on_complete_result_validated(
        &mut self,
        result: &TaskResult,
    ) -> Result<Outcome, RefusalReason> {
        if !result.is_complete_for(self.task.kind()) {
            return Err(RefusalReason::ResultHasUnmetRequirements);
        }
        self.finish(result.clone());
        Ok(
            Outcome::moves(TaskState::Completed, StateReason::ResultValidated)
                .with_effects(vec![Effect::ReleaseTaskTabs]),
        )
    }

    /// A useful result with labelled gaps ends the task as partly done.
    pub(super) fn on_partial_result_validated(&mut self, result: &TaskResult) -> Outcome {
        self.finish(result.clone());
        Outcome::moves(TaskState::Partial, StateReason::ResultHasGaps)
            .with_effects(vec![Effect::ReleaseTaskTabs])
    }

    /// A terminal failure. The plan is superseded so nothing reads it as live.
    pub(super) fn on_fail_task(&mut self, reason: FailureReason) -> Outcome {
        self.pending_action = None;
        self.task.consent_stage = None;
        self.task.terminal_failure = Some(reason);
        if let Some(plan) = self.plan.as_mut() {
            plan.supersede();
        }
        Outcome::moves(TaskState::Failed, StateReason::TerminalFailure)
            .with_effects(vec![Effect::ReleaseTaskTabs])
    }

    /// The user corrected a fact the result rests on.
    pub(super) fn on_correct_fact(fact_id: FactId, kind: CommandKind) -> Outcome {
        Outcome::recorded(
            vec![TaskEvent::record(EventKind::FactCorrected, kind)
                .about(EventSubject::Fact(fact_id))],
            Vec::new(),
        )
    }

    /// The user removed a source from scope. The exclusion is durable.
    pub(super) fn on_exclude_source(&mut self, source_id: &SourceId, kind: CommandKind) -> Outcome {
        self.task.scope.exclude(source_id);
        Outcome::recorded(
            vec![TaskEvent::record(EventKind::SourceExcluded, kind)
                .about(EventSubject::Source(*source_id))],
            Vec::new(),
        )
    }

    /// Records a successful bounded render and exposes only its identity and
    /// evidence revision to persistence and audit.
    pub(super) fn on_request_artifact(
        &mut self,
        artifact_id: &ArtifactId,
        format: ArtifactKind,
        workspace_revision: u64,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        if workspace_revision == 0 {
            return Err(RefusalReason::ArtifactNotReady);
        }
        if self.artifacts.contains_key(artifact_id.as_str()) {
            return Err(RefusalReason::ArtifactAlreadyReady);
        }
        if self.artifacts.len() >= super::MAX_ARTIFACTS_PER_TASK {
            return Err(RefusalReason::ArtifactRegisterFull);
        }
        self.artifacts.insert(
            artifact_id.as_str().to_owned(),
            ArtifactRecord::workspace(artifact_id.clone(), format, workspace_revision),
        );
        // A rendered artifact is a thing the task did not have (decision 0233).
        self.note_progress();
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::ArtifactReady, kind)
                .about(EventSubject::Artifact(artifact_id.clone()))],
            vec![Effect::GenerateArtifact {
                artifact_id: artifact_id.clone(),
                kind: format,
                workspace_revision,
            }],
        ))
    }

    /// The user accepted an artifact. Acceptance is what makes it exportable.
    pub(super) fn on_accept_artifact(
        &mut self,
        artifact_id: &ArtifactId,
        kind: CommandKind,
    ) -> Outcome {
        if !self.task.accepted_artifacts.contains(artifact_id) {
            self.task.accepted_artifacts.push(artifact_id.clone());
        }
        Outcome::recorded(
            vec![TaskEvent::record(EventKind::ArtifactAccepted, kind)
                .about(EventSubject::Artifact(artifact_id.clone()))],
            Vec::new(),
        )
    }

    /// The user exported an accepted artifact.
    pub(super) fn on_export_artifact(
        &self,
        artifact_id: &ArtifactId,
        format: ArtifactKind,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let artifact = self
            .artifacts
            .get(artifact_id.as_str())
            .ok_or(RefusalReason::ArtifactNotReady)?;
        if artifact.kind() != format {
            return Err(RefusalReason::ArtifactNotReady);
        }
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::ArtifactExported, kind)
                .about(EventSubject::Artifact(artifact_id.clone()))],
            vec![Effect::ExportArtifact {
                artifact_id: artifact_id.clone(),
                kind: artifact.kind(),
                workspace_revision: artifact.workspace_revision(),
            }],
        ))
    }

    /// Settles everything a terminal result makes stale.
    fn finish(&mut self, result: TaskResult) {
        self.pending_action = None;
        self.task.consent_stage = None;
        // The last step of what the task did, on both terminal result edges:
        // done and partly done both assembled an output, and the count is the
        // facts it rests on rather than the facts it saw. A count that will not
        // fit is clamped rather than wrapped — a step saying "a great many" is
        // a worse sentence than the truth and a better one than nought.
        self.note(
            TaskActivityKind::BuiltOutput,
            None,
            u32::try_from(result.fact_count).unwrap_or(u32::MAX),
        );
        self.task.terminal_result = Some(result);
        self.task.terminal_failure = None;
        if let Some(plan) = self.plan.as_mut() {
            if plan.status() == PlanStatus::Active {
                plan.supersede();
            }
        }
    }
}
