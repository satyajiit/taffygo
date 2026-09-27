// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Handing the page to a person, and taking it back afterwards.
//!
//! The eAdhaar case is the one this file is written from. A task drives a
//! government form, reaches a test meant to prove a person is present or a
//! code that arrived on another device, and cannot go further — not because it
//! failed to recognise anything, but because everything that could have got
//! past the challenge is prohibited by action class and was refused before
//! anything looked at it. What remains is to stop and say so.
//!
//! Four properties are checked here, and each of them is a way the feature
//! could look finished and be wrong:
//!
//! 1. **The whole sequence.** Running, handover, waiting, resumed, running.
//! 2. **Revocation precedes the wait.** The assistant must not hold mutation
//!    authority over a tab a person is typing into, and "before" is a property
//!    of the effect list rather than of a comment.
//! 3. **A new lease on resume.** Reusing the lease the handover revoked would
//!    leave an audit unable to say which page changes were the assistant's and
//!    which were the person's.
//! 4. **Expiry holds rather than resumes.** Nobody came back, and the
//!    assistant has no standing to take the page back on a timer.
//!
//! There is a fifth property this file states by having nothing to say about
//! it: no type used here names a challenge, a one-time code or a password.
//! `a_handover_learns_nothing_about_the_page` is that absence written down.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use common::agent::{with_recorded_turn, Digest};
use task_engine::agent::{ModelStopReason, ModelToolCall};
use task_engine::authority::{ActorLeaseId, RevocationReason};
use task_engine::command::{Command, CommandKind};
use task_engine::effect::{revocation_precedes_remote_wait, Effect};
use task_engine::event::EventKind;
use task_engine::handover::{
    handover_id_for_call, HandoverCompletion, HandoverId, PersonInput, HANDOVER_WINDOW_MS,
};
use task_engine::task::{DisplayState, StateReason, TaskState};
use task_engine::transition::RefusalReason;

/// The handover the fixtures open. The same identity the agent loop derives
/// for the first call of the first turn, so this file and the loop agree.
fn handover() -> HandoverId {
    handover_id_for_call(0, 0)
}

fn lease(value: &str) -> ActorLeaseId {
    ActorLeaseId::new(value)
}

/// One completion: a different lease from the one the handover revoked, and
/// enough observed input that the browser saw the person act.
fn came_back() -> HandoverCompletion {
    HandoverCompletion::new(
        handover(),
        lease("lease-before-handover"),
        lease("lease-after-handover"),
        PersonInput::observed(2),
    )
    .expect("both probe lease identities are non-empty")
}

/// A running task that has just handed the page over.
fn handed_over() -> common::Fixture {
    let mut fixture = common::running();
    fixture.must_apply(Command::RequestHandover {
        handover_id: handover(),
    });
    fixture
}

#[test]
fn the_whole_sequence_runs_stops_waits_and_resumes() {
    let mut fixture = common::running();
    assert_eq!(fixture.state(), TaskState::Running);

    let committed = fixture
        .apply(Command::RequestHandover {
            handover_id: handover(),
        })
        .expect("a running task may hand the page over");
    assert_eq!(committed.to, TaskState::WaitingUser);
    assert_eq!(
        transition_reason(&committed),
        Some(StateReason::HandoverRequested)
    );

    // What a person sees is one thing in both halves of the wait, which is the
    // point: "Waiting for you" is the same sentence whether the task needs a
    // value or the page.
    assert_eq!(
        fixture.reducer.task().display_state(),
        Some(DisplayState::WaitingForYou)
    );
    assert_eq!(fixture.reducer.pending_handover(), Some(&handover()));

    let resumed = fixture
        .apply(Command::CompleteHandover(came_back()))
        .expect("the person came back");
    assert_eq!(resumed.to, TaskState::Running);
    assert_eq!(
        transition_reason(&resumed),
        Some(StateReason::HandoverCompleted)
    );
    assert_eq!(fixture.reducer.pending_handover(), None);
}

/// The one ordering that matters, checked as an ordering rather than asserted.
#[test]
fn authority_is_revoked_before_anything_waits_on_the_person() {
    let mut fixture = common::running();
    let committed = fixture
        .apply(Command::RequestHandover {
            handover_id: handover(),
        })
        .expect("a running task may hand the page over");

    assert_eq!(
        committed.effects.first(),
        Some(&Effect::RevokeAuthority {
            reason: RevocationReason::UserTookOver
        }),
        "the tab is the person's before anything waits on them"
    );
    assert_eq!(
        committed.effects.get(1),
        Some(&Effect::AwaitHandover {
            handover_id: handover(),
            window_ms: HANDOVER_WINDOW_MS,
        })
    );
    assert_eq!(committed.effects.len(), 2);
    assert!(revocation_precedes_remote_wait(&committed.effects));
}

