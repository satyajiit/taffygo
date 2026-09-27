// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browser-only bindings projected from one immutable reducer fact set.

use crate::ports::TaskViewFacts;

use super::{
    AcceptedTaskConsentBindingFacts, CommittedActionApprovalBindingFacts,
    PendingApprovalBindingFacts, PendingPermissionBindingFacts, TaskSettlementBindingFacts,
    TaskSettlementKind, TerminalTaskBindingFacts, TerminalTaskKind,
};

pub(super) struct TaskBrowserBindings {
    pub(super) allowed_controls: Vec<task_engine::TaskControlKind>,
    pub(super) pending_approval: Option<PendingApprovalBindingFacts>,
    pub(super) pending_permission: Option<PendingPermissionBindingFacts>,
    pub(super) settlement: Option<TaskSettlementBindingFacts>,
    pub(super) terminal: Option<TerminalTaskBindingFacts>,
    pub(super) accepted_consent: Option<AcceptedTaskConsentBindingFacts>,
    pub(super) committed_approvals: Vec<CommittedActionApprovalBindingFacts>,
}

pub(super) fn project_task_browser_bindings(facts: &TaskViewFacts) -> TaskBrowserBindings {
    let pending_approval =
        facts
            .pending_action
            .as_ref()
            .map(|action| PendingApprovalBindingFacts {
                task_id: facts.task_id.clone(),
                action_id: action.action_id.clone(),
                proposal_digest: action.proposal_digest.clone(),
                task_revision: facts.revision,
            });
    let pending_permission =
        facts
            .pending_permission
            .as_ref()
            .map(|permission| PendingPermissionBindingFacts {
                task_id: facts.task_id.clone(),
                request_id: permission.request_id.clone(),
                permission: permission.permission,
                task_revision: facts.revision,
                deadline_monotonic_ms: permission.deadline_monotonic_ms,
                deadline_utc_ms: permission.deadline_utc_ms,
                browser_session_id: permission.browser_session_id.clone(),
            });
    let settlement = match facts.state {
        task_engine::TaskState::Pausing => Some(TaskSettlementBindingFacts {
            task_id: facts.task_id.clone(),
            task_revision: facts.revision,
            kind: TaskSettlementKind::Pause,
        }),
        task_engine::TaskState::Cancelling => Some(TaskSettlementBindingFacts {
            task_id: facts.task_id.clone(),
            task_revision: facts.revision,
            kind: TaskSettlementKind::Cancel,
        }),
        _ => None,
    };
    let terminal = match facts.state {
        task_engine::TaskState::Completed => Some(TerminalTaskKind::Completed),
        task_engine::TaskState::Partial => Some(TerminalTaskKind::Partial),
        task_engine::TaskState::Failed => Some(TerminalTaskKind::Failed),
        task_engine::TaskState::Cancelled => Some(TerminalTaskKind::Cancelled),
        _ => None,
    }
    .map(|kind| TerminalTaskBindingFacts {
        task_id: facts.task_id.clone(),
        task_revision: facts.revision,
        kind,
    });
    // Consent and one-use approvals remain reducer facts after they stop being
    // browser authority: the task still needs the reviewed sources to finish
    // workspace bookkeeping. A settlement is the browser's instruction to
    // revoke that authority before acknowledging the state, Paused keeps it
    // inert until an exact durable Resume, and a terminal is its final cleanup
    // fact. Re-publishing either durable proof beside a withdrawal fact would
    // make one snapshot say both "withdraw" and "rehydrate".
    let withdraws_browser_authority =
        settlement.is_some() || terminal.is_some() || facts.state == task_engine::TaskState::Paused;
    let accepted_consent = (!withdraws_browser_authority)
        .then_some(facts.accepted_consent.as_ref())
        .flatten()
        .map(|consent| AcceptedTaskConsentBindingFacts {
            task_id: facts.task_id.clone(),
            current_task_revision: facts.revision,
            accepted_revision: consent.accepted_revision,
            browser_session_id: consent.browser_session_id.clone(),
            receipt_id: consent.receipt_id.clone(),
            sources: consent.sources.clone(),
            source_discovery_enabled: consent.source_discovery_enabled,
            new_source_cap: consent.new_source_cap,
            provider_route_id: consent.provider_route_id.clone(),
        });
    let committed_approvals = if withdraws_browser_authority {
        Vec::new()
    } else {
        facts
            .committed_action_approvals
            .iter()
            .map(|approval| CommittedActionApprovalBindingFacts {
                task_id: facts.task_id.clone(),
                action_id: approval.action_id.clone(),
                committed_revision: approval.committed_revision,
                receipt_id: approval.receipt_id.clone(),
                proposal_digest: approval.proposal_digest.clone(),
                expires_at_monotonic_ms: approval.expires_at_monotonic_ms,
                expires_at_utc_ms: approval.expires_at_utc_ms,
                browser_session_id: approval.browser_session_id.clone(),
            })
            .collect()
    };
    TaskBrowserBindings {
        allowed_controls: facts.allowed_controls.clone(),
        pending_approval,
        pending_permission,
        settlement,
        terminal,
        accepted_consent,
        committed_approvals,
    }
}
