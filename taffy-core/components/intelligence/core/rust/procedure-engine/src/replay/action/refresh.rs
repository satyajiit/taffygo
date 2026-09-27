// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fresh read proposals remain distinct from the saved operation they prepare.

use crate::{Procedure, ReplayRefusal};

/// Browser polling is paced by its download observer; bound even lost updates.
const MAX_DOWNLOAD_POLLS: usize = 300;
use task_engine::action::{ActionIntent, ActionState, BrowserIntent};
use task_engine::plan::PlanStep;
use task_engine::proposal::plan_step_key;
use task_engine::task::{ConsentedSource, FailureReason};
use task_engine::{Clock, Command, IdSource, Reducer, WorkflowDigest};

pub(super) fn page_is_committed<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    step: &PlanStep,
    source: &ConsentedSource,
    page: crate::ReplayPage<'_>,
) -> bool {
    let namespace = format!("{}.observe", procedure.id.as_str());
    let first = plan_step_key(&namespace, source, step.plan_step_id(), 1);
    let second = plan_step_key(&namespace, source, step.plan_step_id(), 2);
    let latest = reducer
        .actions()
        .filter(|action| {
            action.proposal().idempotency_key == first
                || action.proposal().idempotency_key == second
        })
        .max_by_key(|action| action.proposal().idempotency_key == second);
    let matches = |action: &task_engine::action::ActionRecord| {
        action.state() == ActionState::Verified && action.observation() == Some(page.evidence)
    };
    latest.map_or_else(|| reducer.actions().any(matches), matches)
}

pub(crate) fn observe<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    step: &PlanStep,
    source: &ConsentedSource,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    let namespace = format!("{}.observe", procedure.id.as_str());
    let keys = [
        plan_step_key(&namespace, source, step.plan_step_id(), 1),
        plan_step_key(&namespace, source, step.plan_step_id(), 2),
    ];
    let actions: Vec<_> = reducer
        .actions()
        .filter(|action| keys.contains(&action.proposal().idempotency_key))
        .collect();
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
    if actions.len() >= 2
        || actions.iter().any(|action| {
            matches!(
                action.state(),
                ActionState::Rejected | ActionState::Cancelled
            )
        })
    {
        return Ok(Some(Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        }));
    }
    super::proposal_command(
        reducer,
        &namespace,
        step,
        source,
        actions.len() + 1,
        ActionIntent::Browser(BrowserIntent::DomRead {
            tab: source.tab_id.clone(),
            target: None,
        }),
        false,
        digest,
    )
}

pub(crate) fn poll_downloads<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    step: &PlanStep,
    source: &ConsentedSource,
    page: crate::ReplayPage<'_>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    let actions: Vec<_> = reducer
        .actions()
        .filter(|action| {
            action.proposal().plan_step_id.as_ref() == Some(step.plan_step_id())
                && action.proposal().tool_name() == "browser.download.list"
        })
        .collect();
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
    if actions.len() >= MAX_DOWNLOAD_POLLS
        || actions.iter().any(|action| {
            matches!(
                action.state(),
                ActionState::Rejected
                    | ActionState::Cancelled
                    | ActionState::Failed
                    | ActionState::OutcomeUnknown
            )
        })
    {
        return Ok(Some(Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        }));
    }
    let session = page
        .browser_session_id
        .ok_or(ReplayRefusal::HandleBindingUnavailable)?;
    super::proposal_command(
        reducer,
        procedure.id.as_str(),
        step,
        source,
        actions.len() + 1,
        ActionIntent::Browser(BrowserIntent::DownloadList {
            tab: source.tab_id.clone(),
            browser_session_id: session.clone(),
        }),
        true,
        digest,
    )
}
