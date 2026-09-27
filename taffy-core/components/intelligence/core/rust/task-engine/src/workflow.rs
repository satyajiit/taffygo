// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one reviewed local workflow that needs no model provider.
//!
//! `BuildSourceTable` observes one explicitly consented live source and turns
//! the strictly decoded structural evidence into a result. Every returned
//! value is still a reducer command: the runtime must commit it through the
//! browser-owned storage writer before asking for the next one.

use bip_types::identity::{ContentDigest, DigestAlgorithm};

use crate::action::{ActionIntent, ActionProposal, ActionState, BrowserIntent};
use crate::command::Command;
use crate::plan::{PlanDraft, PlanStatus, StepKind, StepState};
use crate::proposal::{hex_digest, plan_action_material, plan_step_key};
use crate::reducer::Reducer;
use crate::task::{FailureReason, TaskResult, TaskState, TaskTemplateId};
use crate::{Clock, IdSource};

mod refresh;

/// The portable tool this reviewed local workflow proposes, and so the one
/// name its allowlist has to contain. It is not the only name the allowlist
/// may carry — see `Reducer::reviewed_source` and decision 0057.
pub const REVIEWED_OBSERVATION_TOOL: &str = "browser.dom.read";
/// Exact route identifier projected from the closed `NO_MODEL_REQUIRED` wire member.
pub const REVIEWED_NO_MODEL_ROUTE_ID: &str = "no_model_required";
/// What this sequence's effects are about, as
/// [`crate::proposal::plan_step_key`] names them.
///
/// A stored procedure that reproduces this workflow passes its own identifier
/// in the same position, so a built-in whose identifier is this string mints
/// the same key for the same read. That is not a coincidence to be tidied
/// away: it is what stops a task that started under this path and finished
/// under the procedure path reading its source a second time. The built-in
/// asserts the equality rather than assuming it.
pub const REVIEWED_EFFECT_NAMESPACE: &str = "source-table";
const MAX_READ_ATTEMPTS: usize = 2;

/// Narrow digest dependency used only to bind one exact proposal.
pub trait WorkflowDigest {
    /// SHA-256 over the bounded canonical proposal material.
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], WorkflowError>;
}

/// Why the reviewed workflow could not truthfully advance.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WorkflowError {
    /// The local digest adapter was unavailable.
    DigestUnavailable,
    /// The template did not retain exactly one accepted source in scope.
    InvalidSourceScope,
    /// The reviewed provider/tool configuration was changed or lost.
    InvalidWorkflowConfiguration,
    /// A plan with an unexpected shape replaced the reviewed plan.
    UnexpectedPlan,
    /// Durable action state contradicted the reviewed workflow.
    UnexpectedActionState,
    /// Result evidence was absent after the extraction step completed.
    MissingObservationEvidence,
    /// The template this workflow runs describes no reviewed plan.
    MissingTemplatePlan,
    /// Bounded canonical proposal material overflowed.
    ProposalEncodingOverflow,
}

