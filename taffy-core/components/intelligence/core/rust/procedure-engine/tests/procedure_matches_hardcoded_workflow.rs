// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The conformance oracle of decision 0055, validation 1.
//!
//! One task is driven through `next_reviewed_command` — the hard-coded
//! `BuildSourceTable` workflow — and through `next_procedure_command` replaying
//! the shipped procedure, and the two must emit an **identical** `Command`
//! sequence.
//!
//! # Why whole commands and never kinds
//!
//! `CommandKind` is a coarser thing than it looks. Two rows of the agent
//! decision table both answer `FailTask` and differ only in `FailureReason`,
//! so a comparison over kinds would call a task that ran out of attempts equal
//! to one whose source was refused. It would also call every `AdvanceStep`
//! equal to every other regardless of which step or which state, and every
//! `ProposeAction` equal to every other regardless of the tool, the tab, the
//! effect identity or the digest a capability is about to be bound to — which
//! is the entire content of the claim being tested. `Command` implements
//! `PartialEq`, so the assertion is the whole value.
//!
//! # What the oracle is and is not evidence of
//!
//! The two paths share exactly two things, both deliberate and both named
//! where they live: `task_engine::proposal` canonicalises a proposal's
//! identity, and `task_engine::template` owns the prose a person reads. If
//! either were written twice the comparison would fail over a detail nobody is
//! trying to test. Everything else — whether to propose, advance, wait or
//! fail; which tool at which step on which attempt; whether the result is
//! complete or partial — is decided independently on each side, and that is
//! what these tests are about.
//!
//! Passing does not make replay trusted. Every command either path emits is
//! still committed through the same reducer, decided by `policy-engine`,
//! minted as a capability and verified by its postcondition. The oracle is
//! about the two paths agreeing, and about nothing downstream of them.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

#[path = "common/reviewed_task.rs"]
mod reviewed_task;

use procedure_engine::builtin::build_source_table;
use procedure_engine::field::ObservedFields;
use procedure_engine::record::Procedure;
use procedure_engine::replay::{next_procedure_command, ReplayRefusal};
use reviewed_task::{complete_observation, procedure_for_task, seed, Digest, Driver};
use task_engine::{Command, ObservationCompleteness, StepState, TaskState, WorkflowError};

/// Both answers for one reducer state, as a pair a test can compare.
fn both(driver: &Driver, procedure: &Procedure) -> (Option<Command>, Option<Command>) {
    let reviewed = driver
        .reducer
        .next_reviewed_command(&Digest)
        .expect("the reviewed workflow can say what happens next");
    let replayed =
        next_procedure_command(&driver.reducer, procedure, ObservedFields::none(), &Digest)
            .expect("the procedure can say what happens next");
    (reviewed, replayed)
}

/// Drives `driver` to a terminal state through the reviewed path, checking at
/// every committed step that the procedure path would have said the same
/// thing.
///
/// The reviewed path is the one that actually advances the task, so the two
/// are compared against one reducer state rather than against two reducers
/// that could have diverged into different states and then agreed about
/// different worlds.
fn drive_in_lockstep(driver: &mut Driver, procedure: &Procedure) -> Vec<Command> {
    let mut emitted = Vec::new();
    loop {
        let (reviewed, replayed) = both(driver, procedure);
        assert_eq!(
            reviewed,
            replayed,
            "the two paths disagreed after {} commands: {emitted:?}",
            emitted.len()
        );
        let Some(command) = reviewed else { break };
        emitted.push(command.clone());
        driver.apply(command);
        assert!(
            emitted.len() < 32,
            "the reviewed workflow did not terminate"
        );
    }
    emitted
}

#[test]
fn one_completing_task_produces_one_command_sequence() {
    let mut driver = Driver::new();
    driver.start();
    let procedure = procedure_for_task();

    // Up to the point where the observation has been dispatched: this covers
    // `ExecutorStarted`, `SetPlan`, the two `AdvanceStep`s and the proposal
    // whose digest and effect identity are the whole reason the comparison is
    // over whole commands.
    let before = drive_in_lockstep(&mut driver, &procedure);
    assert!(
        matches!(
            before.as_slice(),
            [Command::ExecutorStarted, Command::SetPlan(_), ..]
        ),
        "{before:?}"
    );
    assert!(before
        .iter()
        .any(|command| matches!(command, Command::ProposeAction(_))));

    let action_id = driver.authorize_pending_proposal();
    complete_observation(&mut driver, action_id, ObservationCompleteness::Complete);

    let after = drive_in_lockstep(&mut driver, &procedure);
    assert_eq!(driver.reducer.task().state(), TaskState::Completed);
    assert!(matches!(
        after.last(),
        Some(Command::CompleteResultValidated(_))
    ));
}

