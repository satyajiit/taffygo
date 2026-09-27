// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Steps seven to ten of the section 12 sequence: the outcomes of effects.
//!
//! Journalling the intent, dispatching, observing the postconditions, and
//! recording exactly one terminal result. `StaleNodeSequence` is pure, so
//! these are exercised as a table too — including the case that matters most:
//! an outcome offered for a step the sequence is not waiting on never applies.
//!
//! Steps one to six are decisions over values and live in
//! `stale_node_decisions.rs`.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::VerifierKind;
use bip_types::result_code::SideEffectCertainty;
use bip_types::ActionResultCode;

use policy_engine::report::{
    summarize_postconditions, ConsumptionOutcome, DispatchAck, JournalOutcome, PostconditionReport,
};
use policy_engine::sequence::{SequenceInput, SequenceProgress, SequenceState, StaleNodeSequence};
use policy_engine::{DispatchVerdict, StaleNodeStep};

// ---------------------------------------------------------------------------
// Steps 7 to 10 — the effect steps, through the pure machine.
// ---------------------------------------------------------------------------

/// A sequence that has passed steps one to six.
fn passed_checks() -> StaleNodeSequence {
    let mut sequence = StaleNodeSequence::new();
    let progress = sequence.step(SequenceInput::Preconditions(DispatchVerdict::Proceed));
    assert_eq!(
        progress,
        SequenceProgress::Advanced {
            step: StaleNodeStep::ReevaluateNode,
            next: StaleNodeStep::JournalIntent,
        }
    );
    assert!(!sequence.may_have_reached_the_page());
    sequence
}

/// Drives a sequence to its end and returns the recorded code.
fn drive(sequence: &mut StaleNodeSequence, inputs: &[SequenceInput]) -> ActionResultCode {
    for input in inputs {
        sequence.step(*input);
    }
    sequence
        .result()
        .expect("the inputs must drive the sequence to its end")
}

#[test]
fn step_seven_a_recorded_intent_advances_to_the_dispatch_step() {
    let mut sequence = passed_checks();
    let progress = sequence.step(SequenceInput::Journal(JournalOutcome::Recorded));
    assert_eq!(
        progress,
        SequenceProgress::Advanced {
            step: StaleNodeStep::JournalIntent,
            next: StaleNodeStep::Dispatch,
        }
    );
    assert!(sequence.may_have_reached_the_page());
    assert!(sequence.journalled_intent());
}

#[test]
fn step_seven_a_failed_journal_write_sends_nothing_and_ends_with_internal_error() {
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::WriteFailed),
            SequenceInput::Record(ConsumptionOutcome::Spent),
        ],
    );
    assert_eq!(code, ActionResultCode::InternalError);
    assert_eq!(sequence.refusal_step(), Some(StaleNodeStep::JournalIntent));
    assert!(!sequence.journalled_intent());
}

#[test]
fn step_seven_an_intent_never_attempted_ends_as_a_cancellation_that_touched_nothing() {
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::NotAttempted),
            SequenceInput::Record(ConsumptionOutcome::Spent),
        ],
    );
    assert_eq!(code, ActionResultCode::CancelledByUser);
    assert_eq!(code.side_effect(), SideEffectCertainty::NotPerformed);
    assert!(!sequence.may_have_reached_the_page());
}

#[test]
fn step_eight_an_accepted_command_advances_to_the_postcondition_step() {
    let mut sequence = passed_checks();
    sequence.step(SequenceInput::Journal(JournalOutcome::Recorded));
    let progress = sequence.step(SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor));
    assert_eq!(
        progress,
        SequenceProgress::Advanced {
            step: StaleNodeStep::Dispatch,
            next: StaleNodeStep::ObservePostconditions,
        }
    );
}