#[test]
fn the_journal_names_the_handover_it_opened() {
    let mut fixture = common::running();
    let committed = fixture
        .apply(Command::RequestHandover {
            handover_id: handover(),
        })
        .expect("a running task may hand the page over");

    let requested = committed
        .events
        .iter()
        .find(|event| event.kind == EventKind::HandoverRequested)
        .expect("the handover is journalled");
    assert_eq!(
        requested
            .subject
            .as_ref()
            .map(task_engine::EventSubject::identifier),
        Some(handover().as_str().to_owned())
    );
}

/// Point three, and the reason it is a guard rather than a convention.
#[test]
fn resuming_under_the_revoked_lease_is_refused() {
    let mut fixture = handed_over();
    let reused = HandoverCompletion::new(
        handover(),
        lease("lease-before-handover"),
        lease("lease-before-handover"),
        PersonInput::observed(2),
    )
    .expect("a reused lease is a well-formed value");

    let refusal = fixture
        .apply(Command::CompleteHandover(reused))
        .expect_err("the assistant may not resume under the authority it gave up");

    assert_eq!(refusal.reason, RefusalReason::HandoverLeaseReused);
    assert_eq!(refusal.command, CommandKind::CompleteHandover);
    // Refused, so the task is still the person's.
    assert_eq!(fixture.state(), TaskState::WaitingUser);
    assert_eq!(fixture.reducer.pending_handover(), Some(&handover()));
}

#[test]
fn the_resumption_lease_is_what_the_audit_record_names() {
    let mut fixture = handed_over();
    let resumed = fixture
        .apply(Command::CompleteHandover(came_back()))
        .expect("the person came back");

    let completed = resumed
        .events
        .iter()
        .find(|event| event.kind == EventKind::HandoverCompleted)
        .expect("the resumption is journalled");
    assert_eq!(
        completed
            .subject
            .as_ref()
            .map(task_engine::EventSubject::identifier),
        Some("lease-after-handover".to_owned()),
        "everything under this identity is the assistant's again, and \
         everything under the previous one was before the person had the tab"
    );
    assert_eq!(
        completed
            .subject
            .as_ref()
            .map(task_engine::EventSubject::kind_label),
        Some("actor_lease")
    );
}

#[test]
fn a_completion_for_another_handover_is_refused() {
    let mut fixture = handed_over();
    let other = HandoverCompletion::new(
        handover_id_for_call(9, 9),
        lease("lease-before-handover"),
        lease("lease-after-handover"),
        PersonInput::observed(1),
    )
    .expect("both probe lease identities are non-empty");

    let refusal = fixture
        .apply(Command::CompleteHandover(other))
        .expect_err("a completion may not answer a handover that is not open");
    assert_eq!(refusal.reason, RefusalReason::HandoverMismatch);
}

/// A generic completion may not consume an exact wait.
///
/// Both put the task in `WAITING_USER`, and only one of them carries the lease
/// the assistant resumes under and the evidence that a person acted. Letting
/// the generic one through would resume with neither.
#[test]
fn supplying_input_cannot_end_a_handover() {
    let mut fixture = handed_over();
    let refusal = fixture
        .apply(Command::SupplyUserInput)
        .expect_err("a value is not a returned page");
    assert_eq!(refusal.reason, RefusalReason::NotWaitingForUserInput);
    assert_eq!(fixture.state(), TaskState::WaitingUser);
}

#[test]
fn expiry_holds_the_task_and_leaves_the_page_with_the_person() {
    let mut fixture = handed_over();
    let expired = fixture
        .apply(Command::ExpireHandover {
            handover_id: handover(),
        })
        .expect("a window may close with nobody coming back");

    assert_eq!(expired.to, TaskState::Pausing);
    assert_eq!(
        transition_reason(&expired),
        Some(StateReason::HandoverExpired)
    );
    assert_eq!(fixture.reducer.pending_handover(), None);

    // The reason stays "the user took over", because they did and they still
    // have it. Nothing took the page back from them when the timer ran out.
    assert_eq!(
        expired.effects.first(),
        Some(&Effect::RevokeAuthority {
            reason: RevocationReason::UserTookOver
        })
    );
    assert!(revocation_precedes_remote_wait(&expired.effects));

    let held = fixture
        .apply(Command::PauseSettled)
        .expect("settling a pause with nothing in flight");
    assert_eq!(held.to, TaskState::Paused);
    // Resuming re-enters the queue, where the browser, the sources and the
    // route are revalidated — which is exactly right after a stretch of time
    // in which a person was doing things in the tab.
    let queued = fixture
        .apply(Command::ResumeTask)
        .expect("a held task resumes");
    assert_eq!(queued.to, TaskState::Queued);
}

