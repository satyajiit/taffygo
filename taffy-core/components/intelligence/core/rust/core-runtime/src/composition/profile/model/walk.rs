// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable task-walk planning and loop-local person input.

use std::rc::Rc;

use bip_types::identity::TaskId;
use core_api_types::WorkspaceExportFormat;
use loop_kernel::walk;
use task_engine::{ArtifactKind, Command, CommandEnvelope, FailureReason, TurnGap};

use crate::{
    next_agent_command_envelope, next_durable_command_envelope, AgentWorkflowError,
    DurableWorkflowError, ProcedureCatalogue, WorkspaceExportError,
};

use super::ProfileServiceRuntime;

fn selected_procedure<'a>(
    catalogue: &'a ProcedureCatalogue,
    version_id: Option<&str>,
) -> Result<Option<&'a procedure_engine::Procedure>, walk::WalkError> {
    let Some(version_id) = version_id else {
        return Ok(None);
    };
    match catalogue.resolve(version_id) {
        Some(procedure) if procedure.is_runnable() => Ok(Some(procedure)),
        Some(_) | None => Err(walk::WalkError::SelectedProcedureUnavailable),
    }
}

impl ProfileServiceRuntime {
    /// Chooses and identifies the next durable command for one task's walk.
    ///
    /// The outer `None` means the task identity is not open. The walk settles
    /// a refused call first, settles loop-local calls on the residency, then
    /// asks whichever scheduler the consented route owns; the caller submits
    /// the plan and rolls a staged ask back when admission refuses it.
    pub fn advance_task_walk(
        &mut self,
        task_id: &TaskId,
        service_generation: u64,
        now_monotonic_ms: u64,
    ) -> Option<Result<Option<walk::PlannedCommand>, walk::WalkError>> {
        let digest = Rc::clone(&self.digest);
        let refused_call = self
            .core
            .loop_state(task_id.as_str())
            .and_then(|state| state.residency.as_ref())
            .filter(|residency| {
                residency.reply().tool_calls.iter().any(|call| {
                    !self
                        .assistant_configuration
                        .admits_tool_call(&call.tool_name)
                })
            })
            .map(|residency| residency.call_id().clone());
        if let Some(call_id) = refused_call {
            let state = self.core.loop_state_mut(task_id.as_str());
            state.residency = None;
            state.refused = Some(loop_kernel::state::RefusedModelCall {
                call_id,
                gap: TurnGap::Unreadable,
            });
        }
        let configured_tools = self.assistant_configuration.effective_tool_allowlist();
        let procedures = &self.procedures;
        let (task, state) = self.core.walk_surfaces(task_id.as_str())?;
        let saved_version = crate::builtin_skills::saved_procedure_version(
            task.builtin_skill_reference(),
            task.skill_version_id(),
        );
        let procedure = match selected_procedure(procedures, saved_version) {
            Ok(procedure) => procedure,
            Err(error) => return Some(Err(error)),
        };
        let agent_walk = procedure.is_none()
            && walk::workflow_for_provider_route(
                task.model_turn_facts().provider_route_id.as_deref(),
            ) == walk::TaskWorkflow::Agent;
        let planned = walk::advance_with_procedure(
            task,
            procedure,
            Some(&configured_tools),
            state,
            digest.as_ref(),
            service_generation,
            now_monotonic_ms,
        );
        let planned = match planned {
            Ok(Some(planned)) => planned,
            Ok(None) => return Some(Ok(None)),
            Err(error) => return Some(Err(error)),
        };
        Some(
            self.prepare_walk_result(
                task_id,
                planned,
                agent_walk,
                service_generation,
                now_monotonic_ms,
            )
            .map(Some),
        )
    }