#[test]
fn step_eight_every_refusal_carries_the_code_the_taxonomy_gives_it() {
    let cases = [
        (
            DispatchAck::RejectedByExecutor,
            ActionResultCode::DispatchFailed,
        ),
        (DispatchAck::RendererGone, ActionResultCode::RendererCrashed),
        (
            DispatchAck::TargetGoneAtDispatch,
            ActionResultCode::NodeGone,
        ),
        (
            DispatchAck::CancelledByUserBeforeSend,
            ActionResultCode::CancelledByUser,
        ),
        (
            DispatchAck::NavigationCommittedBeforeSend,
            ActionResultCode::CancelledByNavigation,
        ),
    ];
    for (ack, expected) in cases {
        let mut sequence = passed_checks();
        let code = drive(
            &mut sequence,
            &[
                SequenceInput::Journal(JournalOutcome::Recorded),
                SequenceInput::Dispatch(ack),
                SequenceInput::Record(ConsumptionOutcome::Spent),
            ],
        );
        assert_eq!(code, expected, "{}", ack.label());
        assert_eq!(sequence.refusal_step(), Some(StaleNodeStep::Dispatch));
    }

    // Every acknowledgement is covered, so a new one cannot be added without a
    // decision about what it means.
    assert_eq!(DispatchAck::ALL.len(), cases.len() + 1);
}

#[test]
fn step_nine_browser_owned_corroboration_is_the_only_route_to_verified() {
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::Recorded),
            SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            SequenceInput::Postcondition(PostconditionReport::Satisfied),
            SequenceInput::Record(ConsumptionOutcome::Spent),
        ],
    );
    assert_eq!(code, ActionResultCode::Verified);
    assert_eq!(code.side_effect(), SideEffectCertainty::Performed);
    assert_eq!(sequence.refusal_step(), None);
}

#[test]
fn step_nine_a_renderer_acknowledgement_alone_keeps_the_sequence_waiting() {
    let mut sequence = passed_checks();
    sequence.step(SequenceInput::Journal(JournalOutcome::Recorded));
    sequence.step(SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor));
    let progress = sequence.step(SequenceInput::Postcondition(
        PostconditionReport::AcknowledgedOnly,
    ));
    assert_eq!(
        progress,
        SequenceProgress::Waiting {
            step: StaleNodeStep::ObservePostconditions,
        }
    );
    assert_eq!(sequence.result(), None);
    assert_eq!(sequence.state(), SequenceState::AwaitingPostcondition);
}

#[test]
fn step_nine_every_other_report_ends_the_action_without_claiming_success() {
    let cases = [
        (
            PostconditionReport::Contradicted,
            ActionResultCode::PostconditionFailed,
        ),
        (
            PostconditionReport::DeadlineExpired,
            ActionResultCode::PostconditionTimeout,
        ),
        (
            PostconditionReport::NavigationCommitted,
            ActionResultCode::CancelledByNavigation,
        ),
        (
            PostconditionReport::CancelledByUserAfterSend,
            ActionResultCode::OutcomeUnknown,
        ),
        (
            PostconditionReport::RendererCrashed,
            ActionResultCode::RendererCrashed,
        ),
        (
            PostconditionReport::Unreconciled,
            ActionResultCode::OutcomeUnknown,
        ),
    ];
    for (report, expected) in cases {
        let mut sequence = passed_checks();
        let code = drive(
            &mut sequence,
            &[
                SequenceInput::Journal(JournalOutcome::Recorded),
                SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
                SequenceInput::Postcondition(report),
                SequenceInput::Record(ConsumptionOutcome::Spent),
            ],
        );
        assert_eq!(code, expected, "{}", report.label());
        assert!(code.fails_closed());
        assert_ne!(code.side_effect(), SideEffectCertainty::NotPerformed);
    }

    // Satisfied and acknowledged-only are the two that do not end here.
    assert_eq!(PostconditionReport::ALL.len(), cases.len() + 2);
}

#[test]
fn step_nine_a_cancellation_after_the_command_was_sent_is_never_reported_as_a_cancellation() {
    // Reporting it as a cancellation would say the page was untouched, which
    // would license a retry an unconfirmed side effect forbids.
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::Recorded),
            SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            SequenceInput::Postcondition(PostconditionReport::CancelledByUserAfterSend),
            SequenceInput::Record(ConsumptionOutcome::Spent),
        ],
    );
    assert_eq!(code, ActionResultCode::OutcomeUnknown);
    assert_eq!(code.side_effect(), SideEffectCertainty::Possible);
}

