// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! How a task ends: the validated result, its gaps, and why it failed.
//!
//! Three closed enumerations and the result they describe. They are one
//! module because the promotion rule binds them: `COMPLETED` requires a
//! [`TaskResult`] with no [`UnmetRequirement`], the reducer checks
//! [`TaskResult::is_complete`] rather than trusting the caller's choice of
//! command, and a [`FailureReason`] is the only alternative to a result. Split
//! apart, they would be three files that always change together.

use super::TaskKind;
use crate::ids::ArtifactId;
/// Why a task failed.
///
/// A closed enumeration, so no page or model text becomes a failure
/// explanation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FailureReason {
    /// A budget was exhausted.
    BudgetExhausted,
    /// The provider route could not be used.
    ProviderUnavailable,
    /// The sources in scope could not be observed.
    SourcesUnavailable,
    /// An action could not be verified and could not be safely retried.
    UnverifiableAction,
    /// The journal or its projection could not be trusted.
    JournalUnusable,
    /// The task ran past its deadline.
    DeadlineExceeded,
    /// The provider refused the request itself: a key it does not accept, or
    /// a request it will not serve.
    ProviderRefused,
    /// The provider's limit was reached. Ordinarily a pause rather than a
    /// failure; this is the terminal form, for when nothing can resume it.
    ProviderLimit,
    /// The device is offline. Ordinarily a pause; this is the terminal form.
    Offline,
    /// The task's moves were refused until nothing admissible was left.
    PolicyRefused,
}

impl FailureReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::BudgetExhausted,
        Self::ProviderUnavailable,
        Self::SourcesUnavailable,
        Self::UnverifiableAction,
        Self::JournalUnusable,
        Self::DeadlineExceeded,
        Self::ProviderRefused,
        Self::ProviderLimit,
        Self::Offline,
        Self::PolicyRefused,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::BudgetExhausted => "budget_exhausted",
            Self::ProviderUnavailable => "provider_unavailable",
            Self::SourcesUnavailable => "sources_unavailable",
            Self::UnverifiableAction => "unverifiable_action",
            Self::JournalUnusable => "journal_unusable",
            Self::DeadlineExceeded => "deadline_exceeded",
            Self::ProviderRefused => "provider_refused",
            Self::ProviderLimit => "provider_limit",
            Self::Offline => "offline",
            Self::PolicyRefused => "policy_refused",
        }
    }
}

/// A requirement the task was asked for and did not meet.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct UnmetRequirement {
    /// Which part of the goal it belongs to, as the plan named it.
    pub subject: String,
    /// Why it is unmet, from a closed enumeration.
    pub reason: GapReason,
}

/// Why a requirement is unmet.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum GapReason {
    /// No source in scope carried it.
    NotFoundInScope,
    /// Sources disagreed and the conflict is unresolved.
    ConflictUnresolved,
    /// A budget stopped the work before it was reached.
    BudgetReached,
    /// The source refused to be observed.
    SourceUnavailable,
    /// The user stopped the task first.
    StoppedByUser,
}

impl GapReason {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NotFoundInScope => "not_found_in_scope",
            Self::ConflictUnresolved => "conflict_unresolved",
            Self::BudgetReached => "budget_reached",
            Self::SourceUnavailable => "source_unavailable",
            Self::StoppedByUser => "stopped_by_user",
        }
    }
}

/// A validated result candidate.
///
/// `COMPLETED` requires one of these with no unmet requirement. The reducer
/// checks [`TaskResult::is_complete`] rather than trusting the caller's choice
/// of command, so a result with gaps cannot be promoted to success.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TaskResult {
    /// The artifacts the result produced.
    pub artifact_ids: Vec<ArtifactId>,
    /// Requirements the task did not meet, in a deterministic order.
    pub unmet: Vec<UnmetRequirement>,
    /// How many facts the result rests on.
    pub fact_count: u64,
    /// How many sources those facts cite.
    pub source_count: u64,
}

impl TaskResult {
    /// Whether the result meets every requirement it was asked for.
    pub fn is_complete(&self) -> bool {
        self.unmet.is_empty()
    }

    /// Whether this result can complete the named kind of task.
    ///
    /// Research without a fact or artifact is an empty answer, even when no
    /// caller supplied an explicit gap. Keeping this check here gives every
    /// reducer entry point the same rule while leaving future task kinds free
    /// to define their own payload invariant.
    pub fn is_complete_for(&self, kind: TaskKind) -> bool {
        self.is_complete()
            && match kind {
                TaskKind::Research => self.fact_count > 0 || !self.artifact_ids.is_empty(),
                // An errand's completeness is the verified outcome the agent
                // table already required before it offered the result; facts
                // and artifacts are not what it was for.
                TaskKind::Errand => true,
            }
    }
}

#[cfg(test)]
mod tests {
    use super::{GapReason, TaskKind, TaskResult, UnmetRequirement};

    #[test]
    fn a_result_with_a_gap_is_not_complete() {
        let mut result = TaskResult::default();
        assert!(result.is_complete());
        result.unmet.push(UnmetRequirement {
            subject: "price".to_owned(),
            reason: GapReason::NotFoundInScope,
        });
        assert!(!result.is_complete());
    }

    #[test]
    fn empty_research_is_never_complete() {
        let empty = TaskResult::default();
        assert!(!empty.is_complete_for(TaskKind::Research));

        let with_fact = TaskResult {
            fact_count: 1,
            ..TaskResult::default()
        };
        assert!(with_fact.is_complete_for(TaskKind::Research));
    }
}