    /// The runtime owns the retained workspace used by result validation and
    /// artifact preparation. The pure walk owns all resulting identities.
    fn prepare_walk_result(
        &mut self,
        task_id: &TaskId,
        mut planned: walk::PlannedCommand,
        agent_walk: bool,
        service_generation: u64,
        now_monotonic_ms: u64,
    ) -> Result<walk::PlannedCommand, walk::WalkError> {
        if agent_walk {
            if let Command::PartialResultValidated(candidate) = &planned.envelope.command {
                if let Some((workspace_revision, result)) =
                    self.core.validated_research_result(task_id, candidate)
                {
                    return walk::bind_research_result(
                        planned,
                        task_id.as_str(),
                        workspace_revision,
                        result,
                        self.digest.as_ref(),
                    );
                }
            }
        }
        let expected_revision = planned.envelope.expected_revision;
        if let Command::RequestArtifact {
            artifact_id,
            format,
            workspace_revision,
        } = &mut planned.envelope.command
        {
            let prepared = match *format {
                ArtifactKind::Markdown => self.core.prepare_task_artifact(
                    task_id,
                    artifact_id.as_str(),
                    WorkspaceExportFormat::Markdown,
                ),
                ArtifactKind::Csv => self.core.prepare_task_artifact(
                    task_id,
                    artifact_id.as_str(),
                    WorkspaceExportFormat::Csv,
                ),
                ArtifactKind::Xlsx
                | ArtifactKind::Pdf
                | ArtifactKind::Docx
                | ArtifactKind::Pptx => self.core.prepare_rich_task_artifact(task_id, *format),
                // Media artifacts are born only from a successfully verified
                // media job. A model-authored artifact request cannot mint an
                // empty placeholder or route them through a document encoder.
                ArtifactKind::WaveAudio | ArtifactKind::FrameArchive => {
                    Err(WorkspaceExportError::RenderRefused)
                }
            };
            *workspace_revision = match prepared {
                Ok(revision) => revision,
                Err(error) => {
                    let reason = match error {
                        WorkspaceExportError::NoExportableFacts
                        | WorkspaceExportError::InvalidEvidence => {
                            FailureReason::SourcesUnavailable
                        }
                        WorkspaceExportError::RenderRefused => FailureReason::BudgetExhausted,
                        WorkspaceExportError::StaleRevision | WorkspaceExportError::Store(_) => {
                            FailureReason::JournalUnusable
                        }
                    };
                    return walk::artifact_failure_command(
                        expected_revision,
                        artifact_id.as_str(),
                        reason,
                        service_generation,
                        now_monotonic_ms,
                    );
                }
            };
        }
        Ok(planned)
    }

    /// Holds a classified person-answer line until the next compose consumes it.
    pub fn stage_person_answer(&mut self, task_id: &str, line: String) {
        self.core.loop_state_mut(task_id).person_answer = Some(line);
    }

    /// Drops a staged person-answer that never became a committed command.
    pub fn drop_person_answer(&mut self, task_id: &str) {
        self.core.loop_state_mut(task_id).person_answer = None;
    }

    /// Whether this generation still holds the task's last model reply.
    ///
    /// A follow-up composes on that reply (decision 0137); a task restored
    /// into a new generation has its durable facts and none of the prose, so
    /// the conversation is gone and the follow-up is refused rather than
    /// composed over a transcript the core would have to make up.
    #[must_use]
    pub fn holds_transcript(&self, task_id: &str) -> bool {
        self.core
            .loop_state(task_id)
            .is_some_and(|state| state.residency.is_some())
    }

    /// Holds a classified ask subject until the person answers or the task ends.
    pub fn stage_ask_prompt(&mut self, task_id: &str, line: String) {
        self.core.loop_state_mut(task_id).ask_prompt = Some(line);
    }

    /// Drops a staged ask subject.
    pub fn drop_ask_prompt(&mut self, task_id: &str) {
        self.core.loop_state_mut(task_id).ask_prompt = None;
    }

    /// Records that the browser definitely refused this task's discovery
    /// bootstrap. The next walk that finds the task running ends it under
    /// `SourcesUnavailable`; a queued task is walked to running first.
    pub fn note_discovery_unavailable(&mut self, task_id: &str) {
        self.core.loop_state_mut(task_id).discovery_unavailable = true;
    }

    /// Computes the next replay-stable command for the assistant loop.
    ///
    /// The outer `None` means the task identity is not open in this profile;
    /// the inner `None` means the loop is truthfully waiting or terminal.
    pub fn next_agent_task_command(
        &self,
        task_id: &TaskId,
        residency: Option<&task_engine::TurnResidency>,
    ) -> Option<Result<Option<CommandEnvelope>, AgentWorkflowError>> {
        self.core
            .task(task_id)
            .map(|task| next_agent_command_envelope(task, residency, self.digest.as_ref()))
    }

    /// Computes the next replay-stable command for whichever scheduler the
    /// task's consented route owns.
    ///
    /// The outer `None` means the task identity is not open in this profile;
    /// the inner `None` means that scheduler is waiting or the task disclosed
    /// no route. The residency is the assistant table's; a reviewed task
    /// ignores it.
    pub fn next_durable_task_command(
        &self,
        task_id: &TaskId,
        residency: Option<&task_engine::TurnResidency>,
    ) -> Option<Result<Option<CommandEnvelope>, DurableWorkflowError>> {
        self.core
            .task(task_id)
            .map(|task| next_durable_command_envelope(task, residency, self.digest.as_ref()))
    }
}
