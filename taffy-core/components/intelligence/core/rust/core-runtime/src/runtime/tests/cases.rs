// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

mod open;

#[test]
fn profile_cancellation_drops_a_pending_task_open_exactly_once() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = empty_runtime(false, &calls);
    let task_id = task_id();
    let operation_id = operation_id("open-task-1");
    let begin = runtime.begin_open_task(
        load(task_id.as_str()),
        operation_id.clone(),
        Deadline::from_millis(100),
        1,
        1,
    );
    assert!(begin.is_ok());

    let terminal = runtime.cancel_operation(&operation_id, CancellationReason::User);
    assert!(matches!(
        terminal,
        Ok(terminal)
            if terminal.task_id == task_id
                && terminal.completion.operation_id == operation_id
                && terminal.completion.body
                    == Completion::Cancelled(CancellationReason::User)
    ));
    assert!(runtime.task(&task_id).is_none());
    assert_eq!(runtime.pending_len(), 0);
    assert_eq!(
        runtime.cancel_operation(&operation_id, CancellationReason::User),
        Err(TaskCompletionError::Terminal(
            crate::pending::CompletionError::DuplicateTerminal
        ))
    );
}

#[test]
fn profile_cancellation_resolves_an_active_task_operation_exactly_once() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let operation_id = operation_id("model-1");
    let staged = runtime.stage_effect(
        &task_id,
        operation_id.clone(),
        Deadline::from_millis(100),
        IdempotencyKey::new("effect-1"),
        EffectRequest::Model(BoundedPayload::empty()),
        1,
    );
    assert!(staged.is_ok());

    let terminal = runtime.cancel_operation(&operation_id, CancellationReason::TaskSettled);
    assert!(matches!(
        terminal,
        Ok(terminal)
            if terminal.task_id == task_id
                && terminal.completion.operation_id == operation_id
                && terminal.completion.body
                    == Completion::Cancelled(CancellationReason::TaskSettled)
    ));
    assert!(runtime.task(&task_id).is_some());
    assert_eq!(runtime.pending_len(), 0);
    assert_eq!(
        runtime.cancel_operation(&operation_id, CancellationReason::TaskSettled),
        Err(TaskCompletionError::Terminal(
            crate::pending::CompletionError::DuplicateTerminal
        ))
    );
}

#[test]
fn successful_commit_releases_task_effects_exactly_once() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let commit = begin_for(&mut runtime, &task_id, "commit-1");
    assert!(!runtime.operation_is_terminal(&commit.operation_id));
    let terminal = storage_success(&commit);
    let outcome = runtime.complete_commit(&task_id, terminal.clone(), 2);

    assert!(matches!(
        outcome,
        Ok(CommitOutcome::Committed(accepted))
            if accepted.effects == vec![Effect::ReleaseTaskTabs]
    ));
    assert!(runtime.operation_is_terminal(&commit.operation_id));
    assert_eq!(
        runtime.complete_effect(&task_id, terminal, 3),
        Err(TaskCompletionError::Terminal(
            crate::pending::CompletionError::DuplicateTerminal
        ))
    );
}

#[test]
fn storage_completion_must_report_the_exact_committed_revision() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let commit = begin_for(&mut runtime, &task_id, "commit-1");
    let terminal = commit.with_body(Completion::StorageCommitted {
        committed_revision: u64::MAX,
    });

    assert_eq!(
        runtime.complete_commit(&task_id, terminal, 2),
        Err(CommitCompletionError::Terminal(
            crate::pending::CompletionError::MetadataMismatch
        ))
    );
    assert_eq!(runtime.commit_in_flight(&task_id), Some(true));
}

#[test]
fn kill_before_commit_never_releases_effects_and_requires_replay() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let commit = begin_for(&mut runtime, &task_id, "commit-1");
    let next = ServiceGeneration::INITIAL
        .next()
        .unwrap_or_else(|| unreachable!());
    let terminals = runtime.disconnected(next).unwrap_or_default();

    assert_eq!(runtime.recovery_required(&task_id), Some(true));
    assert!(terminals.iter().any(|terminal| {
        terminal.completion.operation_id == commit.operation_id
            && terminal.completion.body == Completion::Failed(RuntimeError::CoreUnavailable)
    }));
    assert!(matches!(
        runtime.begin_submit(
            &task_id,
            command(),
            operation_id("commit-2"),
            Deadline::from_millis(200),
            2,
            2,
        ),
        Err(SubmitError::RecoveryRequired { .. })
    ));
}

#[test]
fn failed_commit_never_releases_effects_and_requires_replay() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let commit = begin_for(&mut runtime, &task_id, "commit-1");
    let terminal = commit.with_body(Completion::Failed(RuntimeError::StorageConflict));
    let outcome = runtime.complete_commit(&task_id, terminal, 2);

    assert!(matches!(
        outcome,
        Ok(CommitOutcome::RecoveryRequired { .. })
    ));
    assert_eq!(runtime.recovery_required(&task_id), Some(true));
    assert_eq!(runtime.commit_in_flight(&task_id), Some(false));
}

#[test]
fn encoding_failure_after_reducer_apply_fails_closed() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(true, &calls);
    let task_id = task_id();
    let result = runtime.begin_submit(
        &task_id,
        command(),
        operation_id("commit-1"),
        Deadline::from_millis(100),
        1,
        1,
    );

    assert!(matches!(result, Err(SubmitError::StorageEncoding { .. })));
    assert_eq!(runtime.recovery_required(&task_id), Some(true));
    assert_eq!(runtime.pending_len(), 0);
}

