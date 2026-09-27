// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Whether a retryable provider failure may be retried *in the product*.
//!
//! Every other fixture in this suite runs under `common::defaults()`, which
//! states `MaxRetriesPerStep => 1`. The product does not: its factory is
//! `BudgetDefaults::uniform(0)`, and a start-task command states exactly five
//! budgets, none of which is `MaxRetriesPerStep` — the codec refuses a sixth,
//! so the task cannot state it either. This file runs the same question under
//! the numbers the phone runs under.

mod common;

use task_engine::budget::{BudgetDefaults, BudgetKind, TaskBudgets};
use task_engine::command::Command;

/// The budgets `core_api_command_factory_start_task.cc` puts on a web errand,
/// with the source ceiling the consent preview fixes.
fn errand_budgets() -> TaskBudgets {
    TaskBudgets::none()
        .with(BudgetKind::MaxSources, 1)
        .with(BudgetKind::MaxModelRequests, 160)
        .with(BudgetKind::MaxInputTokensOrBytes, 0)
        .with(BudgetKind::MaxOutputTokensOrBytes, 0)
        .with(BudgetKind::MaxCost, 0)
        .with(BudgetKind::MaxRetriesPerStep, 2)
}

/// A running task built the way the product builds one: budgets through the
/// consent preview, and `BudgetDefaults::uniform(0)` behind them.
fn running_in_production() -> common::Fixture {
    let mut fixture = common::draft_from_within(common::seed(), BudgetDefaults::uniform(0));
    let mut consented = common::preview();
    consented.budgets = errand_budgets();
    fixture.must_apply(Command::StartTask(consented));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

/// A turn the provider refuses for a retryable reason may be paid for again.
///
/// `can_afford_model_attempt` conjoins the per-step retry limit with the total
/// request budget, and it is the *per-step* half that used to fail. Nothing
/// stated that limit, so it resolved to `ProductionTaskFactory`'s fail-closed
/// zero and `retries_started < limit` read `0 < 0` — false on the first
/// attempt of every turn, in every task, which left `verdict`, the backoff
/// ladder, `SEMANTIC_RETRY_ATTEMPTS` and the failover list compiled in and
/// unreachable. Every other fixture here runs under `common::defaults()`,
/// which states the limit, so no test in this suite could see it
/// (decision 0218).
#[test]
fn a_first_attempt_is_retryable_under_the_products_own_budgets() {
    let mut fixture = running_in_production();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn { call_id });

    assert_eq!(
        fixture
            .reducer
            .task()
            .budgets()
            .effective_limit(BudgetKind::MaxRetriesPerStep, &BudgetDefaults::uniform(0)),
        2,
        "the start command states the retry limit, so the fail-closed zero \
         behind it is never reached",
    );
    assert!(
        fixture.reducer.can_afford_a_model_turn(),
        "the total request budget is nowhere near spent",
    );
    assert!(
        fixture.reducer.can_afford_model_attempt(),
        "a retryable provider failure on the first attempt must be retryable",
    );
}