#[test]
fn an_incomplete_observation_reaches_the_same_partial_result_on_both_paths() {
    // The two paths could agree on every command up to the last one and still
    // differ on the only one that decides what the person is told, because
    // `TaskResult` carries the labelled gap as prose. That gap is the
    // template's on both sides, and this is the test that says so.
    let mut driver = Driver::new();
    driver.start();
    let procedure = procedure_for_task();
    drive_in_lockstep(&mut driver, &procedure);
    let action_id = driver.authorize_pending_proposal();
    complete_observation(&mut driver, action_id, ObservationCompleteness::Incomplete);

    let after = drive_in_lockstep(&mut driver, &procedure);
    assert_eq!(driver.reducer.task().state(), TaskState::Partial);
    let Some(Command::PartialResultValidated(result)) = after.last() else {
        panic!("the incomplete observation ended partial: {after:?}")
    };
    assert_eq!(result.unmet.len(), 1);
}

#[test]
fn a_refused_observation_fails_the_task_the_same_way_on_both_paths() {
    // `FailTask` is the command whose payload a `CommandKind` comparison would
    // discard, so it gets its own case: both paths must reach the same
    // `FailureReason`, not merely the same decision to stop.
    let mut driver = Driver::new();
    driver.start();
    let procedure = procedure_for_task();
    drive_in_lockstep(&mut driver, &procedure);
    driver.deny_pending_proposal();

    let (reviewed, replayed) = both(&driver, &procedure);
    assert_eq!(reviewed, replayed);
    assert!(
        matches!(reviewed, Some(Command::FailTask { .. })),
        "{reviewed:?}"
    );
}

#[test]
fn the_two_paths_agree_that_a_dispatched_action_means_wait() {
    // `Ok(None)` is a command in the sequence too — it is the answer that
    // stops the runtime asking again — so the paths have to agree about it as
    // exactly as they agree about `ProposeAction`.
    let mut driver = Driver::new();
    driver.start();
    let procedure = procedure_for_task();
    drive_in_lockstep(&mut driver, &procedure);
    driver.dispatch_pending_proposal();

    let (reviewed, replayed) = both(&driver, &procedure);
    assert_eq!(reviewed, None);
    assert_eq!(replayed, None);
}

#[test]
fn the_proposal_the_two_paths_build_is_the_same_proposal_field_by_field() {
    // The identical-command assertion already covers this, and it is worth
    // failing separately: if the two ever diverge on the effect identity or
    // the digest, the sequence comparison reports "two long `Debug` values
    // differ" and this reports which field.
    let mut driver = Driver::new();
    let procedure = procedure_for_task();
    driver.start();
    // Executor started, plan set, the observation step moved to running: the
    // next thing either path says is the proposal itself.
    driver.apply_reviewed();
    driver.apply_reviewed();
    driver.apply_reviewed();

    let (reviewed, replayed) = both(&driver, &procedure);
    // Both are now looking at a task whose observation step is running with no
    // action against it yet.
    let (Some(Command::ProposeAction(left)), Some(Command::ProposeAction(right))) =
        (reviewed, replayed)
    else {
        panic!("both paths propose the observation here")
    };
    assert_eq!(left.intent(), right.intent());
    assert_eq!(left.tool_name(), right.tool_name());
    assert_eq!(left.action_class(), right.action_class());
    assert_eq!(left.idempotency(), right.idempotency());
    assert_eq!(left.idempotency_key, right.idempotency_key);
    assert_eq!(left.plan_step_id, right.plan_step_id);
    assert_eq!(left.tab_id(), right.tab_id());
    assert_eq!(left.node_id(), right.node_id());
    assert_eq!(left.required_for_step, right.required_for_step);
    assert_eq!(left.budget_draw, right.budget_draw);
    assert_eq!(left.proposal_digest, right.proposal_digest);
    assert_eq!(left, right);
}

#[test]
fn a_replaced_plan_is_refused_by_both_paths_rather_than_worked_through() {
    // A plan is revisable, and neither path may work through one that is not
    // the sequence it is about. The reviewed workflow checks a shape; the
    // procedure checks its own step count and that the last step extracts from
    // the one before it. Two different checks, and they have to agree.
    let mut driver = Driver::new();
    let procedure = procedure_for_task();
    driver.start();
    driver.apply_reviewed();
    driver.apply(Command::SetPlan(task_engine::PlanDraft {
        summary: "Something else entirely".to_owned(),
        steps: vec![task_engine::StepDraft {
            kind: task_engine::StepKind::Infer,
            description: "Ask a model".to_owned(),
            dependencies: Vec::new(),
        }],
    }));

    assert_eq!(
        driver.reducer.next_reviewed_command(&Digest),
        Err(WorkflowError::UnexpectedPlan)
    );
    assert_eq!(
        next_procedure_command(&driver.reducer, &procedure, ObservedFields::none(), &Digest),
        Err(ReplayRefusal::UnexpectedPlan)
    );
}

