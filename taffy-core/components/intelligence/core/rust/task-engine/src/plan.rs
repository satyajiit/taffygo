// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Plans and plan steps (domain model section 11).
//!
//! # A plan has no authority
//!
//! It is explanatory and revisable. Superseding one changes nothing about the
//! actions taken under it, and nothing here can widen a scope, grant a
//! capability, or authorize an effect. The only thing a step's state decides is
//! what the runtime does next.
//!
//! # A successful step is not a successful action
//!
//! Section 11.2 says a successful plan step does not imply every underlying
//! action succeeded, so [`Plan::may_succeed`] checks the step's required
//! actions against their verified outcomes instead of trusting the caller.

use std::collections::{BTreeMap, BTreeSet};

use bip_types::identity::{ActionId, TaskId};

use crate::ids::{PlanId, PlanStepId};

/// What a step does.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum StepKind {
    /// Take a bounded observation.
    Observe,
    /// Move a task tab to a source.
    Navigate,
    /// Pull typed values out of an observation.
    Extract,
    /// Ask a model.
    Infer,
    /// Stop and ask the user.
    AskUser,
    /// Generate an artifact.
    Export,
}

impl StepKind {
    /// Every kind, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Observe,
        Self::Navigate,
        Self::Extract,
        Self::Infer,
        Self::AskUser,
        Self::Export,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Observe => "observe",
            Self::Navigate => "navigate",
            Self::Extract => "extract",
            Self::Infer => "infer",
            Self::AskUser => "ask_user",
            Self::Export => "export",
        }
    }
}

/// A plan step's state (domain model section 11.2).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum StepState {
    /// Waiting on a dependency.
    Pending,
    /// Every dependency has succeeded.
    Ready,
    /// Being worked on.
    Running,
    /// Stopped on something outside the runtime.
    Waiting,
    /// Finished, with its required outcomes verified.
    Succeeded,
    /// Deliberately not done.
    Skipped,
    /// Finished without its required outcomes.
    Failed,
    /// Ended because the task ended.
    Cancelled,
    /// Replaced by a later plan revision.
    Superseded,
}

impl StepState {
    /// Every state, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Pending,
        Self::Ready,
        Self::Running,
        Self::Waiting,
        Self::Succeeded,
        Self::Skipped,
        Self::Failed,
        Self::Cancelled,
        Self::Superseded,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Pending => "pending",
            Self::Ready => "ready",
            Self::Running => "running",
            Self::Waiting => "waiting",
            Self::Succeeded => "succeeded",
            Self::Skipped => "skipped",
            Self::Failed => "failed",
            Self::Cancelled => "cancelled",
            Self::Superseded => "superseded",
        }
    }

    /// Whether the step has finished, whatever the outcome.
    pub const fn is_final(self) -> bool {
        matches!(
            self,
            Self::Succeeded | Self::Skipped | Self::Failed | Self::Cancelled | Self::Superseded
        )
    }

    /// Whether a dependent step may become ready because of this one.
    pub const fn satisfies_dependency(self) -> bool {
        matches!(self, Self::Succeeded | Self::Skipped)
    }
}

/// A plan's lifecycle (domain model section 11.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PlanStatus {
    /// Being composed.
    Draft,
    /// The plan the task is working from.
    Active,
    /// Replaced by a later revision.
    Superseded,
    /// Every step reached a final state and the task finished.
    Completed,
    /// Given up on.
    Abandoned,
}

impl PlanStatus {
    /// Every durable plan state, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Draft,
        Self::Active,
        Self::Superseded,
        Self::Completed,
        Self::Abandoned,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Draft => "draft",
            Self::Active => "active",
            Self::Superseded => "superseded",
            Self::Completed => "completed",
            Self::Abandoned => "abandoned",
        }
    }
}

/// One step of a plan (domain model section 11.2).
#[allow(clippy::struct_field_names)]
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlanStep {
    pub(crate) plan_step_id: PlanStepId,
    pub(crate) kind: StepKind,
    pub(crate) description: String,
    pub(crate) state: StepState,
    pub(crate) action_ids: Vec<ActionId>,
    pub(crate) required_action_ids: Vec<ActionId>,
    pub(crate) dependencies: Vec<PlanStepId>,
}

