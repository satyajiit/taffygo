// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable accounting and sequence rules for paid model sub-attempts.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::{Command, ModelAttemptKind, ModelCallId};

#[test]
fn each_subattempt_is_charged_and_candidate_progress_is_durable() {
    let mut fixture = common::running_within(
        task_engine::TaskBudgets::none().with(task_engine::BudgetKind::MaxRetriesPerStep, 2),
    );
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });

    for (attempt_ordinal, candidate_ordinal, kind, expected_spend) in [
        (1, 0, ModelAttemptKind::Retry, 2),
        (2, 1, ModelAttemptKind::Failover, 3),
    ] {
        let accepted = fixture
            .apply(Command::RequestModelAttempt {
                call_id: call_id.clone(),
                attempt_ordinal,
                candidate_ordinal,
                kind,
            })
            .expect("the exact next paid attempt is admitted");
        assert_eq!(
            accepted.effects,
            vec![task_engine::Effect::CallModel {
                call_id: call_id.clone()
            }]
        );
        assert_eq!(
            fixture
                .reducer
                .task()
                .ledger()
                .spent(task_engine::BudgetKind::MaxModelRequests),
            expected_spend
        );
    }

    let turn = fixture
        .reducer
        .model_turn()
        .expect("the turn stays in flight");
    assert_eq!(turn.attempts_started(), 3);
    assert_eq!(turn.candidate_ordinal(), 1);
}

#[test]
fn subattempt_sequence_and_kind_are_not_caller_chosen() {
    for command in [
        Command::RequestModelAttempt {
            call_id: ModelCallId::new("placeholder"),
            attempt_ordinal: 2,
            candidate_ordinal: 0,
            kind: ModelAttemptKind::Retry,
        },
        Command::RequestModelAttempt {
            call_id: ModelCallId::new("placeholder"),
            attempt_ordinal: 1,
            candidate_ordinal: 1,
            kind: ModelAttemptKind::Retry,
        },
        Command::RequestModelAttempt {
            call_id: ModelCallId::new("placeholder"),
            attempt_ordinal: 1,
            candidate_ordinal: 0,
            kind: ModelAttemptKind::Failover,
        },
    ] {
        let mut fixture = common::running_within(
            task_engine::TaskBudgets::none().with(task_engine::BudgetKind::MaxRetriesPerStep, 2),
        );
        let call_id = fixture.reducer.next_model_call_id();
        fixture.must_apply(Command::RequestModelTurn {
            call_id: call_id.clone(),
        });
        let command = match command {
            Command::RequestModelAttempt {
                attempt_ordinal,
                candidate_ordinal,
                kind,
                ..
            } => Command::RequestModelAttempt {
                call_id,
                attempt_ordinal,
                candidate_ordinal,
                kind,
            },
            _ => unreachable!(),
        };
        let refusal = fixture
            .apply(command)
            .expect_err("an invented subattempt sequence is refused");
        assert_eq!(
            refusal.reason,
            task_engine::RefusalReason::ModelTurnMismatch
        );
        assert_eq!(
            fixture
                .reducer
                .task()
                .ledger()
                .spent(task_engine::BudgetKind::MaxModelRequests),
            1
        );
    }
}

#[test]
fn subattempts_obey_both_total_request_and_per_step_retry_limits() {
    for budgets in [
        task_engine::TaskBudgets::none().with(task_engine::BudgetKind::MaxModelRequests, 1),
        task_engine::TaskBudgets::none().with(task_engine::BudgetKind::MaxRetriesPerStep, 0),
    ] {
        let mut fixture = common::running_within(budgets);
        let call_id = fixture.reducer.next_model_call_id();
        fixture.must_apply(Command::RequestModelTurn {
            call_id: call_id.clone(),
        });
        assert!(!fixture.reducer.can_afford_model_attempt());
        let refusal = fixture
            .apply(Command::RequestModelAttempt {
                call_id,
                attempt_ordinal: 1,
                candidate_ordinal: 0,
                kind: ModelAttemptKind::Retry,
            })
            .expect_err("the exhausted attempt is refused before charging");
        assert_eq!(refusal.reason, task_engine::RefusalReason::BudgetExhausted);
    }
}
