// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Usage and cost accounting for one task.
//!
//! The cost model says the reporting units are cost per started task, per
//! completed task, and per verified-complete task — the last being the honest
//! one — and that retries, fallback calls, and replans are part of the task's
//! cost rather than a separate line. Both rules are structural here: one ledger
//! per task, every call kind adding to the same totals, and the outcome
//! recorded on the ledger so a report can be filed under the right unit.
//!
//! Admission happens before a call and refuses; recording happens after one and
//! cannot. Spend that already left the device is not something an accounting
//! type gets to reject, so [`TaskLedger::record`] always succeeds and the
//! overage becomes visible state that later admissions honor.
//!
//! Prices are snapshots. Access backed by a user plan reports no provider
//! price, so the catalog carries equivalent rates and every record says the
//! value was imputed — a plan does not make a request free to account for.

use std::collections::BTreeMap;

use crate::catalog::{ModelRole, PriceBasis, PriceSnapshot};
use crate::ids::{ModelKey, TaskId};
use crate::money::{Currency, Micros};

/// Tokens a request consumed or is expected to consume.
///
/// Cache reads and cache writes are counted apart from ordinary input because
/// they are priced apart, and because a cache that is written and never read is
/// a cost with no benefit — which only shows up if the two are separate.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct TokenUsage {
    /// Uncached input tokens.
    pub input: u64,
    /// Output tokens, including any thinking the provider bills as output.
    pub output: u64,
    /// Tokens read from a prompt cache.
    pub cache_read: u64,
    /// Tokens written to a prompt cache.
    pub cache_write: u64,
}

impl TokenUsage {
    /// Every token that counts toward the model's input side.
    pub fn total_input(self) -> u64 {
        self.input
            .saturating_add(self.cache_read)
            .saturating_add(self.cache_write)
    }

    /// Adds two usages, saturating rather than wrapping.
    #[must_use]
    pub fn saturating_add(self, other: Self) -> Self {
        Self {
            input: self.input.saturating_add(other.input),
            output: self.output.saturating_add(other.output),
            cache_read: self.cache_read.saturating_add(other.cache_read),
            cache_write: self.cache_write.saturating_add(other.cache_write),
        }
    }
}

/// What a priced usage came to.
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct CostAmount {
    /// The currency of the snapshot that priced it.
    pub currency: Currency,
    /// The amount.
    pub micros: Micros,
    /// Whether the price was billed or imputed.
    pub basis: PriceBasis,
}

/// Why usage could not be priced.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PricingError {
    /// A rate multiplied out beyond the range of the amount type.
    Overflow,
}

impl core::fmt::Display for PricingError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str("pricing overflowed")
    }
}

/// Prices a usage against a snapshot.
///
/// Long-context tiers are selected by total input size, and each component is
/// rounded up independently so accumulated truncation can never let a task
/// spend past its budget.
pub fn price(snapshot: &PriceSnapshot, usage: TokenUsage) -> Result<CostAmount, PricingError> {
    let rates = snapshot.rates_for(usage.total_input());
    let parts = [
        Micros::for_tokens(rates.input, usage.input),
        Micros::for_tokens(rates.output, usage.output),
        Micros::for_tokens(rates.cache_read, usage.cache_read),
        Micros::for_tokens(rates.cache_write, usage.cache_write),
    ];
    let mut total = Micros::ZERO;
    for part in parts {
        let part = part.ok_or(PricingError::Overflow)?;
        total = total.checked_add(part).ok_or(PricingError::Overflow)?;
    }
    Ok(CostAmount {
        currency: snapshot.currency,
        micros: total,
        basis: snapshot.basis,
    })
}

/// Why one call was made.
///
/// All three spend from the same budget. The distinction exists so a report can
/// say how much of a task's cost was retry and failover, not so any of them can
/// be accounted somewhere else.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum CallKind {
    /// The first attempt at a step.
    Initial,
    /// A repeat of the same request after a transient or semantic failure.
    Retry,
    /// The same step against the next candidate after a failure.
    Failover,
}

/// The hard limits one task runs inside.
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct TaskBudget {
    /// The currency every limit and every priced call must be in.
    pub currency: Currency,
    /// Most a task may spend.
    pub max_micros: Micros,
    /// Most calls a task may make, counting retries and failovers.
    pub max_calls: u32,
    /// Most retries a task may make.
    pub max_retries: u32,
}