impl PlanStep {
    /// Identity.
    pub const fn plan_step_id(&self) -> &PlanStepId {
        &self.plan_step_id
    }

    /// What the step does.
    pub const fn kind(&self) -> StepKind {
        self.kind
    }

    /// The step's own words, written by the runtime rather than by a page.
    pub fn description(&self) -> &str {
        &self.description
    }

    /// Where the step is.
    pub const fn state(&self) -> StepState {
        self.state
    }

    /// The actions taken under it.
    pub fn action_ids(&self) -> &[ActionId] {
        &self.action_ids
    }

    /// The actions that must be verified before the step may succeed.
    pub fn required_action_ids(&self) -> &[ActionId] {
        &self.required_action_ids
    }

    /// The steps this one waits on.
    pub fn dependencies(&self) -> &[PlanStepId] {
        &self.dependencies
    }
}

/// A step as a caller describes it before the plan mints identifiers.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StepDraft {
    /// What the step does.
    pub kind: StepKind,
    /// The step's own words.
    pub description: String,
    /// The indexes, within this draft, of the steps it waits on.
    pub dependencies: Vec<usize>,
}

/// A plan as a caller describes it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlanDraft {
    /// What the plan is for, in the runtime's own words.
    pub summary: String,
    /// The steps, in order.
    pub steps: Vec<StepDraft>,
}

impl PlanDraft {
    /// Whether the draft could become a plan.
    ///
    /// Pure: it mints nothing, so the reducer can check it as a guard before it
    /// touches an identifier source.
    pub fn validate(&self) -> Result<(), PlanError> {
        if self.steps.is_empty() {
            return Err(PlanError::NoSteps);
        }
        for (index, step) in self.steps.iter().enumerate() {
            for dependency in &step.dependencies {
                if *dependency >= self.steps.len() {
                    return Err(PlanError::DependencyOutOfRange);
                }
                if *dependency >= index {
                    return Err(PlanError::DependencyNotEarlier);
                }
            }
        }
        Ok(())
    }
}

/// Why a draft could not become a plan.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PlanError {
    /// A step depends on an index that is not in the draft.
    DependencyOutOfRange,
    /// A step depends on itself or on a later step.
    DependencyNotEarlier,
    /// The identifier source is exhausted.
    IdSourceExhausted,
    /// The draft has no steps.
    NoSteps,
}

impl PlanError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::DependencyOutOfRange => "dependency_out_of_range",
            Self::DependencyNotEarlier => "dependency_not_earlier",
            Self::IdSourceExhausted => "id_source_exhausted",
            Self::NoSteps => "no_steps",
        }
    }
}

/// A plan revision (domain model section 11.1).
#[allow(clippy::struct_field_names)]
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Plan {
    pub(crate) plan_id: PlanId,
    pub(crate) task_id: TaskId,
    pub(crate) plan_revision: u32,
    pub(crate) based_on_task_revision: u64,
    pub(crate) summary: String,
    pub(crate) steps: Vec<PlanStep>,
    pub(crate) status: PlanStatus,
}

