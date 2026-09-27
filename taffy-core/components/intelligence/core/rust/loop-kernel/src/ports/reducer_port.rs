// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical reducer implementation of the task composition port.

mod boxed;
mod model_facts;
mod transcript;

use bip_types::identity::TaskId;
use task_engine::task::ConsentStage;
use task_engine::{
    Accepted, ActionState, ArtifactCustody, ArtifactId, CommandEnvelope, Refusal, StepState,
    TaskJournal, TaskState, WorkspaceId,
};

use super::{
    browser_facts, ActionEffectFacts, BuiltinSkillBindingFacts, ModelTurnFacts, PendingActionFacts,
    PendingPermissionFacts, PlanProgressFacts, TaskActivityFacts, TaskArtifactFacts,
    TaskDiscoveryAuthorityFacts, TaskEnginePort, TaskViewFacts, TaskViewFactsError,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum ActionProjectionClass {
    NoUiOverride,
    WaitingApproval,
    OutcomeUnknown,
}

pub(crate) const fn classify_action_state(state: ActionState) -> ActionProjectionClass {
    match state {
        ActionState::Proposed
        | ActionState::Authorized
        | ActionState::Dispatching
        | ActionState::Verifying
        | ActionState::Verified
        | ActionState::Failed
        | ActionState::Rejected
        | ActionState::Cancelled => ActionProjectionClass::NoUiOverride,
        ActionState::WaitingApproval => ActionProjectionClass::WaitingApproval,
        ActionState::OutcomeUnknown => ActionProjectionClass::OutcomeUnknown,
    }
}

pub(crate) const fn is_projected_finished_step(state: StepState) -> bool {
    match state {
        StepState::Pending | StepState::Ready | StepState::Running | StepState::Waiting => false,
        StepState::Succeeded
        | StepState::Skipped
        | StepState::Failed
        | StepState::Cancelled
        | StepState::Superseded => true,
    }
}

fn project_plan_progress<C, I>(
    reducer: &task_engine::Reducer<C, I>,
) -> Result<Option<PlanProgressFacts>, TaskViewFactsError>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    reducer
        .plan()
        .map(|plan| {
            let total_steps =
                u32::try_from(plan.steps().len()).map_err(|_| TaskViewFactsError::CountOverflow)?;
            let finished = plan
                .steps()
                .iter()
                .filter(|step| is_projected_finished_step(step.state()))
                .count();
            let finished_steps =
                u32::try_from(finished).map_err(|_| TaskViewFactsError::CountOverflow)?;
            Ok(PlanProgressFacts {
                status: plan.status(),
                total_steps,
                finished_steps,
            })
        })
        .transpose()
}

fn project_pending_action<C, I>(
    reducer: &task_engine::Reducer<C, I>,
) -> Result<Option<PendingActionFacts>, TaskViewFactsError>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let task = reducer.task();
    reducer
        .pending_action()
        .map(|action_id| {
            let action = reducer
                .action(action_id)
                .ok_or(TaskViewFactsError::MissingPendingAction)?;
            if action.state() != ActionState::WaitingApproval {
                return Err(TaskViewFactsError::PendingActionNotWaiting);
            }
            if task.state() != TaskState::AwaitingConsent
                || task.consent_stage() != Some(ConsentStage::InTask)
            {
                return Err(TaskViewFactsError::PendingActionOutsideConsent);
            }
            let proposal_digest = &action.proposal().proposal_digest.value;
            if proposal_digest.len() != 64
                || !proposal_digest
                    .bytes()
                    .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
            {
                return Err(TaskViewFactsError::InvalidProposalDigest);
            }
            Ok(PendingActionFacts {
                action_id: action.action_id().as_str().to_owned(),
                action_class: action.proposal().action_class(),
                item_count: 1,
                proposal_digest: proposal_digest.clone(),
            })
        })
        .transpose()
}