/// Why the ledger refused a call.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BudgetRefusal {
    /// The projected total would pass the budget.
    CostExceeded {
        /// The budget.
        budget: Micros,
        /// Already committed.
        committed: Micros,
        /// What this call would add.
        requested: Micros,
    },
    /// The call count is used up.
    CallsExhausted {
        /// The limit.
        limit: u32,
    },
    /// The retry count is used up.
    RetriesExhausted {
        /// The limit.
        limit: u32,
    },
    /// The price snapshot is in another currency than the budget.
    ///
    /// The model router has no exchange rate and will not invent one.
    CurrencyMismatch {
        /// The budget's currency.
        budget: Currency,
        /// The snapshot's currency.
        snapshot: Currency,
    },
    /// The arithmetic did not fit.
    Overflow,
}

impl core::fmt::Display for BudgetRefusal {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::CostExceeded {
                budget,
                committed,
                requested,
            } => write!(
                f,
                "budget {budget} exceeded: {committed} committed plus {requested} requested"
            ),
            Self::CallsExhausted { limit } => write!(f, "call limit {limit} reached"),
            Self::RetriesExhausted { limit } => write!(f, "retry limit {limit} reached"),
            Self::CurrencyMismatch { budget, snapshot } => {
                write!(
                    f,
                    "budget is in {budget} and the price snapshot is in {snapshot}"
                )
            }
            Self::Overflow => f.write_str("cost arithmetic overflowed"),
        }
    }
}

/// How a task ended, which decides the unit its cost is reported under.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum TaskOutcome {
    /// Started and not yet finished.
    Started,
    /// Finished and produced a result.
    Completed,
    /// Finished, produced a result, and the result passed verification. This is
    /// the honest unit.
    VerifiedComplete,
    /// Finished without a result.
    Failed,
    /// Stopped by the user.
    Cancelled,
}

/// Totals for one role within a task.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct RoleTotals {
    /// Calls made in this role.
    pub calls: u32,
    /// Tokens consumed in this role.
    pub tokens: TokenUsage,
    /// Amount spent in this role.
    pub micros: Micros,
}

/// One recorded call.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct UsageRecord {
    /// Which model.
    pub model: ModelKey,
    /// Which role it served.
    pub role: ModelRole,
    /// Why the call was made.
    pub kind: CallKind,
    /// What it consumed.
    pub tokens: TokenUsage,
    /// What it cost.
    pub cost: CostAmount,
}

/// The one ledger a task spends through.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct TaskLedger {
    task_id: TaskId,
    budget: TaskBudget,
    outcome: TaskOutcome,
    calls: u32,
    retries: u32,
    failovers: u32,
    estimated: Micros,
    committed: Micros,
    implied: Micros,
    tokens: TokenUsage,
    per_role: BTreeMap<ModelRole, RoleTotals>,
}

impl TaskLedger {
    /// Opens a ledger for a task.
    pub fn new(task_id: TaskId, budget: TaskBudget) -> Self {
        Self {
            task_id,
            budget,
            outcome: TaskOutcome::Started,
            calls: 0,
            retries: 0,
            failovers: 0,
            estimated: Micros::ZERO,
            committed: Micros::ZERO,
            implied: Micros::ZERO,
            tokens: TokenUsage::default(),
            per_role: BTreeMap::new(),
        }
    }

    /// The task.
    pub fn task_id(&self) -> TaskId {
        self.task_id
    }

    /// The budget.
    pub fn budget(&self) -> TaskBudget {
        self.budget
    }

    /// What has been spent.
    pub fn committed(&self) -> Micros {
        self.committed
    }

    /// What is left, or zero once the budget is gone.
    pub fn remaining(&self) -> Micros {
        self.budget.max_micros.saturating_sub(self.committed)
    }

    /// Whether recorded spend has already passed the budget.
    pub fn is_over_budget(&self) -> bool {
        self.committed > self.budget.max_micros
    }

    /// Prices a projected usage without recording it.
    pub fn estimate(
        &self,
        snapshot: &PriceSnapshot,
        usage: TokenUsage,
    ) -> Result<CostAmount, BudgetRefusal> {
        if snapshot.currency != self.budget.currency {
            return Err(BudgetRefusal::CurrencyMismatch {
                budget: self.budget.currency,
                snapshot: snapshot.currency,
            });
        }
        price(snapshot, usage).map_err(|_| BudgetRefusal::Overflow)
    }

