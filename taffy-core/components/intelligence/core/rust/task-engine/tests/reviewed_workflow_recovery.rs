// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordering, duplicate, and replay proofs for the reviewed local workflow.

#[path = "reviewed_workflow/common.rs"]
mod common;

use bip_types::identity::{ApprovalReceiptReference, DispatchId, MonotonicMillis};
use bip_types::ActionResultCode;
use common::{complete_observation, defaults, evidence, preview, seed, Digest, Driver};
use task_engine::{
    ActionOutcome, Authorization, Command, CommandEnvelope, IdempotencyKey, ManualClock,
    ObservationCompleteness, ProposalDecision, Reducer, SequentialIds, TraceId,
};

#[test]
fn duplicate_late_wrong_dispatch_and_out_of_order_terminals_fail_closed() {
    let mut driver = Driver::new();
    driver.apply(Command::StartTask(preview()));
    driver.apply(Command::AcceptInitialConsent(ApprovalReceiptReference(
        "consent-1".to_owned(),
    )));
    for _ in 0..4 {
        driver.apply_next();
    }
    let action_id = driver
        .reducer
        .actions()
        .next()
        .map_or_else(|| unreachable!(), |action| action.action_id().clone());
    let early = outcome_envelope(&driver, "early", action_id.clone(), "dispatch-1", 1);
    assert_eq!(
        driver.reducer.apply(early).err().map(|error| error.reason),
        Some(task_engine::RefusalReason::ActionOutcomeMismatch)
    );
    driver.apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: task_engine::CapabilityId::new("capability-1"),
        })),
        dispatch_id: Some(DispatchId::new("dispatch-1")),
    });
    let wrong = outcome_envelope(
        &driver,
        "wrong-dispatch",
        action_id.clone(),
        "dispatch-other",
        2,
    );
    assert_eq!(
        driver.reducer.apply(wrong).err().map(|error| error.reason),
        Some(task_engine::RefusalReason::ActionOutcomeMismatch)
    );
    let accepted_envelope = outcome_envelope(&driver, "terminal", action_id, "dispatch-1", 3);
    let first = driver
        .reducer
        .apply(accepted_envelope.clone())
        .unwrap_or_else(|_| unreachable!());
    let duplicate = driver
        .reducer
        .apply(accepted_envelope.clone())
        .unwrap_or_else(|_| unreachable!());
    assert!(!first.duplicate);
    assert!(duplicate.duplicate);
    assert!(duplicate.effects.is_empty());
    let late = CommandEnvelope::new(
        IdempotencyKey::new("late"),
        accepted_envelope.expected_revision,
        TraceId::new("late"),
        accepted_envelope.command,
    );
    assert_eq!(
        driver.reducer.apply(late).err().map(|error| error.reason),
        Some(task_engine::RefusalReason::RevisionConflict)
    );
}

#[test]
fn every_commit_prefix_replays_and_old_monotonic_metadata_never_proves_freshness() {
    let mut driver = Driver::new();
    driver.apply(Command::StartTask(preview()));
    driver.apply(Command::AcceptInitialConsent(ApprovalReceiptReference(
        "consent-1".to_owned(),
    )));
    let mut journals = vec![driver.reducer.journal().clone()];
    for _ in 0..4 {
        driver.apply_next();
        journals.push(driver.reducer.journal().clone());
    }
    let action_id = driver
        .reducer
        .actions()
        .next()
        .map_or_else(|| unreachable!(), |action| action.action_id().clone());
    driver.apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: task_engine::CapabilityId::new("capability-1"),
        })),
        dispatch_id: Some(DispatchId::new("dispatch-1")),
    });
    let dispatching_journal = driver.reducer.journal().clone();
    journals.push(dispatching_journal.clone());
    complete_observation(&mut driver, action_id, ObservationCompleteness::Complete);
    journals.push(driver.reducer.journal().clone());
    while !driver.reducer.task().state().is_terminal() {
        driver.apply_next();
        journals.push(driver.reducer.journal().clone());
    }

    for journal in journals {
        let expected_state = journal
            .last_recorded_state()
            .unwrap_or_else(|| unreachable!());
        let expected_revision = journal.revision();
        let (replayed, _) = Reducer::replay(
            seed(),
            defaults(),
            ManualClock::at(9_000),
            SequentialIds::new(),
            &journal,
        )
        .unwrap_or_else(|error| unreachable!("replay failed: {error:?}"));
        assert_eq!(replayed.task().state(), expected_state);
        assert_eq!(replayed.task().revision(), expected_revision);
    }

    let (replayed, recovery) = Reducer::replay(
        seed(),
        defaults(),
        ManualClock::at(9_000),
        SequentialIds::new(),
        &dispatching_journal,
    )
    .unwrap_or_else(|_| unreachable!());
    assert_eq!(recovery.unknown_outcome_actions.len(), 1);
    let retry = replayed
        .next_reviewed_command(&Digest)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("pure read has one reviewed retry"));
    let Command::ProposeAction(proposal) = retry else {
        unreachable!("recovery proposes a new exact read")
    };
    assert_eq!(
        proposal.idempotency(),
        task_engine::IdempotencyClass::PureRead
    );
}

fn outcome_envelope(
    driver: &Driver,
    key: &str,
    action_id: bip_types::identity::ActionId,
    dispatch_id: &str,
    observed_at: u64,
) -> CommandEnvelope {
    CommandEnvelope::new(
        IdempotencyKey::new(key),
        driver.reducer.task().revision(),
        TraceId::new(key),
        Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(ActionOutcome {
                code: ActionResultCode::Verified,
                dispatch_id: Some(DispatchId::new(dispatch_id)),
                // This process-local reading is retained as history only.
                // Freshness is the exact generation/document/dispatch tuple.
                observed_at: MonotonicMillis(observed_at),
                observation: Some(evidence(ObservationCompleteness::Complete)),
                discovered_source: None,
            }),
        },
    )
}
