// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic, provider-free execution of one approved Library refresh.

use bip_types::identity::{ContentDigest, DigestAlgorithm, TabId};

use super::{WorkflowDigest, WorkflowError, REVIEWED_NO_MODEL_ROUTE_ID};
use crate::action::{ActionIntent, ActionProposal, ActionState, BrowserIntent};
use crate::command::Command;
use crate::plan::{PlanDraft, PlanStatus, StepDraft, StepKind, StepState};
use crate::proposal::{hex_digest, plan_action_material, plan_step_key};
use crate::reducer::Reducer;
use crate::task::{
    GapReason, LibraryRefreshContext, LibraryRefreshSource, TaskResult, TaskState, TaskTemplateId,
    UnmetRequirement,
};
use crate::{Clock, ConsentedSource, IdSource};

const REFRESH_EFFECT_NAMESPACE: &str = "library-refresh";
const NAVIGATION_TOOL: &str = "browser.navigate";

impl<C: Clock, I: IdSource> Reducer<C, I> {
    pub(super) fn next_library_refresh_command(
        &self,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        let refresh = self.refresh_context()?;
        match self.task().state() {
            TaskState::Queued => Ok(Some(Command::ExecutorStarted)),
            TaskState::Running => self.next_running_refresh_command(refresh, digest),
            TaskState::Completing => self.refresh_result_command(refresh).map(Some),
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

    fn refresh_context(&self) -> Result<&LibraryRefreshContext, WorkflowError> {
        let snapshot = self.task().snapshot();
        let Some(refresh) = snapshot.library_refresh.as_ref() else {
            return Err(WorkflowError::InvalidWorkflowConfiguration);
        };
        if snapshot.template_id != TaskTemplateId::WebErrand
            || !refresh.is_well_formed()
            || !snapshot.consented_sources.is_empty()
            || !snapshot.source_discovery_enabled
            || snapshot
                .provider_route
                .as_ref()
                .map(crate::ProviderRouteId::as_str)
                != Some(REVIEWED_NO_MODEL_ROUTE_ID)
            || ![super::REVIEWED_OBSERVATION_TOOL, NAVIGATION_TOOL]
                .iter()
                .all(|required| snapshot.tool_allowlist.iter().any(|tool| tool == required))
        {
            return Err(WorkflowError::InvalidWorkflowConfiguration);
        }
        Ok(refresh)
    }

    fn next_running_refresh_command(
        &self,
        refresh: &LibraryRefreshContext,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        let Some(plan) = self.plan() else {
            return Ok(Some(Command::SetPlan(refresh_plan(refresh))));
        };
        if plan.status() == PlanStatus::Superseded {
            return Ok(Some(Command::SetPlan(refresh_plan(refresh))));
        }
        let expected_steps = refresh.sources.len().saturating_mul(2);
        if plan.steps().len() != expected_steps {
            return Err(WorkflowError::UnexpectedPlan);
        }
        if plan.status() == PlanStatus::Completed {
            return Ok(Some(Command::ResultCandidateReady));
        }
        if plan.status() != PlanStatus::Active {
            return Err(WorkflowError::UnexpectedPlan);
        }
        let Some((index, step)) = plan
            .steps()
            .iter()
            .enumerate()
            .find(|(_, step)| !step.state().is_final())
        else {
            return Err(WorkflowError::UnexpectedPlan);
        };
        let source = refresh
            .sources
            .get(index / 2)
            .ok_or(WorkflowError::UnexpectedPlan)?;
        let expected_kind = if index % 2 == 0 {
            StepKind::Navigate
        } else {
            StepKind::Observe
        };
        if step.kind() != expected_kind {
            return Err(WorkflowError::UnexpectedPlan);
        }
        if expected_kind == StepKind::Observe
            && step.state() == StepState::Ready
            && index
                .checked_sub(1)
                .and_then(|prior| plan.steps().get(prior))
                .is_some_and(|prior| prior.state() == StepState::Skipped)
        {
            return Ok(Some(Command::AdvanceStep {
                plan_step_id: step.plan_step_id().clone(),
                to: StepState::Skipped,
            }));
        }
        match step.state() {
            StepState::Ready => Ok(Some(Command::AdvanceStep {
                plan_step_id: step.plan_step_id().clone(),
                to: StepState::Running,
            })),
            StepState::Running if expected_kind == StepKind::Navigate => {
                self.next_refresh_navigation(source, step.plan_step_id(), digest)
            }
            StepState::Running => self.next_refresh_observation(step.plan_step_id(), digest),
            StepState::Pending => Ok(None),
            StepState::Waiting
            | StepState::Succeeded
            | StepState::Skipped
            | StepState::Failed
            | StepState::Cancelled
            | StepState::Superseded => Err(WorkflowError::UnexpectedPlan),
        }
    }

    fn next_refresh_navigation(
        &self,
        source: &LibraryRefreshSource,
        step_id: &crate::PlanStepId,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        let Some(tab) = self.task().snapshot().discovery_tab_id.as_ref() else {
            return Ok(None);
        };
        let actions = self.step_actions(step_id);
        if actions
            .iter()
            .any(|action| action.state() == ActionState::Verified)
        {
            return Ok(Some(Command::AdvanceStep {
                plan_step_id: step_id.clone(),
                to: StepState::Succeeded,
            }));
        }
        if has_pending_action(&actions) {
            return Ok(None);
        }
        if actions
            .iter()
            .any(|action| is_terminal_failure(action.state()))
        {
            return Ok(Some(Command::AdvanceStep {
                plan_step_id: step_id.clone(),
                to: StepState::Skipped,
            }));
        }
        if !actions.is_empty() {
            return Err(WorkflowError::UnexpectedActionState);
        }
        let proposal_source = proposal_source(source, tab)?;
        let intent = ActionIntent::Browser(BrowserIntent::Navigate {
            tab: tab.clone(),
            address: source.canonical_locator.clone(),
            new_tab: false,
        });
        self.refresh_proposal(&proposal_source, step_id, intent, digest)
            .map(Some)
    }

    fn next_refresh_observation(
        &self,
        step_id: &crate::PlanStepId,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        let actions = self.step_actions(step_id);
        if actions
            .iter()
            .any(|action| action.state() == ActionState::Verified && action.observation().is_some())
        {
            return Ok(Some(Command::AdvanceStep {
                plan_step_id: step_id.clone(),
                to: StepState::Succeeded,
            }));
        }
        if has_pending_action(&actions) {
            return Ok(None);
        }
        if actions
            .iter()
            .any(|action| is_terminal_failure(action.state()))
        {
            return Ok(Some(Command::AdvanceStep {
                plan_step_id: step_id.clone(),
                to: StepState::Skipped,
            }));
        }
        if !actions.is_empty() {
            return Err(WorkflowError::UnexpectedActionState);
        }
        let tab = self
            .task()
            .snapshot()
            .discovery_tab_id
            .as_ref()
            .ok_or(WorkflowError::InvalidSourceScope)?;
        let source = self
            .task()
            .consented_sources()
            .iter()
            .find(|source| source.tab_id == *tab)
            .ok_or(WorkflowError::InvalidSourceScope)?;
        let intent = ActionIntent::Browser(BrowserIntent::DomRead {
            tab: tab.clone(),
            target: None,
        });
        self.refresh_proposal(source, step_id, intent, digest)
            .map(Some)
    }

    fn refresh_proposal(
        &self,
        source: &ConsentedSource,
        step_id: &crate::PlanStepId,
        intent: ActionIntent,
        digest: &dyn WorkflowDigest,
    ) -> Result<Command, WorkflowError> {
        let key = plan_step_key(REFRESH_EFFECT_NAMESPACE, source, step_id, 1);
        let material = plan_action_material(
            REFRESH_EFFECT_NAMESPACE,
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
        Ok(Command::ProposeAction(Box::new(ActionProposal::new(
            intent,
            Some(step_id.clone()),
            key,
            false,
            None,
            proposal_digest,
        ))))
    }

    fn step_actions(&self, step_id: &crate::PlanStepId) -> Vec<&crate::ActionRecord> {
        self.actions()
            .filter(|action| action.proposal().plan_step_id.as_ref() == Some(step_id))
            .collect()
    }

    fn refresh_result_command(
        &self,
        refresh: &LibraryRefreshContext,
    ) -> Result<Command, WorkflowError> {
        let Some(plan) = self.plan() else {
            return Err(WorkflowError::UnexpectedPlan);
        };
        let observed = plan
            .steps()
            .iter()
            .filter(|step| step.kind() == StepKind::Observe && step.state() == StepState::Succeeded)
            .count();
        let source_count = u64::try_from(observed).unwrap_or(u64::MAX);
        let complete = observed == refresh.sources.len();
        let result = TaskResult {
            artifact_ids: Vec::new(),
            unmet: if complete {
                Vec::new()
            } else {
                vec![UnmetRequirement {
                    subject: "saved source refresh".to_owned(),
                    reason: GapReason::NotFoundInScope,
                }]
            },
            fact_count: source_count.saturating_mul(crate::ObservationGraphSummary::FACT_COUNT),
            source_count,
        };
        Ok(if complete {
            Command::CompleteResultValidated(result)
        } else {
            Command::PartialResultValidated(result)
        })
    }
}

fn refresh_plan(refresh: &LibraryRefreshContext) -> PlanDraft {
    let mut steps = Vec::with_capacity(refresh.sources.len().saturating_mul(2));
    for index in 0..refresh.sources.len() {
        let navigation_index = index.saturating_mul(2);
        let dependency = navigation_index.checked_sub(1).into_iter().collect();
        steps.push(StepDraft {
            kind: StepKind::Navigate,
            description: format!("Open saved source {}", index.saturating_add(1)),
            dependencies: dependency,
        });
        steps.push(StepDraft {
            kind: StepKind::Observe,
            description: format!("Check saved source {}", index.saturating_add(1)),
            dependencies: vec![navigation_index],
        });
    }
    PlanDraft {
        summary: "Refresh the approved saved sources".to_owned(),
        steps,
    }
}

fn proposal_source(
    source: &LibraryRefreshSource,
    tab: &TabId,
) -> Result<ConsentedSource, WorkflowError> {
    let (scheme, rest) = source
        .canonical_locator
        .split_once("://")
        .ok_or(WorkflowError::InvalidWorkflowConfiguration)?;
    let authority = rest
        .split('/')
        .next()
        .ok_or(WorkflowError::InvalidWorkflowConfiguration)?;
    Ok(ConsentedSource {
        source_id: source.source_id,
        tab_id: tab.clone(),
        normalized_origin: format!("{scheme}://{authority}"),
        canonical_locator: Some(source.canonical_locator.clone()),
    })
}

fn has_pending_action(actions: &[&crate::ActionRecord]) -> bool {
    actions.iter().any(|action| {
        matches!(
            action.state(),
            ActionState::Proposed
                | ActionState::WaitingApproval
                | ActionState::Authorized
                | ActionState::Dispatching
                | ActionState::Verifying
        )
    })
}

const fn is_terminal_failure(state: ActionState) -> bool {
    matches!(
        state,
        ActionState::Rejected
            | ActionState::Failed
            | ActionState::Cancelled
            | ActionState::OutcomeUnknown
    )
}