#[test]
fn step_ten_spending_the_authority_records_the_result_the_verifier_decided() {
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::Recorded),
            SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            SequenceInput::Postcondition(PostconditionReport::Satisfied),
            SequenceInput::Record(ConsumptionOutcome::Spent),
        ],
    );
    assert_eq!(code, ActionResultCode::Verified);
    assert_eq!(
        sequence.furthest_step(),
        StaleNodeStep::RecordTerminalResult
    );
}

#[test]
fn step_ten_a_refusal_before_any_authority_was_in_flight_keeps_its_own_code() {
    let mut sequence = StaleNodeSequence::new();
    sequence.step(SequenceInput::Preconditions(DispatchVerdict::refuse(
        StaleNodeStep::ConfirmOrigin,
        ActionResultCode::OriginChanged,
    )));
    let code = drive(
        &mut sequence,
        &[SequenceInput::Record(ConsumptionOutcome::NothingToSpend)],
    );
    assert_eq!(code, ActionResultCode::OriginChanged);
}

#[test]
fn step_ten_nothing_to_spend_after_a_journalled_intent_is_an_internal_fault() {
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::Recorded),
            SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            SequenceInput::Postcondition(PostconditionReport::Satisfied),
            SequenceInput::Record(ConsumptionOutcome::NothingToSpend),
        ],
    );
    assert_eq!(code, ActionResultCode::InternalError);
    assert_eq!(
        sequence.refusal_step(),
        Some(StaleNodeStep::RecordTerminalResult)
    );
}

#[test]
fn step_ten_authority_already_spent_is_an_internal_fault_even_after_a_verified_effect() {
    // The broker's own ledger disagreed with itself. A success claim on top of
    // that would be a claim nobody can support.
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::Recorded),
            SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            SequenceInput::Postcondition(PostconditionReport::Satisfied),
            SequenceInput::Record(ConsumptionOutcome::AlreadySpent),
        ],
    );
    assert_eq!(code, ActionResultCode::InternalError);
    assert!(code.fails_closed());
}

#[test]
fn step_ten_a_sequence_that_has_ended_is_not_reopened_by_a_late_outcome() {
    let mut sequence = passed_checks();
    let code = drive(
        &mut sequence,
        &[
            SequenceInput::Journal(JournalOutcome::Recorded),
            SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            SequenceInput::Postcondition(PostconditionReport::Satisfied),
            SequenceInput::Record(ConsumptionOutcome::Spent),
        ],
    );
    assert_eq!(code, ActionResultCode::Verified);
    let late = sequence.step(SequenceInput::Postcondition(
        PostconditionReport::Contradicted,
    ));
    assert_eq!(
        late,
        SequenceProgress::Ended {
            step: StaleNodeStep::RecordTerminalResult,
            code: ActionResultCode::Verified,
        }
    );
    assert_eq!(sequence.result(), Some(ActionResultCode::Verified));
}

// ---------------------------------------------------------------------------
// Ordering — an outcome for the wrong step never applies.
// ---------------------------------------------------------------------------

/// One outcome offered to a sequence that is waiting on a different step.
struct OutOfTurn {
    /// Builds the sequence in the state under test.
    build: fn() -> StaleNodeSequence,
    /// The outcome offered out of turn.
    input: SequenceInput,
    /// The step the sequence was actually waiting on.
    awaited: StaleNodeStep,
}

