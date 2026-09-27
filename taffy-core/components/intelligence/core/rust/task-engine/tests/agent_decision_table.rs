// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The agent decision table, driven row by row.
//!
//! The table lives in `task_engine::agent`'s `table` module documentation and
//! this file is what keeps it honest. Every row below names the row it drives.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::agent::TurnResidency;
use task_engine::{
    loop_tool_result, AgentError, CallVerdict, Command, Milestone, ModelCallId, ModelStopReason,
    NotAttempted, TurnGap, SEARCH_TOOLS,
};

use common::agent::{
    page, read_call, reply, running, search_tools_call, with_recorded_turn, Digest,
};

#[test]
fn row_2_a_queued_task_asks_for_an_executor() {
    let fixture = common::in_state(task_engine::TaskState::Queued, false);
    assert_eq!(
        fixture.reducer.next_agent_command(None, &Digest).unwrap(),
        Some(Command::ExecutorStarted)
    );
}

#[test]
fn rows_1_and_3_only_queued_running_or_completing_have_a_next_command() {
    for state in task_engine::TaskState::ALL {
        if matches!(
            state,
            task_engine::TaskState::Queued
                | task_engine::TaskState::Running
                | task_engine::TaskState::Completing
        ) {
            continue;
        }
        let fixture = common::in_state(*state, false);
        assert_eq!(
            fixture.reducer.next_agent_command(None, &Digest).unwrap(),
            None,
            "{}",
            state.label()
        );
    }
}

#[test]
fn row_4_a_running_task_with_no_turn_asks_the_model_and_the_identity_is_derived() {
    let fixture = running();
    let expected = fixture.reducer.next_model_call_id();
    assert_eq!(
        fixture.reducer.next_agent_command(None, &Digest).unwrap(),
        Some(Command::RequestModelTurn {
            call_id: expected.clone()
        })
    );
    // Derived rather than supplied. An identity the caller invented is refused
    // by the guard, which is what makes a replay reach the same one.
    let mut fixture = fixture;
    let envelope = fixture.envelope(Command::RequestModelTurn {
        call_id: ModelCallId::new("something-else"),
    });
    let refusal = fixture
        .reducer
        .apply(envelope)
        .expect_err("an invented identity is refused");
    assert_eq!(refusal.reason, task_engine::RefusalReason::ModelCallNotNext);
}

#[test]
fn row_4_asking_charges_the_budget_and_emits_the_call() {
    let mut fixture = running();
    let call_id = fixture.reducer.next_model_call_id();
    let envelope = fixture.envelope(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let accepted = fixture.reducer.apply(envelope).expect("the turn is asked");
    assert_eq!(
        accepted.effects,
        vec![task_engine::Effect::CallModel { call_id }]
    );
    assert_eq!(fixture.reducer.turns_started(), 1);
    assert!(fixture.reducer.model_turn_in_flight());
    // The charge happens when the call leaves, not when an answer comes back,
    // so an unanswered turn still costs what it cost.
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(task_engine::BudgetKind::MaxModelRequests),
        1
    );
}

#[test]
fn row_5_a_call_in_flight_with_no_reply_waits() {
    let mut fixture = running();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn { call_id });
    assert_eq!(
        fixture.reducer.next_agent_command(None, &Digest).unwrap(),
        None
    );
}

#[test]
fn row_6_a_reply_to_another_call_is_refused_rather_than_attributed() {
    let mut fixture = running();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn { call_id });
    let (page, _) = page();
    let stranger = TurnResidency::read(
        ModelCallId::new("model-someone-else-0"),
        page,
        reply(ModelStopReason::Complete, Vec::new()),
    )
    .expect("a readable reply");
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&stranger), &Digest)
            .unwrap_err(),
        AgentError::ResidencyMismatch
    );
}

#[test]
fn row_7_a_matching_reply_is_recorded_as_shape_and_never_as_content() {
    let mut fixture = running();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let (page, handle) = page();
    let residency = TurnResidency::read(
        call_id.clone(),
        page,
        reply(ModelStopReason::ToolCall, vec![read_call(handle)]),
    )
    .expect("a readable reply");
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::RecordModelTurn {
        call_id: id,
        digest,
    }) = next
    else {
        panic!("expected a recorded turn, got {next:?}");
    };
    assert_eq!(id, call_id);
    assert_eq!(digest.stop, ModelStopReason::ToolCall);
    assert_eq!(digest.tool_calls, 1);
    assert_eq!(digest.refused_tool_calls, 0);
    assert_eq!(digest.render.digest, [7_u8; 32]);
}