impl Plan {
    /// Builds a plan revision from a draft.
    ///
    /// Dependencies must point backwards. A cycle would let a step wait on
    /// itself forever, and a plan that cannot finish is a plan that leaves a
    /// task running with nothing to do.
    pub fn from_draft(
        draft: &PlanDraft,
        task_id: &TaskId,
        plan_revision: u32,
        based_on_task_revision: u64,
        ids: &mut impl crate::ids::IdSource,
    ) -> Result<Self, PlanError> {
        draft.validate()?;
        let mut step_ids: Vec<PlanStepId> = Vec::with_capacity(draft.steps.len());
        for _ in &draft.steps {
            step_ids.push(crate::ids::next_plan_step_id(ids).ok_or(PlanError::IdSourceExhausted)?);
        }
        let mut steps: Vec<PlanStep> = Vec::with_capacity(draft.steps.len());
        for (index, step) in draft.steps.iter().enumerate() {
            let mut dependencies = Vec::with_capacity(step.dependencies.len());
            for dependency in &step.dependencies {
                if *dependency >= draft.steps.len() {
                    return Err(PlanError::DependencyOutOfRange);
                }
                if *dependency >= index {
                    return Err(PlanError::DependencyNotEarlier);
                }
                let id = step_ids
                    .get(*dependency)
                    .ok_or(PlanError::DependencyOutOfRange)?;
                dependencies.push(id.clone());
            }
            let plan_step_id = step_ids
                .get(index)
                .ok_or(PlanError::DependencyOutOfRange)?
                .clone();
            steps.push(PlanStep {
                plan_step_id,
                kind: step.kind,
                description: step.description.clone(),
                state: if dependencies.is_empty() {
                    StepState::Ready
                } else {
                    StepState::Pending
                },
                action_ids: Vec::new(),
                required_action_ids: Vec::new(),
                dependencies,
            });
        }
        let plan_id = crate::ids::next_plan_id(ids).ok_or(PlanError::IdSourceExhausted)?;
        Ok(Self {
            plan_id,
            task_id: task_id.clone(),
            plan_revision,
            based_on_task_revision,
            summary: draft.summary.clone(),
            steps,
            status: PlanStatus::Active,
        })
    }

    /// Identity.
    pub const fn plan_id(&self) -> &PlanId {
        &self.plan_id
    }

    /// The task it belongs to.
    pub const fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    /// Which revision of the plan this is. Revisions count from one.
    pub const fn plan_revision(&self) -> u32 {
        self.plan_revision
    }

    /// The task revision the plan was built against.
    pub const fn based_on_task_revision(&self) -> u64 {
        self.based_on_task_revision
    }

    /// What the plan is for.
    pub fn summary(&self) -> &str {
        &self.summary
    }

    /// The steps, in order.
    pub fn steps(&self) -> &[PlanStep] {
        &self.steps
    }

    /// Where the plan is in its lifecycle.
    pub const fn status(&self) -> PlanStatus {
        self.status
    }

    /// One step by identity.
    pub fn step(&self, plan_step_id: &PlanStepId) -> Option<&PlanStep> {
        self.steps
            .iter()
            .find(|step| step.plan_step_id == *plan_step_id)
    }

    /// Marks the whole plan superseded, leaving every step's history intact.
    pub fn supersede(&mut self) {
        self.status = PlanStatus::Superseded;
        for step in &mut self.steps {
            if !step.state.is_final() {
                step.state = StepState::Superseded;
            }
        }
    }

    /// Records that an action belongs to a step.
    pub fn attach_action(
        &mut self,
        plan_step_id: &PlanStepId,
        action_id: &ActionId,
        required: bool,
    ) -> bool {
        let Some(step) = self
            .steps
            .iter_mut()
            .find(|step| step.plan_step_id == *plan_step_id)
        else {
            return false;
        };
        if !step.action_ids.contains(action_id) {
            step.action_ids.push(action_id.clone());
        }
        if required && !step.required_action_ids.contains(action_id) {
            step.required_action_ids.push(action_id.clone());
        }
        true
    }

    /// Whether a step may move to `to`, given what its actions actually did.
    ///
    /// `verified` answers, for one action, whether its postcondition held. A
    /// step only succeeds when every action it declared required is verified —
    /// the check section 11.2 requires, made by the reducer rather than by the
    /// caller asserting it.
    pub fn may_move(
        &self,
        plan_step_id: &PlanStepId,
        to: StepState,
        verified: &BTreeMap<String, bool>,
    ) -> bool {
        let Some(step) = self.step(plan_step_id) else {
            return false;
        };
        if step.state.is_final() {
            return false;
        }
        if to != StepState::Succeeded {
            return true;
        }
        step.required_action_ids
            .iter()
            .all(|action_id| verified.get(action_id.as_str()).copied().unwrap_or(false))
    }

    /// Moves a step, and readies any step whose dependencies are now satisfied.
    pub fn move_step(&mut self, plan_step_id: &PlanStepId, to: StepState) -> bool {
        let Some(step) = self
            .steps
            .iter_mut()
            .find(|step| step.plan_step_id == *plan_step_id)
        else {
            return false;
        };
        step.state = to;
        self.ready_unblocked_steps();
        if self.steps.iter().all(|step| step.state.is_final()) {
            self.status = PlanStatus::Completed;
        }
        true
    }