    /// Decides whether a call may be made.
    ///
    /// This is the refusal point. Once a call has been made its cost is a fact,
    /// so nothing downstream gets a second chance to say no.
    pub fn admit(&self, kind: CallKind, estimate: CostAmount) -> Result<(), BudgetRefusal> {
        if estimate.currency != self.budget.currency {
            return Err(BudgetRefusal::CurrencyMismatch {
                budget: self.budget.currency,
                snapshot: estimate.currency,
            });
        }
        if self.calls >= self.budget.max_calls {
            return Err(BudgetRefusal::CallsExhausted {
                limit: self.budget.max_calls,
            });
        }
        if kind == CallKind::Retry && self.retries >= self.budget.max_retries {
            return Err(BudgetRefusal::RetriesExhausted {
                limit: self.budget.max_retries,
            });
        }
        let projected = self
            .committed
            .checked_add(estimate.micros)
            .ok_or(BudgetRefusal::Overflow)?;
        if projected > self.budget.max_micros {
            return Err(BudgetRefusal::CostExceeded {
                budget: self.budget.max_micros,
                committed: self.committed,
                requested: estimate.micros,
            });
        }
        Ok(())
    }

    /// Notes what a call was expected to cost, for the estimate-versus-actual
    /// line of a cost report.
    pub fn note_estimate(&mut self, estimate: CostAmount) {
        self.estimated = self
            .estimated
            .checked_add(estimate.micros)
            .unwrap_or(self.estimated);
    }

    /// Records what a call actually cost.
    ///
    /// Always succeeds: the spend has happened. If it takes the task past its
    /// budget, [`TaskLedger::is_over_budget`] says so and the next
    /// [`TaskLedger::admit`] refuses.
    pub fn record(&mut self, record: &UsageRecord) {
        self.calls = self.calls.saturating_add(1);
        match record.kind {
            CallKind::Initial => {}
            CallKind::Retry => self.retries = self.retries.saturating_add(1),
            CallKind::Failover => self.failovers = self.failovers.saturating_add(1),
        }
        self.tokens = self.tokens.saturating_add(record.tokens);
        if record.cost.currency == self.budget.currency {
            self.committed = self
                .committed
                .checked_add(record.cost.micros)
                .unwrap_or(self.committed);
            if record.cost.basis == PriceBasis::Implied {
                self.implied = self
                    .implied
                    .checked_add(record.cost.micros)
                    .unwrap_or(self.implied);
            }
        }
        let totals = self.per_role.entry(record.role).or_default();
        totals.calls = totals.calls.saturating_add(1);
        totals.tokens = totals.tokens.saturating_add(record.tokens);
        totals.micros = totals
            .micros
            .checked_add(record.cost.micros)
            .unwrap_or(totals.micros);
    }

    /// Records how the task ended.
    pub fn finish(&mut self, outcome: TaskOutcome) {
        self.outcome = outcome;
    }

    /// The report for this task.
    pub fn report(&self) -> CostReport {
        CostReport {
            task_id: self.task_id,
            outcome: self.outcome,
            currency: self.budget.currency,
            budget: self.budget.max_micros,
            estimated: self.estimated,
            actual: self.committed,
            imputed: self.implied,
            over_budget: self.is_over_budget(),
            calls: self.calls,
            retries: self.retries,
            failovers: self.failovers,
            tokens: self.tokens,
            per_role: self.per_role.clone(),
        }
    }
}

/// What a task cost, reported under the unit its outcome earns.
///
/// Content-free by construction: identifiers, counts, and amounts. No prompt,
/// no page content, no locator.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct CostReport {
    /// The task.
    pub task_id: TaskId,
    /// How it ended, which selects the reporting unit.
    pub outcome: TaskOutcome,
    /// Currency of every amount below.
    pub currency: Currency,
    /// The budget it ran under.
    pub budget: Micros,
    /// What the calls were expected to cost.
    pub estimated: Micros,
    /// What they did cost.
    pub actual: Micros,
    /// How much of the actual was imputed rather than billed.
    pub imputed: Micros,
    /// Whether the actual passed the budget.
    pub over_budget: bool,
    /// Calls made, counting retries and failovers.
    pub calls: u32,
    /// Retries made.
    pub retries: u32,
    /// Failovers made.
    pub failovers: u32,
    /// Tokens consumed.
    pub tokens: TokenUsage,
    /// Totals per role.
    pub per_role: BTreeMap<ModelRole, RoleTotals>,
}
