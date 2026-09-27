// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The commands that build and advance a plan.
//!
//! A plan explains and authorizes nothing (`crate::plan`), so this module is
//! deliberately small: it builds a revision, supersedes the one before it, and
//! moves a step. The one rule worth naming is the order — the new plan is
//! built *before* the old one is superseded, so a rejected draft cannot leave
//! the task without the plan it was working from.

use super::outcome::Outcome;
use super::Reducer;
use crate::command::CommandKind;
use crate::event::{EventKind, EventSubject, TaskEvent};
use crate::ids::{IdSource, PlanStepId};
use crate::plan::{Plan, PlanDraft, StepState};
use crate::time::Clock;
use crate::transition::RefusalReason;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Installs a new plan revision, superseding the previous one.
    pub(super) fn on_set_plan(
        &mut self,
        draft: &PlanDraft,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let next_revision = self.plan_revision.saturating_add(1);
        let plan = Plan::from_draft(
            draft,
            &self.task.task_id,
            next_revision,
            self.task.revision,
            &mut self.ids,
        )
        .map_err(|_| RefusalReason::PlanRejected)?;
        self.plan_revision = next_revision;
        let superseded = self.plan.as_mut().map(|previous| {
            previous.supersede();
            previous.plan_id().clone()
        });
        let mut events = Vec::new();
        if let Some(plan_id) = superseded {
            events.push(
                TaskEvent::record(EventKind::PlanSuperseded, kind)
                    .about(EventSubject::Plan(plan_id)),
            );
        }
        events.push(
            TaskEvent::record(EventKind::PlanCreated, kind)
                .about(EventSubject::Plan(plan.plan_id().clone())),
        );
        self.plan = Some(plan);
        Ok(Outcome::recorded(events, Vec::new()))
    }

    /// Moves one step of the current plan.
    pub(super) fn on_advance_step(
        &mut self,
        plan_step_id: &PlanStepId,
        to: StepState,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let moved = self
            .plan
            .as_mut()
            .is_some_and(|plan| plan.move_step(plan_step_id, to));
        if !moved {
            return Err(RefusalReason::StepCannotMove);
        }
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::PlanStepAdvanced, kind)
                .about(EventSubject::PlanStep(plan_step_id.clone()))],
            Vec::new(),
        ))
    }
}