#[test]
fn rows_8_to_10_each_gap_has_its_own_answer() {
    for (gap, expected) in [
        (TurnGap::Cancelled, None),
        (
            TurnGap::Refused,
            Some(Command::FailTask {
                reason: task_engine::FailureReason::ProviderUnavailable,
            }),
        ),
        (
            TurnGap::Unavailable,
            Some(Command::FailTask {
                reason: task_engine::FailureReason::ProviderUnavailable,
            }),
        ),
    ] {
        let mut fixture = running();
        let call_id = fixture.reducer.next_model_call_id();
        fixture.must_apply(Command::RequestModelTurn {
            call_id: call_id.clone(),
        });
        fixture.must_apply(Command::RecordModelTurnGap { call_id, gap });
        assert_eq!(
            fixture.reducer.next_agent_command(None, &Digest).unwrap(),
            expected,
            "{}",
            gap.label()
        );
    }
}

/// The rule this test is named for is unchanged, and the answer it asserts is
/// not. An unknown outcome may never produce another paid identity by itself;
/// it used to buy that by failing the task, which also threw away every
/// verified action the task held. A pause buys the same thing and keeps the
/// work — the next identity is minted only if a person resumes (decision
/// 0217).
#[test]
fn row_10_an_unknown_outcome_never_mints_fresh_paid_work() {
    let mut fixture = running();
    let first = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: first.clone(),
    });
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id: first.clone(),
        gap: TurnGap::OutcomeUnknown,
    });
    let next = fixture.reducer.next_agent_command(None, &Digest).unwrap();
    assert_eq!(
        next,
        Some(Command::PauseTask {
            cause: task_engine::PauseCause::NoAnswer,
        })
    );
    // The half the name promises, asserted rather than implied: whatever this
    // row answers, it is never a fresh turn.
    assert!(
        !matches!(next, Some(Command::RequestModelTurn { .. })),
        "row 10 may never mint another paid identity: {next:?}"
    );
}

/// And the hold is a real one the person can lift: applying it leaves the task
/// settling toward `PAUSED` rather than in a terminal state, which is the
/// whole difference between this and the failure it replaced.
#[test]
fn row_10_leaves_the_task_holdable_rather_than_ended() {
    let mut fixture = running();
    let first = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: first.clone(),
    });
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id: first.clone(),
        gap: TurnGap::OutcomeUnknown,
    });
    fixture.must_apply(Command::PauseTask {
        cause: task_engine::PauseCause::NoAnswer,
    });
    assert!(
        !fixture.reducer.task().state().is_terminal(),
        "a held task is not an ended one: {:?}",
        fixture.reducer.task().state()
    );
}

#[test]
fn row_13_a_finished_answer_with_no_calls_offers_a_result() {
    let (fixture, residency) = with_recorded_turn(ModelStopReason::Complete, Vec::new());
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        Some(Command::ResultCandidateReady)
    );
}

#[test]
fn row_22_empty_research_becomes_a_stable_partial_result() {
    // The shared COMPLETING fixture owns an artifact and is therefore not an
    // empty research result. Enter the same state without requesting one so
    // this row continues to exercise the empty-result branch exactly.
    let mut fixture = common::running();
    fixture.must_apply(Command::ResultCandidateReady);
    let next = fixture.reducer.next_agent_command(None, &Digest).unwrap();
    let Some(Command::PartialResultValidated(result)) = next else {
        panic!("expected PartialResultValidated, got {next:?}");
    };
    assert!(!result.is_complete());
    assert_eq!(
        result.unmet,
        vec![task_engine::template::empty_research_result_gap()]
    );
    assert!(result.artifact_ids.is_empty());
    assert_eq!(result.source_count, 1);
    assert_eq!(result.fact_count, 0);
    fixture.must_apply(Command::PartialResultValidated(result));
    assert_eq!(
        fixture.reducer.task().state(),
        task_engine::TaskState::Partial
    );
}

