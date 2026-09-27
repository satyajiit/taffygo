// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browser-only durable authority facts derived from the reducer journal.

use task_engine::journal::CommandRecord;
use task_engine::{ActionState, BudgetKind, Command, Reducer, TaskJournal, TaskState};

use super::{AcceptedTaskConsentFacts, CommittedActionApprovalFacts, TaskViewFactsError};

pub(super) fn accepted_consent<C, I>(
    reducer: &Reducer<C, I>,
) -> Result<Option<AcceptedTaskConsentFacts>, TaskViewFactsError>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let task = reducer.task();
    if matches!(
        task.state(),
        TaskState::Draft
            | TaskState::Cancelled
            | TaskState::Completed
            | TaskState::Partial
            | TaskState::Failed
    ) || (task.state() == TaskState::AwaitingConsent
        && task.consent_stage() == Some(task_engine::task::ConsentStage::Initial))
    {
        return Ok(None);
    }
    let mut preview = None;
    let mut acceptance = None;
    for record in reducer.journal().commands() {
        match &record.envelope.command {
            Command::StartTask(value) => {
                if preview.replace(value.clone()).is_some() {
                    return Err(TaskViewFactsError::InvalidAcceptedConsent);
                }
            }
            Command::AcceptInitialConsent(receipt)
                if acceptance.replace((record, receipt)).is_some() =>
            {
                return Err(TaskViewFactsError::InvalidAcceptedConsent);
            }
            _ => {}
        }
    }
    let Some(preview) = preview else {
        return Err(TaskViewFactsError::MissingAcceptedConsent);
    };
    let Some((record, receipt)) = acceptance else {
        // Stopping before initial consent enters Cancelling to follow the
        // task-state diagram, but there is no accepted authority to project or
        // revoke. Every other nonterminal state still requires the unique
        // journalled acceptance below.
        if task.state() == TaskState::Cancelling {
            return Ok(None);
        }
        return Err(TaskViewFactsError::MissingAcceptedConsent);
    };
    let accepted_revision = resulting_revision(reducer.journal(), record)
        .ok_or(TaskViewFactsError::InvalidAcceptedConsent)?;
    let new_source_cap = accepted_source_cap(reducer, &preview)?;
    let snapshot = task.snapshot();
    let accepted_sources = task.consented_sources();
    Ok(Some(AcceptedTaskConsentFacts {
        accepted_revision,
        browser_session_id: snapshot.browser_session_id.as_str().to_owned(),
        receipt_id: receipt.0.clone(),
        sources: accepted_sources.to_vec(),
        source_discovery_enabled: preview.source_discovery_enabled,
        new_source_cap,
        provider_route_id: preview
            .provider_route
            .map(|route| route.as_str().to_owned()),
    }))
}

