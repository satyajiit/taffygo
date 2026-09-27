// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Task budgets (domain model section 9.4).
//!
//! # A missing limit is a policy default, never "unlimited"
//!
//! The domain model states it in one sentence and this module enforces it in
//! the type system: [`TaskBudgets`] is a sparse map, and there is no way to ask
//! it for a limit without also supplying the [`BudgetDefaults`] that fill the
//! gaps. Every query returns a number.
//!
//! # No number is compiled in here
//!
//! The defaults arrive from the policy bundle the task froze, because product
//! numbers live in the metric registry and their owner documents, not scattered
//! through code. [`BudgetDefaults::new`] takes a total function over
//! [`BudgetKind`], so a caller cannot construct defaults with a hole in them.

use std::collections::BTreeMap;

/// A limit a task may run into (domain model section 9.4).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum BudgetKind {
    /// How many sources the task may take into scope.
    MaxSources,
    /// How many tabs the assistant may hold open at once.
    MaxWorkingTabs,
    /// How long the task may run, in milliseconds of wall time.
    MaxWallTimeMillis,
    /// How many model requests the task may make.
    MaxModelRequests,
    /// How much may be sent to a model, in tokens or bytes.
    MaxInputTokensOrBytes,
    /// How much may be received from a model, in tokens or bytes.
    MaxOutputTokensOrBytes,
    /// What the task may cost, in the ledger's smallest currency unit.
    MaxCost,
    /// How deep the task may follow links from a seed source.
    MaxNavigationDepth,
    /// How many times one plan step may be retried.
    MaxRetriesPerStep,
    /// How large a generated artifact may be, in bytes.
    MaxArtifactBytes,
}

impl BudgetKind {
    /// Every limit, in the order the domain model lists them.
    pub const ALL: &'static [Self] = &[
        Self::MaxSources,
        Self::MaxWorkingTabs,
        Self::MaxWallTimeMillis,
        Self::MaxModelRequests,
        Self::MaxInputTokensOrBytes,
        Self::MaxOutputTokensOrBytes,
        Self::MaxCost,
        Self::MaxNavigationDepth,
        Self::MaxRetriesPerStep,
        Self::MaxArtifactBytes,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::MaxSources => "max_sources",
            Self::MaxWorkingTabs => "max_working_tabs",
            Self::MaxWallTimeMillis => "max_wall_time_millis",
            Self::MaxModelRequests => "max_model_requests",
            Self::MaxInputTokensOrBytes => "max_input_tokens_or_bytes",
            Self::MaxOutputTokensOrBytes => "max_output_tokens_or_bytes",
            Self::MaxCost => "max_cost",
            Self::MaxNavigationDepth => "max_navigation_depth",
            Self::MaxRetriesPerStep => "max_retries_per_step",
            Self::MaxArtifactBytes => "max_artifact_bytes",
        }
    }
}

/// The limits policy applies when a task did not set one of its own.
///
/// Constructed from a total function over [`BudgetKind`], so there is no kind
/// without a default and therefore no path to an unbounded task.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BudgetDefaults {
    values: BTreeMap<BudgetKind, u64>,
    floor: u64,
}

impl BudgetDefaults {
    /// Builds the defaults by asking `limit` for every kind.
    ///
    /// `floor` is the smallest value supplied. It is what a lookup would return
    /// for a kind that is somehow absent, which keeps the lookup total and
    /// keeps its failure mode restrictive rather than permissive.
    pub fn new(mut limit: impl FnMut(BudgetKind) -> u64) -> Self {
        let mut values = BTreeMap::new();
        let mut floor = u64::MAX;
        for kind in BudgetKind::ALL {
            let value = limit(*kind);
            floor = floor.min(value);
            values.insert(*kind, value);
        }
        Self { values, floor }
    }

    /// Builds defaults where every kind has the same limit.
    pub fn uniform(limit: u64) -> Self {
        Self::new(|_| limit)
    }

    /// The default for `kind`. Always a number.
    pub fn limit(&self, kind: BudgetKind) -> u64 {
        self.values.get(&kind).copied().unwrap_or(self.floor)
    }
}

/// The limits a task set for itself (domain model section 9.4).
///
/// Sparse on purpose: a task states what it wants to constrain, and policy
/// supplies the rest. Changing one is an explicit event and may require the
/// user's approval, so nothing here mutates a limit in place.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TaskBudgets {
    limits: BTreeMap<BudgetKind, u64>,
}

impl TaskBudgets {
    /// Budgets that state nothing, so every limit is the policy default.
    pub const fn none() -> Self {
        Self {
            limits: BTreeMap::new(),
        }
    }

    /// The same budgets with `kind` set to `limit`.
    #[must_use]
    pub fn with(mut self, kind: BudgetKind, limit: u64) -> Self {
        self.limits.insert(kind, limit);
        self
    }

    /// Whether the task stated this limit itself.
    pub fn is_stated(&self, kind: BudgetKind) -> bool {
        self.limits.contains_key(&kind)
    }