#[test]
fn rows_14_and_19_a_reply_that_contradicts_itself_is_refused() {
    let (_, handle) = page();
    let (fixture, residency) =
        with_recorded_turn(ModelStopReason::Complete, vec![read_call(handle)]);
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_err(),
        AgentError::ContradictoryReply
    );

    let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, Vec::new());
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_err(),
        AgentError::ContradictoryReply
    );
}

#[test]
fn row_15_a_provider_error_fails_the_task_and_row_16_a_provider_stop_asks_the_person() {
    let (fixture, residency) = with_recorded_turn(ModelStopReason::Error, Vec::new());
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        Some(Command::FailTask {
            reason: task_engine::FailureReason::ProviderUnavailable,
        })
    );

    let (fixture, residency) = with_recorded_turn(ModelStopReason::ProviderStop, Vec::new());
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        Some(Command::RequestUserInput)
    );
}

#[test]
fn row_17_a_truncated_reply_attempts_none_of_the_calls_it_carries() {
    // The mandatory row. Two well-formed calls in a reply that stopped at the
    // token allowance: a truncated argument list is not a call anybody made,
    // and running the ones that happen to have parsed would execute a prefix
    // of an intention.
    let (_, handle) = page();
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::Length,
        vec![read_call(handle), read_call(handle)],
    );

    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(dispositions.len(), 2);
    for disposition in &dispositions {
        assert_eq!(
            disposition.verdict,
            CallVerdict::NotAttempted(NotAttempted::TruncatedArguments)
        );
    }
    // Every call refused on sight, and the durable record says so.
    let digest = residency.digest(&dispositions);
    assert_eq!(digest.tool_calls, 2);
    assert_eq!(digest.refused_tool_calls, 2);
    assert!(digest.every_call_refused_on_sight());

    // And nothing is proposed: the answer is another turn, not a dispatch.
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "a truncated reply must dispatch nothing, got {next:?}"
    );
}

#[test]
fn row_21f_a_settled_search_still_asks_again_even_with_a_visible_answer() {
    // No production row is terminal_safe, so row 21i cannot fire against
    // REGISTRY. A settled `tool.search` is the walk reaching the end on a
    // real tool: it is attemptable, it settled, and the fixture reply
    // carries answer_segments > 0. The follow-up is still a fresh turn.
    let entry = task_engine::resolve(SEARCH_TOOLS, Milestone::M3)
        .entry()
        .expect("tool.search is registered");
    assert!(
        !entry.terminal_safe,
        "tool.search must stay ordinary so this test names 21f, not 21i"
    );

    let (fixture, mut residency) =
        with_recorded_turn(ModelStopReason::ToolCall, vec![search_tools_call("pdf")]);
    let (sequence, call, entry) = fixture
        .reducer
        .pending_loop_call(&residency)
        .expect("the search is waiting");
    let tools = task_engine::EffectiveToolSet::for_task(Milestone::M3, &[]);
    let (outcome, result) = loop_tool_result(entry, call, &tools);
    assert!(residency.settle_loop_with_result(sequence, outcome, result));
    assert!(
        residency.reply().answer_segments > 0,
        "answer_segments does not skip the follow-up for tool.search"
    );

    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "a settled search must still start a fresh turn, got {next:?}"
    );
}

#[test]
fn a_task_that_cannot_afford_another_turn_fails_rather_than_waiting() {
    let mut fixture = common::running_within(
        task_engine::TaskBudgets::none().with(task_engine::BudgetKind::MaxModelRequests, 1),
    );
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id,
        gap: TurnGap::Unreadable,
    });
    assert!(!fixture.reducer.can_afford_a_model_turn());
    assert_eq!(
        fixture.reducer.next_agent_command(None, &Digest).unwrap(),
        Some(Command::FailTask {
            reason: task_engine::FailureReason::BudgetExhausted,
        })
    );
}

#[test]
fn settling_is_refused_while_a_paid_call_is_outstanding() {
    let mut fixture = running();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    fixture.must_apply(Command::CancelTask);
    let envelope = fixture.envelope(Command::CancelSettled);
    let refusal = fixture
        .reducer
        .apply(envelope)
        .expect_err("a paid call is still outstanding");
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ModelTurnInFlight
    );

    // The cancellation reaches the reducer as a positive fact, and then the
    // task may settle.
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id,
        gap: TurnGap::Cancelled,
    });
    fixture.must_apply(Command::CancelSettled);
    assert_eq!(fixture.state(), task_engine::TaskState::Cancelled);
}
