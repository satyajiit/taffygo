// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! How a task that is not moving is shown: paused for a reason, failed for a
//! reason, or ambiguous because an action's outcome is unknown.
//!
//! Split from the projection tests beside it by subject: these read the end
//! of a task, the others read its shape while it runs.

use core_api_types::{CoreFailureCode, TaskPhase};
use task_engine::{FailureReason, TaskState};

use super::super::task_projection::{project_failure, project_task_view};
use super::task_facts;

#[test]
fn a_pause_the_provider_caused_says_so_and_a_persons_pause_keeps_the_plain_word() {
    let mut facts = task_facts(TaskState::Paused);
    facts.pause_cause = Some(task_engine::PauseCause::ProviderLimit);
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.status_message_key.as_deref(),
        Some("task.paused_provider_limit")
    );

    let mut facts = task_facts(TaskState::Paused);
    facts.pause_cause = Some(task_engine::PauseCause::Offline);
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.status_message_key.as_deref(),
        Some("task.paused_offline")
    );

    let mut facts = task_facts(TaskState::Paused);
    facts.pause_cause = Some(task_engine::PauseCause::User);
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(projected.status_message_key.as_deref(), Some("task.paused"));
}

/// A busy provider and an exhausted allowance are two sentences.
///
/// They were one. An HTTP 5xx is the provider's own capacity and says nothing
/// about the person's account, so reporting it as their limit sent them to
/// check a plan with usage left on it, and offered to switch a model that was
/// never the problem (decision 0219).
#[test]
fn a_busy_provider_is_not_projected_as_the_persons_limit() {
    let mut facts = task_facts(TaskState::Paused);
    facts.pause_cause = Some(task_engine::PauseCause::ProviderBusy);
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        projected.status_message_key.as_deref(),
        Some("task.paused_provider_busy")
    );
    assert_ne!(
        projected.status_message_key.as_deref(),
        Some("task.paused_provider_limit")
    );
}

#[test]
fn paused_phase_does_not_invent_resume_and_retains_outcome_unknown_failure() {
    let mut facts = task_facts(TaskState::Paused);
    facts.outcome_unknown_actions = 1;
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());

    assert_eq!(projected.phase, TaskPhase::Paused);
    assert!(projected.allowed_controls.is_empty());
    assert_eq!(
        projected.failure.map(|failure| failure.code),
        Some(CoreFailureCode::OutcomeUnknown)
    );
}

#[test]
fn an_ambiguous_action_overrides_nonterminal_phase_and_is_never_retryable() {
    let mut facts = task_facts(TaskState::Running);
    facts.outcome_unknown_actions = 1;
    let projected = project_task_view(facts).unwrap_or_else(|_| unreachable!());
    assert_eq!(projected.phase, TaskPhase::OutcomeUnknown);
    let failure = projected.failure.unwrap_or_else(|| unreachable!());
    assert_eq!(failure.code, CoreFailureCode::OutcomeUnknown);
    assert!(!failure.retryable);
}

#[test]
fn every_terminal_failure_keeps_its_exact_closed_meaning() {
    let expected = [
        CoreFailureCode::BudgetExceeded,
        CoreFailureCode::ProviderUnavailable,
        CoreFailureCode::SourcesUnavailable,
        CoreFailureCode::UnverifiableAction,
        CoreFailureCode::JournalUnusable,
        CoreFailureCode::DeadlineExceeded,
        CoreFailureCode::ProviderRefused,
        CoreFailureCode::ProviderLimit,
        CoreFailureCode::Offline,
        CoreFailureCode::PolicyRefused,
    ];
    assert_eq!(FailureReason::ALL.len(), expected.len());
    for (reason, code) in FailureReason::ALL.iter().zip(expected) {
        assert_eq!(project_failure(*reason).code, code, "{}", reason.label());
    }
}