impl<C, I> TaskEnginePort for task_engine::Reducer<C, I>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    fn task_id(&self) -> &TaskId {
        self.task().task_id()
    }

    fn workspace_id(&self) -> Option<&WorkspaceId> {
        self.task().workspace_id()
    }

    fn revision(&self) -> u64 {
        self.task().revision()
    }

    fn updated_at_utc_millis(&self) -> u64 {
        self.task().updated_at().0
    }

    fn is_terminal(&self) -> bool {
        self.task().state().is_terminal()
    }

    fn artifact_custody(&self, artifact_id: &ArtifactId) -> Option<ArtifactCustody> {
        self.artifact(artifact_id)
            .map(task_engine::ArtifactRecord::custody)
    }

    fn capability_policy_version(&self) -> task_engine::PolicyVersion {
        self.task().snapshot().capability_policy_version
    }

    fn action_effect_facts(
        &self,
        action_id: &bip_types::identity::ActionId,
    ) -> Option<ActionEffectFacts> {
        let action = self.action(action_id)?;
        let task = self.task();
        Some(ActionEffectFacts {
            action_id: action.action_id().as_str().to_owned(),
            proposal: action.proposal().clone(),
            state: action.state(),
            capability_id: action.capability_id().map(|id| id.as_str().to_owned()),
            dispatch_id: action.dispatch_id().map(|id| id.0.clone()),
            approval: action.approval().cloned(),
            control_mode: task.control_mode(),
            policy_version: task.snapshot().capability_policy_version,
            skill_version_id: task
                .snapshot()
                .skill_version_id
                .as_ref()
                .map(|id| id.0.clone()),
        })
    }

    fn discovery_authority_facts(&self) -> Option<TaskDiscoveryAuthorityFacts> {
        model_facts::discovery_authority_facts(self)
    }

    fn journal(&self) -> &TaskJournal {
        task_engine::Reducer::journal(self)
    }

    fn view_facts(&self) -> Result<TaskViewFacts, TaskViewFactsError> {
        let task = self.task();
        let plan_progress = project_plan_progress(self)?;
        let pending_action = project_pending_action(self)?;

        // Both counters are re-derived from the records on every projection,
        // and stay that way deliberately.
        //
        // The check below is the only thing that would catch an action left in
        // `WAITING_APPROVAL` that `pending_action` does not name, and it can
        // only catch it because the two sides come from different places: one
        // from the records themselves, one from the field the consent handlers
        // maintain. A counter kept up to date on each transition would make
        // both sides the same bookkeeping, and the mismatch it exists to find
        // would agree with itself.
        //
        // The scan was unbounded when the action register was; it is bounded
        // now by `task_engine::MAX_ACTIONS_PER_TASK`, so the cost this would
        // have bought back is a bounded constant and the cross-check is worth
        // more than the constant.
        let mut waiting_approval_actions = 0_u32;
        let mut outcome_unknown_actions = 0_u32;
        for action in self.actions() {
            let count = match classify_action_state(action.state()) {
                ActionProjectionClass::NoUiOverride => None,
                ActionProjectionClass::WaitingApproval => Some(&mut waiting_approval_actions),
                ActionProjectionClass::OutcomeUnknown => Some(&mut outcome_unknown_actions),
            };
            if let Some(count) = count {
                *count = count
                    .checked_add(1)
                    .ok_or(TaskViewFactsError::CountOverflow)?;
            }
        }
        if waiting_approval_actions != u32::from(pending_action.is_some()) {
            return Err(TaskViewFactsError::PendingActionCountMismatch);
        }

        let pending_permission = self
            .pending_permission()
            .map(|request| PendingPermissionFacts {
                request_id: request.request_id().as_str().to_owned(),
                permission: request.permission(),
                deadline_monotonic_ms: request.deadline_monotonic_ms(),
                deadline_utc_ms: request.deadline_utc_ms(),
                browser_session_id: request.browser_session_id().as_str().to_owned(),
            });
        if pending_permission.is_some() && task.state() != TaskState::WaitingUser {
            return Err(TaskViewFactsError::PendingPermissionOutsideWait);
        }

        let pending_handover = self
            .pending_handover()
            .map(|handover_id| handover_id.as_str().to_owned());
        if pending_handover.is_some() && task.state() != TaskState::WaitingUser {
            return Err(TaskViewFactsError::PendingHandoverOutsideWait);
        }

        let pending_field_values = self
            .pending_field_values()
            .map(|request_id| request_id.as_str().to_owned());
        if pending_field_values.is_some() && task.state() != TaskState::WaitingUser {
            return Err(TaskViewFactsError::PendingFieldValuesOutsideWait);
        }

        let terminal_failure = task.terminal_failure();
        match (task.state(), terminal_failure) {
            (TaskState::Failed, None) => return Err(TaskViewFactsError::MissingTerminalFailure),
            (TaskState::Failed, Some(_)) | (_, None) => {}
            (_, Some(_)) => return Err(TaskViewFactsError::UnexpectedTerminalFailure),
        }
        Ok(TaskViewFacts {
            task_id: task.task_id().as_str().to_owned(),
            revision: task.revision(),
            state: task.state(),
            execution_phase: task.execution_phase(),
            goal: task.user_goal().to_owned(),
            template_id: task.snapshot().template_id,
            workspace_id: task.workspace_id().map(|id| id.to_text()),
            plan_progress,
            terminal_failure,
            pause_cause: self.pause_cause(),
            model_attempt: self.model_attempt_in_flight(),
            last_move_refused: self.last_move_refused(),
            reply_being_reasked: self.reply_being_reasked(),
            pending_action,
            pending_permission,
            accepted_consent: browser_facts::accepted_consent(self)?,
            committed_action_approvals: browser_facts::committed_approvals(self)?,
            outcome_unknown_actions,
            artifacts: self
                .artifacts()
                .map(|artifact| TaskArtifactFacts {
                    artifact_id: artifact.artifact_id().as_str().to_owned(),
                    kind: artifact.kind(),
                    workspace_revision: artifact.workspace_revision(),
                    accepted: task.accepted_artifacts().contains(artifact.artifact_id()),
                })
                .collect(),
            activity: task
                .activity()
                .steps()
                .iter()
                .map(|step| TaskActivityFacts {
                    sequence: step.sequence,
                    kind: step.kind,
                    host: step.host.clone(),
                    count: step.count,
                    at_epoch_ms: step.at.0,
                })
                .collect(),
            allowed_controls: self.allowed_task_controls(),
            pending_handover,
            pending_field_values,
        })
    }

    fn next_reviewed_command(
        &self,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::WorkflowError> {
        task_engine::Reducer::next_reviewed_command(self, digest)
    }

    fn next_procedure_command(
        &self,
        procedure: &procedure_engine::Procedure,
        observed: procedure_engine::ObservedFields<'_>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, procedure_engine::ReplayRefusal> {
        procedure_engine::next_procedure_command(self, procedure, observed, digest)
    }

    fn next_procedure_command_with_page(
        &self,
        procedure: &procedure_engine::Procedure,
        observed: procedure_engine::ObservedFields<'_>,
        page: Option<procedure_engine::ReplayPage<'_>>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, procedure_engine::ReplayRefusal> {
        procedure_engine::next_procedure_command_with_page(self, procedure, observed, page, digest)
    }

    fn browser_session_id(&self) -> Option<&task_engine::BrowserSessionId> {
        Some(&self.task().snapshot().browser_session_id)
    }

    fn skill_version_id(&self) -> Option<&str> {
        self.task()
            .snapshot()
            .skill_version_id
            .as_ref()
            .map(|value| value.0.as_str())
    }

    fn builtin_skill_reference(&self) -> Option<task_engine::BuiltinSkillReference> {
        self.task().snapshot().builtin_skill
    }

    fn builtin_skill_binding_facts(&self) -> Option<BuiltinSkillBindingFacts> {
        let snapshot = self.task().snapshot();
        Some(BuiltinSkillBindingFacts {
            reference: snapshot.builtin_skill?,
            template_id: snapshot.template_id,
            tool_allowlist: snapshot.tool_allowlist.clone(),
            milestone: snapshot.milestone,
            initial_source_count: snapshot.consented_sources.len(),
            source_discovery_enabled: snapshot.source_discovery_enabled,
            remaining_new_source_cap: snapshot.remaining_new_source_cap,
        })
    }

    fn library_refresh_context(&self) -> Option<task_engine::LibraryRefreshContext> {
        self.task().snapshot().library_refresh.clone()
    }

    fn model_turn_facts(&self) -> ModelTurnFacts {
        model_facts::model_turn_facts(self)
    }

    fn refused_on_sight(
        &self,
        residency: &task_engine::TurnResidency,
    ) -> Vec<(u32, task_engine::NotAttempted)> {
        self.turn_dispositions(residency)
            .into_iter()
            .filter_map(|disposition| match disposition.verdict {
                task_engine::CallVerdict::NotAttempted(reason) => {
                    Some((disposition.sequence, reason))
                }
                task_engine::CallVerdict::Attemptable => None,
            })
            .collect()
    }
    fn model_turn_in_flight(&self) -> Option<String> {
        self.model_turn()
            .filter(|turn| turn.phase().is_in_flight())
            .map(|turn| turn.call_id().as_str().to_owned())
    }

    fn pre_model_observation_sources(&self) -> Vec<task_engine::ConsentedSource> {
        let task = self.task();
        task.consented_sources()
            .iter()
            .filter(|source| task.scope().included().contains(&source.source_id))
            .cloned()
            .collect()
    }

    fn next_pre_model_observation(
        &self,
        live_observations: &[task_engine::LiveSourceObservation],
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<task_engine::PreModelObservation, task_engine::AgentError> {
        task_engine::Reducer::next_pre_model_observation(self, live_observations, digest)
    }

    fn next_agent_command(
        &self,
        residency: Option<&task_engine::TurnResidency>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::AgentError> {
        task_engine::Reducer::next_agent_command(self, residency, digest)
    }

    fn pending_loop_call<'a>(
        &self,
        residency: &'a task_engine::TurnResidency,
    ) -> Option<(
        u32,
        &'a task_engine::ModelToolCall,
        &'static task_engine::ToolEntry,
    )> {
        task_engine::Reducer::pending_loop_call(self, residency)
    }

    fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal> {
        task_engine::Reducer::apply(self, envelope)
    }
}