fn accepted_source_cap<C, I>(
    reducer: &Reducer<C, I>,
    preview: &task_engine::ScopePreview,
) -> Result<u32, TaskViewFactsError>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let task = reducer.task();
    let snapshot = task.snapshot();
    let mut initial_scope = task_engine::SourceScope::new();
    for source in &snapshot.consented_sources {
        initial_scope = initial_scope.include(source.source_id);
    }
    if preview.scope != initial_scope
        || preview.sources != snapshot.consented_sources
        || preview.source_discovery_enabled != snapshot.source_discovery_enabled
        || preview.provider_route != snapshot.provider_route
        || preview.budgets != *task.budgets()
        || task.scope().included()
            != task
                .consented_sources()
                .iter()
                .map(|source| source.source_id)
                .collect::<Vec<_>>()
    {
        return Err(TaskViewFactsError::InvalidAcceptedConsent);
    }
    let Some(max_sources) = preview
        .budgets
        .stated()
        .find_map(|(kind, limit)| (kind == BudgetKind::MaxSources).then_some(limit))
    else {
        return Err(TaskViewFactsError::InvalidAcceptedConsent);
    };
    let new_source_cap = max_sources
        .checked_sub(task.ledger().spent(BudgetKind::MaxSources))
        .and_then(|value| u32::try_from(value).ok())
        .ok_or(TaskViewFactsError::InvalidAcceptedConsent)?;
    let initial_source_count = u64::try_from(preview.sources.len())
        .map_err(|_| TaskViewFactsError::InvalidAcceptedConsent)?;
    let preview_total = initial_source_count
        .checked_add(u64::from(preview.new_source_cap))
        .ok_or(TaskViewFactsError::InvalidAcceptedConsent)?;
    let shape_is_exact = match snapshot.template_id {
        task_engine::TaskTemplateId::WebErrand
            if preview
                .provider_route
                .as_ref()
                .map(task_engine::ProviderRouteId::as_str)
                == Some(task_engine::REVIEWED_NO_MODEL_ROUTE_ID)
                && snapshot.library_refresh.is_none() =>
        {
            // Project the same closed saved-flow shape admitted by the
            // reducer. Its one accepted page is not a discovery allowance.
            snapshot
                .skill_version_id
                .as_ref()
                .is_some_and(|id| !id.0.is_empty())
                && snapshot.builtin_skill.is_none()
                && task.kind() == task_engine::TaskKind::Errand
                && task.control_mode() == task_engine::ControlMode::Assistant
                && preview.sources.len() == 1
                && !preview.source_discovery_enabled
                && preview.new_source_cap == 0
                && snapshot.discovery_tab_id.is_none()
                && preview
                    .budgets
                    .stated()
                    .any(|(kind, limit)| kind == BudgetKind::MaxModelRequests && limit == 0)
        }
        task_engine::TaskTemplateId::WebErrand => {
            preview.source_discovery_enabled
                && preview.sources.len() <= 1
                && (1..=task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP)
                    .contains(&preview.new_source_cap)
                && new_source_cap <= preview.new_source_cap
        }
        task_engine::TaskTemplateId::BuildSourceTable
        | task_engine::TaskTemplateId::CompareProducts
        | task_engine::TaskTemplateId::SummarizeEvidence => {
            !preview.source_discovery_enabled
                && preview.new_source_cap == 0
                && snapshot.discovery_tab_id.is_none()
        }
    };
    if preview_total != max_sources
        || snapshot.remaining_new_source_cap != new_source_cap
        || !shape_is_exact
    {
        return Err(TaskViewFactsError::InvalidAcceptedConsent);
    }
    Ok(new_source_cap)
}

pub(super) fn committed_approvals<C, I>(
    reducer: &Reducer<C, I>,
) -> Result<Vec<CommittedActionApprovalFacts>, TaskViewFactsError>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let mut approvals = Vec::new();
    for action in reducer.actions() {
        let Some(approval) = action.approval() else {
            continue;
        };
        if action.state() != ActionState::Proposed {
            continue;
        }
        let matches = reducer
            .journal()
            .commands()
            .filter(|record| {
                matches!(
                    &record.envelope.command,
                    Command::ApproveAction {
                        approval: receipt,
                        still_current: true,
                        expires_at_monotonic_ms,
                        expires_at_utc_ms,
                        browser_session_id,
                    } if receipt == &approval.receipt
                        && *expires_at_monotonic_ms == approval.expires_at.0
                        && *expires_at_utc_ms == approval.expires_at_utc.0
                        && browser_session_id == &approval.browser_session_id
                )
            })
            .collect::<Vec<_>>();
        let [record] = matches.as_slice() else {
            return Err(TaskViewFactsError::InvalidCommittedApproval);
        };
        let committed_revision = resulting_revision(reducer.journal(), record)
            .ok_or(TaskViewFactsError::InvalidCommittedApproval)?;
        approvals.push(CommittedActionApprovalFacts {
            action_id: action.action_id().as_str().to_owned(),
            committed_revision,
            receipt_id: approval.receipt.0.clone(),
            proposal_digest: approval.proposal_digest.value.clone(),
            expires_at_monotonic_ms: approval.expires_at.0,
            expires_at_utc_ms: approval.expires_at_utc.0,
            browser_session_id: approval.browser_session_id.as_str().to_owned(),
        });
    }
    Ok(approvals)
}

fn resulting_revision(journal: &TaskJournal, command: &CommandRecord) -> Option<u64> {
    journal
        .events()
        .filter(|event| {
            event.sequence > command.sequence
                && event.causation_key == command.envelope.idempotency_key
        })
        .map(|event| event.revision)
        .max()
}