    /// The limit in force: the task's own where it stated one, the policy
    /// default otherwise. Never unlimited.
    pub fn effective_limit(&self, kind: BudgetKind, defaults: &BudgetDefaults) -> u64 {
        self.limits
            .get(&kind)
            .copied()
            .unwrap_or_else(|| defaults.limit(kind))
    }

    /// Every kind the task stated, in order.
    pub fn stated(&self) -> impl Iterator<Item = (BudgetKind, u64)> + '_ {
        self.limits.iter().map(|(kind, limit)| (*kind, *limit))
    }
}

/// How much of one limit a proposed piece of work draws.
///
/// It comes from the tool definition — trusted, versioned application data —
/// and never from a model's proposal, so a proposal cannot understate what it
/// would cost.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BudgetDraw {
    /// Which limit.
    pub kind: BudgetKind,
    /// How much of it.
    pub amount: u64,
}

/// Why a budget refused work.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BudgetRefusal {
    /// Which limit was reached.
    pub kind: BudgetKind,
    /// The limit in force.
    pub limit: u64,
    /// What was already spent against it.
    pub spent: u64,
    /// What the refused work would have added.
    pub requested: u64,
}

/// What a task has spent against each limit.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct BudgetLedger {
    spent: BTreeMap<BudgetKind, u64>,
}

impl BudgetLedger {
    /// An empty ledger.
    pub const fn new() -> Self {
        Self {
            spent: BTreeMap::new(),
        }
    }

    /// What has been spent against `kind`.
    pub fn spent(&self, kind: BudgetKind) -> u64 {
        self.spent.get(&kind).copied().unwrap_or(0)
    }

    /// Whether `amount` more may be spent against `kind`.
    ///
    /// Checked addition, so a request that would overflow the counter is
    /// refused rather than wrapping into a small number that would pass.
    pub fn admit(
        &self,
        kind: BudgetKind,
        amount: u64,
        budgets: &TaskBudgets,
        defaults: &BudgetDefaults,
    ) -> Result<(), BudgetRefusal> {
        let limit = budgets.effective_limit(kind, defaults);
        let spent = self.spent(kind);
        let refusal = BudgetRefusal {
            kind,
            limit,
            spent,
            requested: amount,
        };
        let Some(total) = spent.checked_add(amount) else {
            return Err(refusal);
        };
        if total > limit {
            return Err(refusal);
        }
        Ok(())
    }

    /// Spends `amount` against `kind` after [`Self::admit`] allowed it.
    pub fn charge(&mut self, kind: BudgetKind, amount: u64) {
        let entry = self.spent.entry(kind).or_insert(0);
        *entry = entry.saturating_add(amount);
    }
}

#[cfg(test)]
mod tests {
    use super::{BudgetDefaults, BudgetKind, BudgetLedger, TaskBudgets};

    fn defaults() -> BudgetDefaults {
        BudgetDefaults::new(|kind| match kind {
            BudgetKind::MaxSources => 8,
            BudgetKind::MaxRetriesPerStep => 2,
            _ => 100,
        })
    }

    #[test]
    fn a_missing_limit_is_the_policy_default_and_not_unlimited() {
        let budgets = TaskBudgets::none();
        let defaults = defaults();
        for kind in BudgetKind::ALL {
            assert!(!budgets.is_stated(*kind));
            assert!(budgets.effective_limit(*kind, &defaults) < u64::MAX);
        }
        assert_eq!(
            budgets.effective_limit(BudgetKind::MaxSources, &defaults),
            8
        );
    }

    #[test]
    fn a_stated_limit_wins_over_the_default() {
        let budgets = TaskBudgets::none().with(BudgetKind::MaxSources, 3);
        assert_eq!(
            budgets.effective_limit(BudgetKind::MaxSources, &defaults()),
            3
        );
    }

    #[test]
    fn a_ledger_refuses_the_charge_that_would_cross_the_limit() {
        let budgets = TaskBudgets::none().with(BudgetKind::MaxSources, 2);
        let defaults = defaults();
        let mut ledger = BudgetLedger::new();
        assert!(ledger
            .admit(BudgetKind::MaxSources, 2, &budgets, &defaults)
            .is_ok());
        ledger.charge(BudgetKind::MaxSources, 2);
        assert!(ledger
            .admit(BudgetKind::MaxSources, 1, &budgets, &defaults)
            .is_err());
        assert_eq!(ledger.spent(BudgetKind::MaxSources), 2);
    }

    #[test]
    fn an_overflowing_request_is_refused_rather_than_wrapping() {
        let budgets = TaskBudgets::none().with(BudgetKind::MaxArtifactBytes, u64::MAX);
        let defaults = defaults();
        let mut ledger = BudgetLedger::new();
        ledger.charge(BudgetKind::MaxArtifactBytes, u64::MAX);
        assert!(ledger
            .admit(BudgetKind::MaxArtifactBytes, 1, &budgets, &defaults)
            .is_err());
    }

    #[test]
    fn defaults_are_total_over_every_kind() {
        let defaults = BudgetDefaults::uniform(5);
        for kind in BudgetKind::ALL {
            assert_eq!(defaults.limit(*kind), 5, "{}", kind.label());
        }
    }
}