impl WorkflowError {
    /// Content-free diagnostic spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::DigestUnavailable => "digest_unavailable",
            Self::InvalidSourceScope => "invalid_source_scope",
            Self::InvalidWorkflowConfiguration => "invalid_workflow_configuration",
            Self::UnexpectedPlan => "unexpected_plan",
            Self::UnexpectedActionState => "unexpected_action_state",
            Self::MissingObservationEvidence => "missing_observation_evidence",
            Self::MissingTemplatePlan => "missing_template_plan",
            Self::ProposalEncodingOverflow => "proposal_encoding_overflow",
        }
    }
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Returns the next replayable command for the reviewed local workflow.
    ///
    /// `None` means either another committed effect/user decision is pending,
    /// the task is terminal, or a different reviewed template owns the task.
    pub fn next_reviewed_command(
        &self,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        if self.task().snapshot().library_refresh.is_some() {
            return self.next_library_refresh_command(digest);
        }
        if self.task().snapshot().template_id != TaskTemplateId::BuildSourceTable {
            return Ok(None);
        }
        let source = self.reviewed_source()?;
        match self.task().state() {
            TaskState::Queued => Ok(Some(Command::ExecutorStarted)),
            TaskState::Running => self.next_running_command(source, digest),
            TaskState::Completing => self.result_command().map(Some),
            TaskState::Draft
            | TaskState::AwaitingConsent
            | TaskState::WaitingUser
            | TaskState::Pausing
            | TaskState::Paused
            | TaskState::Cancelling
            | TaskState::Cancelled
            | TaskState::Completed
            | TaskState::Partial
            | TaskState::Failed => Ok(None),
        }
    }

    /// The one source this workflow observes, once its configuration is the
    /// one it was reviewed under.
    ///
    /// The allowlist clause asks that [`REVIEWED_OBSERVATION_TOOL`] is in the
    /// list, not that it is the whole list (decision 0057). What this workflow
    /// needs is its own tool; a name beside it is a name this workflow never
    /// proposes, and refusing the task over it would be this file holding an
    /// opinion about the tool vocabulary, which lives in `tool::REGISTRY` and
    /// is narrowed again by the policy engine.
    ///
    /// An empty list is still refused, and not as a formality: guard
    /// evaluation reads an empty allowlist as everything the milestone has
    /// reached, so `[]` is the one value that would silently widen the task
    /// rather than narrow it.
    fn reviewed_source(&self) -> Result<&crate::task::ConsentedSource, WorkflowError> {
        let snapshot = self.task().snapshot();
        let Some(source) = snapshot.consented_sources.first() else {
            return Err(WorkflowError::InvalidSourceScope);
        };
        if snapshot.consented_sources.len() != 1
            || snapshot.source_discovery_enabled
            || self.task().scope().included() != [source.source_id]
            || snapshot
                .provider_route
                .as_ref()
                .map(crate::ProviderRouteId::as_str)
                != Some(REVIEWED_NO_MODEL_ROUTE_ID)
            || snapshot.tool_allowlist.is_empty()
            || !snapshot
                .tool_allowlist
                .iter()
                .any(|tool| tool == REVIEWED_OBSERVATION_TOOL)
        {
            return Err(WorkflowError::InvalidWorkflowConfiguration);
        }
        Ok(source)
    }

    fn next_running_command(
        &self,
        source: &crate::task::ConsentedSource,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        let Some(plan) = self.plan() else {
            return Ok(Some(Command::SetPlan(reviewed_plan()?)));
        };
        if plan.status() == PlanStatus::Superseded {
            return Ok(Some(Command::SetPlan(reviewed_plan()?)));
        }
        if plan.steps().len() != 2 || plan.status() != PlanStatus::Active {
            if plan.status() == PlanStatus::Completed {
                return Ok(Some(Command::ResultCandidateReady));
            }
            return Err(WorkflowError::UnexpectedPlan);
        }
        let observe = plan.steps().first().ok_or(WorkflowError::UnexpectedPlan)?;
        let extract = plan.steps().get(1).ok_or(WorkflowError::UnexpectedPlan)?;
        if observe.kind() != StepKind::Observe
            || extract.kind() != StepKind::Extract
            || extract.dependencies() != [observe.plan_step_id().clone()]
        {
            return Err(WorkflowError::UnexpectedPlan);
        }
        match observe.state() {
            StepState::Ready => Ok(Some(Command::AdvanceStep {
                plan_step_id: observe.plan_step_id().clone(),
                to: StepState::Running,
            })),
            StepState::Running => {
                self.next_observation_command(source, observe.plan_step_id(), digest)
            }
            StepState::Succeeded => self.next_extract_command(extract),
            StepState::Pending
            | StepState::Waiting
            | StepState::Skipped
            | StepState::Failed
            | StepState::Cancelled
            | StepState::Superseded => Err(WorkflowError::UnexpectedPlan),
        }
    }

    fn next_observation_command(
        &self,
        source: &crate::task::ConsentedSource,
        step_id: &crate::PlanStepId,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        let actions: Vec<_> = self
            .actions()
            .filter(|action| action.proposal().plan_step_id.as_ref() == Some(step_id))
            .collect();
        if actions
            .iter()
            .any(|action| action.state() == ActionState::Verified && action.observation().is_some())
        {
            return Ok(Some(Command::AdvanceStep {
                plan_step_id: step_id.clone(),
                to: StepState::Succeeded,
            }));
        }
        if actions.iter().any(|action| {
            matches!(
                action.state(),
                ActionState::Proposed
                    | ActionState::WaitingApproval
                    | ActionState::Authorized
                    | ActionState::Dispatching
                    | ActionState::Verifying
            )
        }) {
            return Ok(None);
        }
        if actions.iter().any(|action| {
            matches!(
                action.state(),
                ActionState::Rejected | ActionState::Cancelled
            )
        }) {
            return Ok(Some(Command::FailTask {
                reason: FailureReason::SourcesUnavailable,
            }));
        }
        if actions.len() >= MAX_READ_ATTEMPTS {
            return Ok(Some(Command::FailTask {
                reason: FailureReason::SourcesUnavailable,
            }));
        }
        if actions.iter().any(|action| {
            !matches!(
                action.state(),
                ActionState::Failed | ActionState::OutcomeUnknown
            )
        }) {
            return Err(WorkflowError::UnexpectedActionState);
        }
        let attempt = actions.len().saturating_add(1);
        let key = plan_step_key(REVIEWED_EFFECT_NAMESPACE, source, step_id, attempt);
        let intent = ActionIntent::Browser(BrowserIntent::DomRead {
            tab: source.tab_id.clone(),
            target: None,
        });
        let material = plan_action_material(
            REVIEWED_EFFECT_NAMESPACE,
            self.task().task_id().as_str(),
            source,
            step_id,
            &key,
            &intent,
        )
        .ok_or(WorkflowError::ProposalEncodingOverflow)?;
        let proposal_digest = ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: hex_digest(&digest.sha256(&material)?),
        };
        Ok(Some(Command::ProposeAction(Box::new(ActionProposal::new(
            intent,
            Some(step_id.clone()),
            key,
            // The workflow checks verified typed evidence itself. Keeping the
            // explanatory plan free of a permanently unknown first attempt
            // permits one safe read retry after process death.
            false,
            None,
            proposal_digest,
        )))))
    }

    fn next_extract_command(
        &self,
        extract: &crate::PlanStep,
    ) -> Result<Option<Command>, WorkflowError> {
        match extract.state() {
            StepState::Ready => Ok(Some(Command::AdvanceStep {
                plan_step_id: extract.plan_step_id().clone(),
                to: StepState::Running,
            })),
            StepState::Running => {
                if self.verified_evidence().is_none() {
                    return Err(WorkflowError::MissingObservationEvidence);
                }
                Ok(Some(Command::AdvanceStep {
                    plan_step_id: extract.plan_step_id().clone(),
                    to: StepState::Succeeded,
                }))
            }
            StepState::Succeeded => Ok(Some(Command::ResultCandidateReady)),
            StepState::Pending => Ok(None),
            StepState::Waiting
            | StepState::Skipped
            | StepState::Failed
            | StepState::Cancelled
            | StepState::Superseded => Err(WorkflowError::UnexpectedPlan),
        }
    }

    fn result_command(&self) -> Result<Command, WorkflowError> {
        let evidence = self
            .verified_evidence()
            .ok_or(WorkflowError::MissingObservationEvidence)?;
        let unmet = if evidence.supports_complete_result() {
            Vec::new()
        } else {
            vec![crate::template::incomplete_observation_gap()]
        };
        let result = TaskResult {
            artifact_ids: Vec::new(),
            unmet,
            fact_count: crate::ObservationGraphSummary::FACT_COUNT,
            source_count: 1,
        };
        if result.is_complete() {
            Ok(Command::CompleteResultValidated(result))
        } else {
            Ok(Command::PartialResultValidated(result))
        }
    }

    fn verified_evidence(&self) -> Option<&crate::PageObservationEvidence> {
        self.actions().find_map(|action| {
            (action.state() == ActionState::Verified)
                .then(|| action.observation())
                .flatten()
        })
    }
}

/// The plan this workflow works from, which is the template's rather than
/// this file's.
///
/// See [`crate::template`]: the plan is the task's account of itself in a
/// person's words, and a stored procedure carries no prose at all, so both
/// paths have to read the same one for their `SetPlan` commands to be the same
/// command.
fn reviewed_plan() -> Result<PlanDraft, WorkflowError> {
    crate::template::plan_for(TaskTemplateId::BuildSourceTable)
        .ok_or(WorkflowError::MissingTemplatePlan)
}