#[test]
fn an_outcome_for_a_step_the_sequence_is_not_on_ends_it_with_internal_error() {
    let out_of_turn = [
        OutOfTurn {
            build: StaleNodeSequence::new,
            input: SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
            awaited: StaleNodeStep::ResolveTabAndFrame,
        },
        OutOfTurn {
            build: passed_checks,
            input: SequenceInput::Postcondition(PostconditionReport::Satisfied),
            awaited: StaleNodeStep::JournalIntent,
        },
        OutOfTurn {
            build: journalled,
            input: SequenceInput::Record(ConsumptionOutcome::Spent),
            awaited: StaleNodeStep::Dispatch,
        },
        OutOfTurn {
            build: dispatched,
            input: SequenceInput::Journal(JournalOutcome::Recorded),
            awaited: StaleNodeStep::ObservePostconditions,
        },
    ];
    for OutOfTurn {
        build,
        input,
        awaited,
    } in out_of_turn
    {
        let mut sequence = build();
        let progress = sequence.step(input);
        assert_eq!(
            progress,
            SequenceProgress::Ended {
                step: awaited,
                code: ActionResultCode::InternalError,
            }
        );
        assert_eq!(sequence.result(), Some(ActionResultCode::InternalError));
    }
}

/// A sequence whose intent is journalled.
fn journalled() -> StaleNodeSequence {
    let mut sequence = passed_checks();
    sequence.step(SequenceInput::Journal(JournalOutcome::Recorded));
    sequence
}

/// A sequence whose command the executor accepted.
fn dispatched() -> StaleNodeSequence {
    let mut sequence = journalled();
    sequence.step(SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor));
    sequence
}

// ---------------------------------------------------------------------------
// The verifier summary that feeds step 9.
// ---------------------------------------------------------------------------

#[test]
fn step_nine_a_verifier_that_reported_nothing_cannot_be_reconciled() {
    assert_eq!(
        summarize_postconditions(&[]),
        PostconditionReport::Unreconciled
    );
}

#[test]
fn step_nine_every_verifier_kind_is_classified_and_only_browser_owned_ones_verify() {
    let browser_owned: Vec<&str> = VerifierKind::ALL
        .iter()
        .filter(|verifier| policy_engine::report::is_browser_owned(**verifier))
        .map(|verifier| verifier.wire())
        .collect();
    assert_eq!(
        browser_owned,
        vec![
            "BROWSER_NAVIGATION_EVENT",
            "BROWSER_TAB_EVENT",
            "FRESH_SNAPSHOT",
            "DELTA_OBSERVATION",
            "BROWSER_NETWORK_EVENT",
        ]
    );
}

// ---------------------------------------------------------------------------
// The sequence as a whole.
// ---------------------------------------------------------------------------

#[test]
fn every_step_of_the_specification_is_reachable_and_named() {
    let mut reached: Vec<StaleNodeStep> = Vec::new();
    let mut sequence = StaleNodeSequence::new();
    for input in [
        SequenceInput::Preconditions(DispatchVerdict::Proceed),
        SequenceInput::Journal(JournalOutcome::Recorded),
        SequenceInput::Dispatch(DispatchAck::AcceptedByExecutor),
        SequenceInput::Postcondition(PostconditionReport::Satisfied),
        SequenceInput::Record(ConsumptionOutcome::Spent),
    ] {
        sequence.step(input);
        reached.push(sequence.furthest_step());
    }
    assert_eq!(
        reached,
        vec![
            StaleNodeStep::ReevaluateNode,
            StaleNodeStep::JournalIntent,
            StaleNodeStep::Dispatch,
            StaleNodeStep::ObservePostconditions,
            StaleNodeStep::RecordTerminalResult,
        ]
    );
    // Steps one to five are reached inside `evaluate_dispatch`, which reports
    // them on a refusal; the tests above walk each of them.
    assert_eq!(StaleNodeStep::ALL.len(), 10);
}

#[test]
fn a_fresh_sequence_has_decided_nothing() {
    let sequence = StaleNodeSequence::new();
    assert_eq!(sequence.result(), None);
    assert_eq!(sequence.refusal_step(), None);
    assert!(!sequence.may_have_reached_the_page());
    assert_eq!(sequence.state(), SequenceState::AwaitingPreconditions);
    assert_eq!(
        sequence.state().awaited_step(),
        StaleNodeStep::ResolveTabAndFrame
    );
}