    fn ready_unblocked_steps(&mut self) {
        let satisfied: BTreeSet<PlanStepId> = self
            .steps
            .iter()
            .filter(|step| step.state.satisfies_dependency())
            .map(|step| step.plan_step_id.clone())
            .collect();
        for step in &mut self.steps {
            if step.state == StepState::Pending
                && step
                    .dependencies
                    .iter()
                    .all(|dependency| satisfied.contains(dependency))
            {
                step.state = StepState::Ready;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Plan, PlanDraft, PlanError, PlanStatus, StepDraft, StepKind, StepState};
    use crate::ids::SequentialIds;
    use bip_types::identity::{ActionId, TaskId};
    use std::collections::BTreeMap;

    fn draft() -> PlanDraft {
        PlanDraft {
            summary: "Compare the two pages".to_owned(),
            steps: vec![
                StepDraft {
                    kind: StepKind::Observe,
                    description: "Read the first page".to_owned(),
                    dependencies: vec![],
                },
                StepDraft {
                    kind: StepKind::Extract,
                    description: "Pull the values out".to_owned(),
                    dependencies: vec![0],
                },
            ],
        }
    }

    fn plan() -> Plan {
        let mut ids = SequentialIds::new();
        Plan::from_draft(&draft(), &TaskId::new("task_1"), 1, 0, &mut ids)
            .unwrap_or_else(|_| unreachable!("the draft is well formed"))
    }

    #[test]
    fn a_step_with_no_dependency_starts_ready_and_a_dependent_one_starts_pending() {
        let plan = plan();
        let states: Vec<StepState> = plan.steps().iter().map(super::PlanStep::state).collect();
        assert_eq!(states, vec![StepState::Ready, StepState::Pending]);
    }

    #[test]
    fn a_dependency_that_points_forward_is_refused() {
        let mut ids = SequentialIds::new();
        let mut forward = draft();
        if let Some(step) = forward.steps.first_mut() {
            step.dependencies = vec![1];
        }
        assert_eq!(
            Plan::from_draft(&forward, &TaskId::new("task_1"), 1, 0, &mut ids),
            Err(PlanError::DependencyNotEarlier)
        );
    }

    #[test]
    fn a_step_succeeds_only_when_every_required_action_is_verified() {
        let mut plan = plan();
        let step_id = plan.steps().first().map_or_else(
            || unreachable!("the plan has steps"),
            |step| step.plan_step_id().clone(),
        );
        let action = ActionId::new("act_0");
        assert!(plan.attach_action(&step_id, &action, true));

        let mut verified = BTreeMap::new();
        verified.insert(action.as_str().to_owned(), false);
        assert!(!plan.may_move(&step_id, StepState::Succeeded, &verified));

        verified.insert(action.as_str().to_owned(), true);
        assert!(plan.may_move(&step_id, StepState::Succeeded, &verified));
    }

    #[test]
    fn a_succeeding_step_readies_the_one_that_waited_on_it() {
        let mut plan = plan();
        let first = plan.steps().first().map_or_else(
            || unreachable!("the plan has steps"),
            |step| step.plan_step_id().clone(),
        );
        assert!(plan.move_step(&first, StepState::Succeeded));
        assert_eq!(
            plan.steps().get(1).map(super::PlanStep::state),
            Some(StepState::Ready)
        );
    }

    #[test]
    fn superseding_a_plan_leaves_finished_steps_alone() {
        let mut plan = plan();
        let first = plan.steps().first().map_or_else(
            || unreachable!("the plan has steps"),
            |step| step.plan_step_id().clone(),
        );
        assert!(plan.move_step(&first, StepState::Succeeded));
        plan.supersede();
        assert_eq!(plan.status(), PlanStatus::Superseded);
        assert_eq!(
            plan.steps().first().map(super::PlanStep::state),
            Some(StepState::Succeeded)
        );
        assert_eq!(
            plan.steps().get(1).map(super::PlanStep::state),
            Some(StepState::Superseded)
        );
    }
}