#[test]
fn profile_runtime_keeps_multiple_task_commits_independent() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let first = task_id();
    let second = TaskId::new("task-2");
    must_open(&mut runtime, second.as_str());

    let first_commit = begin_for(&mut runtime, &first, "commit-1");
    let second_commit = begin_for(&mut runtime, &second, "commit-2");
    assert_eq!(runtime.task_count(), 2);
    assert_eq!(runtime.commit_in_flight(&first), Some(true));
    assert_eq!(runtime.commit_in_flight(&second), Some(true));

    let first_terminal = storage_success(&first_commit);
    assert!(matches!(
        runtime.complete_commit(&first, first_terminal, 2),
        Ok(CommitOutcome::Committed(_))
    ));
    assert_eq!(runtime.commit_in_flight(&first), Some(false));
    assert_eq!(runtime.commit_in_flight(&second), Some(true));

    let second_terminal = storage_success(&second_commit);
    assert!(matches!(
        runtime.complete_commit(&second, second_terminal, 2),
        Ok(CommitOutcome::Committed(_))
    ));
}

#[test]
fn profile_task_table_is_bounded_and_refuses_duplicates() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    assert!(matches!(
        runtime.begin_open_task(
            load("task-1"),
            operation_id("duplicate-open"),
            Deadline::from_millis(100),
            1,
            1,
        ),
        Err(OpenTaskError::AlreadyOpen)
    ));
    for ordinal in 1..MAX_TASK_SESSIONS_PER_PROFILE {
        let task_name = format!("task-{}", ordinal.saturating_add(1));
        must_open_with_workspace(
            &mut runtime,
            &task_name,
            u8::try_from(ordinal).unwrap_or(u8::MAX),
        );
    }
    assert_eq!(runtime.task_count(), MAX_TASK_SESSIONS_PER_PROFILE);
    assert!(matches!(
        runtime.begin_open_task(
            load("task-over-limit"),
            operation_id("over-limit-open"),
            Deadline::from_millis(100),
            1,
            1,
        ),
        Err(OpenTaskError::Saturated)
    ));
}

#[test]
fn a_commit_past_its_deadline_is_terminal_only_after_a_sweep() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let commit = begin_for(&mut runtime, &task_id, "commit-1");

    // One millisecond short of the boundary the operation is still the
    // browser's to answer. A sweep is not a heuristic and may not end work
    // whose deadline has not arrived.
    assert!(runtime.expire_due(99).is_empty());
    assert_eq!(runtime.pending_len(), 1);
    assert_eq!(runtime.commit_in_flight(&task_id), Some(true));
    assert_eq!(runtime.recovery_required(&task_id), Some(false));

    let terminals = runtime.expire_due(100);

    assert!(matches!(
        terminals.first(),
        Some(terminal)
            if terminal.task_id == task_id
                && terminal.completion.operation_id == commit.operation_id
                && terminal.completion.body
                    == Completion::Failed(RuntimeError::DeadlineExceeded)
    ));
    assert_eq!(terminals.len(), 1);
    assert_eq!(runtime.pending_len(), 0);
    assert_eq!(runtime.commit_in_flight(&task_id), Some(false));
    // The applied command never became durable, so the in-memory task is a
    // revision ahead of storage and has to be discarded and replayed.
    assert_eq!(runtime.recovery_required(&task_id), Some(true));
    // The sweep produced this operation's one terminal answer. A completion
    // the browser delivers afterwards changes nothing.
    assert_eq!(
        runtime.complete_commit(&task_id, storage_success(&commit), 101),
        Err(CommitCompletionError::NoCommitPending)
    );
    assert!(runtime.expire_due(1_000).is_empty());
}

#[test]
fn an_observer_sees_the_durable_commit_and_cannot_change_the_outcome() {
    let task_id = task_id();

    // The unobserved run.
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut plain = runtime(false, &calls);
    let commit = begin_for(&mut plain, &task_id, "commit-1");
    let plain_outcome = plain.complete_commit(&task_id, storage_success(&commit), 2);

    // The observed run, driven identically.
    let seen = Rc::new(RefCell::new(Vec::new()));
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut observed = runtime_observed(
        &calls,
        Box::new(RecordingObserver {
            seen: Rc::clone(&seen),
        }),
    );
    let commit = begin_for(&mut observed, &task_id, "commit-1");
    let observed_outcome = observed.complete_commit(&task_id, storage_success(&commit), 2);

    // Observation changed nothing the caller can see.
    let (Ok(CommitOutcome::Committed(plain_accepted)), Ok(CommitOutcome::Committed(accepted))) =
        (plain_outcome, observed_outcome)
    else {
        unreachable!()
    };
    assert_eq!(plain_accepted.revision, accepted.revision);
    assert_eq!(plain_accepted.to, accepted.to);
    assert_eq!(plain_accepted.effects, accepted.effects);
    assert_eq!(
        plain.commit_in_flight(&task_id),
        observed.commit_in_flight(&task_id)
    );
    assert_eq!(plain.pending_len(), observed.pending_len());

    // And the observer saw exactly the fact that became durable.
    assert_eq!(
        seen.borrow().as_slice(),
        &[(
            task_id.as_str().to_owned(),
            format!("{:?}", accepted.to),
            accepted.revision
        )]
    );
}