#[test]
fn the_procedure_path_refuses_a_task_whose_allowlist_does_not_admit_its_verb() {
    // The narrowing is a precondition of replaying at all. This is the half the
    // reviewed workflow expresses as its own configuration check, and the two
    // refuse the same task — with each one's own vocabulary, because they are
    // refusing it for their own reason.
    let mut invalid = seed();
    invalid.snapshot.tool_allowlist = vec!["browser.dom.query".to_owned()];
    let mut driver = Driver::from_seed(invalid);
    driver.start();

    assert_eq!(
        driver.reducer.next_reviewed_command(&Digest),
        Err(WorkflowError::InvalidWorkflowConfiguration)
    );
    assert_eq!(
        next_procedure_command(
            &driver.reducer,
            &procedure_for_task(),
            ObservedFields::none(),
            &Digest
        ),
        Err(ReplayRefusal::VerbNotAdmitted)
    );
}

#[test]
fn a_procedure_about_another_origin_replays_nothing_here() {
    // The reviewed workflow has no scope of its own, so this is the one place
    // the two are *expected* to differ: a record is about an origin and a
    // hard-coded sequence is not. It is asserted rather than left implicit,
    // because it is the property that makes deleting the hard-coded half a
    // narrowing rather than a widening.
    let driver = {
        let mut driver = Driver::new();
        driver.start();
        driver
    };
    let Ok(elsewhere) = policy_engine::origin::normalize_serialization("https://other.test") else {
        unreachable!("the fixture origin is an origin")
    };
    let Ok(procedure) = build_source_table(&elsewhere) else {
        unreachable!("the built-in is well formed")
    };
    assert!(driver.reducer.next_reviewed_command(&Digest).is_ok());
    assert_eq!(
        next_procedure_command(&driver.reducer, &procedure, ObservedFields::none(), &Digest),
        Err(ReplayRefusal::ScopeDoesNotCoverTheSource)
    );
}

#[test]
fn a_draft_procedure_replays_nothing_however_well_it_matches() {
    let mut driver = Driver::new();
    driver.start();
    let mut draft = procedure_for_task();
    draft.status = procedure_engine::ProcedureStatus::Draft;
    assert_eq!(
        next_procedure_command(&driver.reducer, &draft, ObservedFields::none(), &Digest),
        Err(ReplayRefusal::NotRunnable)
    );
}

#[test]
fn the_two_paths_retry_the_same_number_of_times_and_give_up_together() {
    // The retry ceiling is the one rule the two paths express from different
    // facts: the reviewed workflow writes `MAX_READ_ATTEMPTS` down, and the
    // procedure path derives it from the verb's `RecoveryRule`. Two constants
    // that agree today and are free to drift, because every other case here
    // settles on the first attempt and never asks for a second.
    //
    // A divergence would not look like a failure. If the procedure path
    // allowed one more attempt than the reviewed path, deleting the hard-coded
    // half would silently give every replayed read an extra go at the page;
    // if it allowed one fewer, a task that survives process death today would
    // start failing. This is the case that makes either one a test failure.
    let mut driver = Driver::new();
    driver.start();
    let procedure = procedure_for_task();

    drive_in_lockstep(&mut driver, &procedure);
    let first = driver.dispatch_pending_proposal();
    driver.fail_dispatched_action(first);

    // Both paths must ask for a second attempt, and must ask for it with the
    // same effect identity — the attempt number is part of the idempotency
    // key, and this is the only case in the file where it is not 1.
    let second = drive_in_lockstep(&mut driver, &procedure);
    assert!(
        second
            .iter()
            .any(|command| matches!(command, Command::ProposeAction(_))),
        "a failed read is retried once: {second:?}"
    );

    let retried = driver.dispatch_pending_proposal();
    driver.fail_dispatched_action(retried);

    // The ceiling. Neither path may propose a third read.
    let (reviewed, replayed) = both(&driver, &procedure);
    assert_eq!(reviewed, replayed);
    assert!(
        matches!(reviewed, Some(Command::FailTask { .. })),
        "the second failure exhausts the reads: {reviewed:?}"
    );
}

#[test]
fn the_step_states_the_two_paths_walk_are_the_same_states() {
    // A sequence comparison would pass if both paths skipped the same step, so
    // the states the plan actually visited are pinned separately.
    let mut driver = Driver::new();
    driver.start();
    let procedure = procedure_for_task();
    let commands = drive_in_lockstep(&mut driver, &procedure);
    let advances: Vec<StepState> = commands
        .iter()
        .filter_map(|command| match command {
            Command::AdvanceStep { to, .. } => Some(*to),
            _ => None,
        })
        .collect();
    assert_eq!(advances, vec![StepState::Running]);
}