#[test]
fn expiring_a_handover_that_is_not_open_is_refused() {
    let mut fixture = handed_over();
    let refusal = fixture
        .apply(Command::ExpireHandover {
            handover_id: handover_id_for_call(9, 9),
        })
        .expect_err("an expiry may not close a window that is not the open one");
    assert_eq!(refusal.reason, RefusalReason::HandoverMismatch);
    assert_eq!(fixture.state(), TaskState::WaitingUser);
}

/// The evidence is recorded and decides nothing.
///
/// A person may have answered on another device, or found that nothing was
/// needed. A zero count is recorded and the handover still completes, because
/// deciding from the count would be the detector this product refuses to
/// build.
#[test]
fn a_handover_with_no_observed_input_still_completes() {
    let mut fixture = handed_over();
    let untouched = HandoverCompletion::new(
        handover(),
        lease("lease-before-handover"),
        lease("lease-after-handover"),
        PersonInput::NONE,
    )
    .expect("both probe lease identities are non-empty");
    assert!(!untouched.person_input().any());

    let resumed = fixture
        .apply(Command::CompleteHandover(untouched))
        .expect("nothing decides from the count");
    assert_eq!(resumed.to, TaskState::Running);
}

/// The absence, written down.
///
/// Nothing the handover carries says anything about the page, and that is not
/// an oversight to be filled in later. `BypassAccessControl` and
/// `ExtractCredential` are prohibited by class, so a challenge, a one-time code
/// and a password are refused before anything looks at them, and the handover
/// is what remains. Refusal by exhaustion has no false negatives; a detector
/// does, and a detector is what a field naming the challenge would become.
#[test]
fn a_handover_learns_nothing_about_the_page() {
    let mut fixture = common::running();
    let committed = fixture
        .apply(Command::RequestHandover {
            handover_id: handover(),
        })
        .expect("a running task may hand the page over");

    // The effect the browser receives, rendered. If a field describing the
    // page ever appears, it appears here.
    let rendered = format!("{:?}", committed.effects);
    for absent in [
        "captcha",
        "Captcha",
        "challenge",
        "Challenge",
        "otp",
        "Otp",
        "password",
        "Password",
        "code",
        "Code",
        "secret",
        "Secret",
    ] {
        assert!(
            !rendered.contains(absent),
            "the handover effect mentions {absent}: {rendered}"
        );
    }

    // And the completion carries two lease identities and a number. Nothing
    // in it could hold what the person typed.
    let completion = format!("{:?}", came_back());
    assert!(completion.contains("lease-after-handover"));
    for absent in ["captcha", "challenge", "otp", "password", "secret"] {
        assert!(!completion.to_lowercase().contains(absent), "{absent}");
    }
}

/// The reducer keeps hold of nothing once the page comes back to it.
#[test]
fn taking_over_during_a_handover_clears_it() {
    let mut fixture = handed_over();
    let taken = fixture
        .apply(Command::TakeOver)
        .expect("a waiting task may be taken over");
    assert_eq!(taken.to, TaskState::Pausing);
    assert_eq!(
        fixture.reducer.pending_handover(),
        None,
        "a handover nobody is waiting on is a completion nothing could answer"
    );
}

/// The link between the model's call and the reducer's command.
///
/// `user.handover` and `user.ask` share `ToolDispatch::Person` and both end in
/// `WAITING_USER`, so the loop could plausibly answer either with the same
/// command. It does not, and the difference is authority: asking for a value
/// leaves the task's lease standing, and handing the page back revokes it.
#[test]
fn the_loop_turns_the_handover_row_into_a_handover_and_not_an_ask() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "user.handover",
            vec![task_engine::SuppliedArgument::new(
                "reason",
                task_engine::ArgumentValue::Choice("refused_by_class".to_owned()),
            )],
        )],
    );

    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("the reply is readable");

    // The identity is derived from the turn and the call, so a replay re-opens
    // the same handover rather than a second one.
    assert_eq!(
        next,
        Some(Command::RequestHandover {
            handover_id: handover_id_for_call(0, 0),
        }),
        "got {next:?}"
    );
}

/// The other half of the same contrast, so a change that collapses the two
/// rows has to walk past both.
#[test]
fn the_loop_still_turns_the_ask_row_into_a_request_for_input() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "user.ask",
            vec![task_engine::SuppliedArgument::new(
                "subject",
                task_engine::ArgumentValue::Text("the reference number".to_owned()),
            )],
        )],
    );

    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("the reply is readable");
    assert_eq!(next, Some(Command::RequestUserInput), "got {next:?}");
}

/// Why the task moved, read off the transition event the fold journalled.
///
/// The reason is not a field of the accepted result; it is a fact the journal
/// records, and reading it from there is what makes these assertions about
/// what a replay would see rather than about what the caller was handed.
fn transition_reason(accepted: &task_engine::Accepted) -> Option<StateReason> {
    accepted
        .events
        .iter()
        .find(|event| event.is_transition())
        .and_then(|event| event.reason)
}
