// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Explicit handover completion is bound to this saved step's own identity.

use crate::{Procedure, ReplayRefusal};
use task_engine::handover::HandoverId;
use task_engine::plan::{PlanStep, StepState};
use task_engine::{Clock, Command, IdSource, Reducer};

pub(super) fn explicit_handover<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    step: &PlanStep,
) -> Result<Option<Command>, ReplayRefusal> {
    let request = hand_over(procedure, step, "saved-step")?;
    let Command::RequestHandover { handover_id } = &request else {
        return Err(ReplayRefusal::UnexpectedActionState);
    };
    let completed=reducer.journal().commands().any(|record| matches!(&record.envelope.command,Command::CompleteHandover(completion) if completion.handover_id()==handover_id));
    Ok(Some(if completed {
        super::super::advance(step, StepState::Succeeded)
    } else {
        request
    }))
}

/// The page goes back to the person, under an identity a repeat re-opens.
///
/// Derived from the record and the step rather than minted, for the reason
/// `task_engine::handover` gives about every handover identity: a replay has
/// to reach the same handover instead of opening a second one. The reason is
/// part of it because an identifier must name what it is about, and the two
/// reasons a fill is handed over — the field cannot be classified, or it is
/// now something else — are two different things to tell a person. It is also
/// the only place [`crate::field::HandoverReason`] can be seen from, because
/// `Command::RequestHandover` carries an identity and nothing else: what the
/// person is shown is composed from a trusted local template, and this module
/// holds no text to compose it from.
pub(super) fn hand_over(
    procedure: &Procedure,
    plan_step: &PlanStep,
    reason: &str,
) -> Result<Command, ReplayRefusal> {
    let handover_id = HandoverId::new(format!(
        "{}-handover-{}-{reason}",
        procedure.id.as_str(),
        plan_step.plan_step_id().as_str()
    ))
    .map_err(|_| ReplayRefusal::HandoverIdentityOverflow)?;
    Ok(Command::RequestHandover { handover_id })
}
