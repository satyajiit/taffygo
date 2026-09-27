// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the prepared-effect ledger refuses (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`).
//!
//! The ledger half of the record's validation section: mutate the staged
//! effect, the place, the class, the owner, the prepare time and the gesture
//! between the two steps and assert refusal in each case; walk the lifecycle
//! exhaustively; expire a preparation; and kill the process between prepare and
//! commit. The broker and milestone half is
//! `tests/prepared_effect_authorization.rs`.
//!
//! # The one that matters most
//!
//! [`the_binding_is_the_rendering_and_never_the_proposal`] is the point of the
//! whole mechanism. What a person approved is what they *saw*, so the value the
//! ledger binds is a digest over the effect as the trusted surface rendered it.
//! A digest over the proposal would let the rendering differ from the thing
//! approved while the digest still matched — the gap the record exists to
//! close, and the easiest part of it to get subtly wrong, because both digests
//! are the same type on the wire and either one "works" until somebody changes
//! the rendering.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{MonotonicMillis, TabId, TaskId};
use bip_types::ActionResultCode;

use policy_engine::{
    ActionClass, ActorLeaseId, CommitRefusal, PrepareError, PreparedEffectLedger,
    PreparedEffectState, RenderedEffect, RenderedEffectDigest,
};

use common::{
    binding, commit_verdict, consumed_ledger, digest, prepare_request, prepared_effect_id,
    proposal_digest, rendered, rendered_digest, Commit, COMMIT_AT, EXPIRES_AT, RENDERED_AT,
    STAGED_AT,
};

// ---------------------------------------------------------------------------
// The digest is over the effect as rendered
// ---------------------------------------------------------------------------

/// The whole point of the mechanism.
///
/// The ledger binds a digest over what the trusted surface *displayed*.
/// Offering the prepare's own proposal digest as the rendering is refused by
/// name, so a caller that reaches for the value it already has in hand cannot
/// bind the request a model made in place of the sentence a person read.
#[test]
fn the_binding_is_the_rendering_and_never_the_proposal() {
    let mut ledger = PreparedEffectLedger::new();
    ledger
        .stage(&prepare_request(), MonotonicMillis(STAGED_AT))
        .expect("staging succeeds");

    // The substitution this record exists to refuse: binding the proposal.
    let as_proposal = RenderedEffect {
        digest: RenderedEffectDigest::from_trusted_surface(proposal_digest()),
        rendered_at: MonotonicMillis(RENDERED_AT),
    };
    assert_eq!(
        ledger.record_rendered(
            &prepared_effect_id(),
            &as_proposal,
            MonotonicMillis(RENDERED_AT)
        ),
        Err(PrepareError::RenderedDigestIsTheProposal)
    );
    // It was refused rather than half-applied: nothing was rendered, so there
    // is still nothing a commit could be made against.
    assert_eq!(
        ledger
            .get(&prepared_effect_id())
            .map(|entry| entry.state_at(MonotonicMillis(RENDERED_AT))),
        Some(PreparedEffectState::Staged)
    );

    // The rendering the surface actually computed is accepted, and it is that
    // value — not the proposal's — a commit is compared against.
    ledger
        .record_rendered(
            &prepared_effect_id(),
            &rendered(),
            MonotonicMillis(RENDERED_AT),
        )
        .expect("the surface's own digest is what binds");

    let commit = Commit::default();

    // A commit presenting the rendered digest stands.
    assert_eq!(
        ledger.verdict(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT)),
        Ok(())
    );
    // A commit presenting the *proposal* digest is refused, even though it is
    // the digest of the very proposal that was prepared. It is not what the
    // person read.
    let mut as_proposal = binding();
    as_proposal.prepared_effect_digest = proposal_digest();
    assert_eq!(
        ledger
            .verdict(&as_proposal, &commit.context(), MonotonicMillis(COMMIT_AT))
            .map_err(CommitRefusal::result_code),
        Err(ActionResultCode::PreparedEffectChanged)
    );
}

/// A rendering that changed after it was shown is a different effect.
///
/// This is the failure the digest is for: the staged effect moved between the
/// sheet and the commit, everything else matches, and the commit is refused.
#[test]
fn a_staged_effect_that_moved_after_it_was_shown_is_refused() {
    assert_eq!(
        commit_verdict(|commit| {
            commit.binding.prepared_effect_digest = digest("something-else-entirely");
        }),
        Err(ActionResultCode::PreparedEffectChanged)
    );
}

// ---------------------------------------------------------------------------
// A commit spends exactly what was prepared and never more
// ---------------------------------------------------------------------------

/// Every field the commit is held to, moved one at a time.
///
/// Each of these is "spending more than was prepared" in a different direction:
/// a different place, a different class of effect, a different task, a
/// different lease, or a prepare time the ledger never recorded.
#[test]
fn a_commit_may_not_move_the_place_the_class_the_owner_or_the_time() {
    // Somewhere other than the place that was prepared.
    assert_eq!(
        commit_verdict(|commit| commit.scope.tab_id = TabId::new("tab_other")),
        Err(ActionResultCode::PreparedEffectChanged)
    );

    // A different class of effect under the same preparation.
    assert_eq!(
        commit_verdict(|commit| commit.action_class = ActionClass::Purchase),
        Err(ActionResultCode::PreparedEffectChanged)
    );

    // A prepare time the ledger did not record. The binding names when the
    // prepare completed, and a commit that misremembers it is not answering
    // the sheet the ledger holds.
    assert_eq!(
        commit_verdict(|commit| commit.binding.prepared_at_monotonic_ms = RENDERED_AT + 1),
        Err(ActionResultCode::PreparedEffectChanged)
    );

    // Another task's preparation.
    assert_eq!(
        commit_verdict(|commit| commit.task_id = TaskId::new("task_other")),
        Err(ActionResultCode::CommitWithoutPrepare)
    );

    // Another lease's preparation — which is what a take-over between the two
    // steps looks like from the commit's side.
    assert_eq!(
        commit_verdict(|commit| commit.lease_id = ActorLeaseId::new("lease_other")),
        Err(ActionResultCode::CommitWithoutPrepare)
    );
}

/// A commit cannot invent a prepare that did not run.
#[test]
fn a_commit_naming_no_prepare_that_exists_is_refused() {
    let ledger = PreparedEffectLedger::new();
    let commit = Commit::default();
    assert_eq!(
        ledger
            .verdict(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
            .map_err(CommitRefusal::result_code),
        Err(ActionResultCode::CommitWithoutPrepare)
    );
}

/// A commit is refused in every state but consumed, walked exhaustively.
///
/// Enumerated over [`PreparedEffectState::ALL`] rather than exampled, so a
/// state added to the lifecycle has to decide what a commit against it means
/// instead of inheriting whichever arm happened to be nearest.
#[test]
fn a_commit_is_refused_in_every_state_but_consumed() {
    let commit = Commit::default();

    for state in PreparedEffectState::ALL {
        // Build a ledger whose single record sits in `state` at COMMIT_AT.
        let (ledger, now) = match state {
            PreparedEffectState::Staged => {
                let mut ledger = PreparedEffectLedger::new();
                ledger
                    .stage(&prepare_request(), MonotonicMillis(STAGED_AT))
                    .expect("staged");
                (ledger, MonotonicMillis(COMMIT_AT))
            }
            PreparedEffectState::Consumed => (consumed_ledger(), MonotonicMillis(COMMIT_AT)),
            PreparedEffectState::Committed => {
                let mut ledger = consumed_ledger();
                ledger
                    .commit(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
                    .expect("the first commit succeeds");
                (ledger, MonotonicMillis(COMMIT_AT))
            }
            // Expiry is computed from the record, so the state is reached by
            // asking later rather than by setting a flag.
            PreparedEffectState::Expired => (consumed_ledger(), MonotonicMillis(EXPIRES_AT + 1)),
            PreparedEffectState::Discarded => {
                let mut ledger = consumed_ledger();
                assert!(ledger.discard(&prepared_effect_id()));
                (ledger, MonotonicMillis(COMMIT_AT))
            }
        };

        let verdict = ledger.verdict(&binding(), &commit.context(), now);
        assert_eq!(
            verdict.is_ok(),
            state.is_committable(),
            "a commit against a {} record",
            state.label()
        );
        if let Err(refusal) = verdict {
            assert!(
                refusal.result_code().fails_closed(),
                "{} did not fail closed",
                state.label()
            );
        }
    }
}

/// A rendering is recorded in exactly one state, walked exhaustively.
///
/// The mirror of [`a_commit_is_refused_in_every_state_but_consumed`], and it
/// exists for the same reason: `record_rendered` is what fixes the binding a
/// commit is held to, so a second rendering — over a record already consumed,
/// already committed, or withdrawn — would move what a person is treated as
/// having read after they read it. Enumerated over
/// [`PreparedEffectState::ALL`] rather than exampled, so a state added to the
/// lifecycle has to decide what rendering against it means.
#[test]
fn a_rendering_is_recorded_in_every_state_but_staged_and_refused_in_the_rest() {
    let commit = Commit::default();
    let second = RenderedEffect {
        digest: RenderedEffectDigest::from_trusted_surface(digest("a-second-sentence")),
        rendered_at: MonotonicMillis(RENDERED_AT),
    };

    for state in PreparedEffectState::ALL {
        let (mut ledger, now) = match state {
            PreparedEffectState::Staged => {
                let mut ledger = PreparedEffectLedger::new();
                ledger
                    .stage(&prepare_request(), MonotonicMillis(STAGED_AT))
                    .expect("staged");
                (ledger, MonotonicMillis(RENDERED_AT))
            }
            PreparedEffectState::Consumed => (consumed_ledger(), MonotonicMillis(RENDERED_AT)),
            PreparedEffectState::Committed => {
                let mut ledger = consumed_ledger();
                ledger
                    .commit(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
                    .expect("the first commit succeeds");
                (ledger, MonotonicMillis(COMMIT_AT))
            }
            PreparedEffectState::Expired => (consumed_ledger(), MonotonicMillis(EXPIRES_AT + 1)),
            PreparedEffectState::Discarded => {
                let mut ledger = consumed_ledger();
                assert!(ledger.discard(&prepared_effect_id()));
                (ledger, MonotonicMillis(RENDERED_AT))
            }
        };

        let outcome = ledger.record_rendered(&prepared_effect_id(), &second, now);
        assert_eq!(
            outcome.is_ok(),
            *state == PreparedEffectState::Staged,
            "rendering against a {} record",
            state.label()
        );
        if *state != PreparedEffectState::Staged {
            // The refusal names the state it found, so the audit record says
            // which rule refused rather than that something did.
            assert_eq!(
                outcome,
                Err(PrepareError::NotStaged(*state)),
                "rendering against a {} record",
                state.label()
            );
            // And the refusal changed nothing: what a commit is compared
            // against is still whatever was rendered the first time.
            assert_eq!(
                ledger
                    .get(&prepared_effect_id())
                    .and_then(|entry| entry.rendered().map(|shown| shown.digest.clone())),
                Some(RenderedEffectDigest::from_trusted_surface(rendered_digest()))
            );
        }
    }

    // A rendering of a prepare nobody staged is refused for the absence, not
    // for the state.
    let mut empty = PreparedEffectLedger::new();
    assert_eq!(
        empty.record_rendered(&prepared_effect_id(), &second, MonotonicMillis(RENDERED_AT)),
        Err(PrepareError::Unknown)
    );
}

/// One prepare authorizes one commit.
#[test]
fn a_replayed_commit_meets_the_record_it_already_spent() {
    let mut ledger = consumed_ledger();
    let commit = Commit::default();

    let receipt = ledger
        .commit(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
        .expect("the first commit spends the preparation");
    assert_eq!(receipt.prepared_effect_id, prepared_effect_id());
    assert_eq!(receipt.ordinal, 0);
    // The receipt carries what was rendered, which is what the audit record
    // needs to say what the person approved.
    assert_eq!(
        receipt.rendered_effect,
        RenderedEffectDigest::from_trusted_surface(rendered_digest())
    );

    assert_eq!(
        ledger
            .commit(
                &binding(),
                &commit.context(),
                MonotonicMillis(COMMIT_AT + 1)
            )
            .map_err(CommitRefusal::result_code),
        Err(ActionResultCode::CommitWithoutPrepare)
    );
    assert_eq!(ledger.commits(), 1);
}

// ---------------------------------------------------------------------------
// A prepared effect expires
// ---------------------------------------------------------------------------

/// A commit against an expired preparation is refused, not renewed.
///
/// The expiry is computed from the record rather than stored, so the record
/// does not depend on a timer having run; and there is no method that moves an
/// expiry, so waiting longer never helps. Asking again after the refusal gets
/// the same answer, which is what "not renewed" means operationally.
#[test]
fn a_commit_against_an_expired_preparation_is_refused_and_never_renewed() {
    let mut ledger = consumed_ledger();
    let commit = Commit::default();

    // One millisecond before the expiry it still stands.
    assert_eq!(
        ledger.verdict(
            &binding(),
            &commit.context(),
            MonotonicMillis(EXPIRES_AT - 1)
        ),
        Ok(())
    );

    // At the expiry it does not: the boundary refuses, so a commit arriving
    // exactly on the expiry is late.
    for now in [EXPIRES_AT, EXPIRES_AT + 1, EXPIRES_AT + 10_000] {
        assert_eq!(
            ledger
                .verdict(&binding(), &commit.context(), MonotonicMillis(now))
                .map_err(CommitRefusal::result_code),
            Err(ActionResultCode::CommitWithoutPrepare),
            "at {now}"
        );
        assert_eq!(
            ledger
                .get(&prepared_effect_id())
                .map(|entry| entry.state_at(MonotonicMillis(now))),
            Some(PreparedEffectState::Expired)
        );
    }

    // Nothing was spent by the refusals, and committing is still impossible.
    assert_eq!(ledger.commits(), 0);
    assert!(ledger
        .commit(
            &binding(),
            &commit.context(),
            MonotonicMillis(EXPIRES_AT + 1)
        )
        .is_err());

    // An expired record cannot be brought back by rendering it again.
    assert_eq!(
        ledger.record_rendered(
            &prepared_effect_id(),
            &rendered(),
            MonotonicMillis(EXPIRES_AT + 1)
        ),
        Err(PrepareError::NotStaged(PreparedEffectState::Expired))
    );
}

/// A preparation cannot be staged already expired.
#[test]
fn a_preparation_whose_expiry_has_passed_is_never_staged() {
    let mut ledger = PreparedEffectLedger::new();
    for now in [EXPIRES_AT, EXPIRES_AT + 1] {
        assert_eq!(
            ledger.stage(&prepare_request(), MonotonicMillis(now)),
            Err(PrepareError::ExpiryNotInFuture)
        );
    }
    assert!(ledger.is_empty());
}

// ---------------------------------------------------------------------------
// The gesture must postdate the prepare
// ---------------------------------------------------------------------------

/// A confirmation that predates the prepare cannot have been a response to it.
///
/// This is the rule that makes "the model cannot commit on its own" true rather
/// than merely likely: a gesture exists only because the trusted surface minted
/// it from a real input event, and one minted before the prepare cannot be
/// spliced onto it afterwards.
#[test]
fn a_confirmation_that_does_not_postdate_the_prepare_is_refused() {
    // Before the prepare, and exactly at it — simultaneity is not a response.
    for gesture in [0, RENDERED_AT - 1, RENDERED_AT] {
        assert_eq!(
            commit_verdict(|commit| commit.binding.gesture_at_monotonic_ms = gesture),
            Err(ActionResultCode::CommitWithoutPrepare),
            "a gesture at {gesture}"
        );
    }
    // A gesture from the future has not happened either.
    assert_eq!(
        commit_verdict(|commit| commit.binding.gesture_at_monotonic_ms = COMMIT_AT + 1),
        Err(ActionResultCode::CommitWithoutPrepare)
    );
    // Strictly between the prepare and now is the only window that stands.
    assert_eq!(
        commit_verdict(|commit| commit.binding.gesture_at_monotonic_ms = RENDERED_AT + 1),
        Ok(())
    );
}

// ---------------------------------------------------------------------------
// Restart, and take-over
// ---------------------------------------------------------------------------

/// Kill and restart between the two steps, and no commit replays.
///
/// The ledger is session-lifetime and in memory, so a broker that restarted has
/// nothing staged. A commit arriving with a perfectly well-formed binding — the
/// same identity, the same digest, the same times — is refused, because the
/// prepare it names did not run in *this* broker.
#[test]
fn nothing_commits_across_a_restart() {
    let commit = Commit::default();

    let before = consumed_ledger();
    assert_eq!(
        before.verdict(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT)),
        Ok(())
    );

    // The process died here. Nothing is restored, because nothing was durable.
    let after = PreparedEffectLedger::new();
    assert!(after.is_empty());
    assert_eq!(
        after
            .verdict(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
            .map_err(CommitRefusal::result_code),
        Err(ActionResultCode::CommitWithoutPrepare)
    );
}

/// A take-over withdraws every uncommitted preparation staged under the lease.
#[test]
fn a_take_over_withdraws_what_was_staged_under_the_lease_it_ended() {
    let commit = Commit::default();

    let mut ledger = consumed_ledger();
    let withdrawn = ledger.discard_for_lease(&commit.lease_id);
    assert_eq!(withdrawn, vec![prepared_effect_id()]);
    assert_eq!(
        ledger
            .verdict(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
            .map_err(CommitRefusal::result_code),
        Err(ActionResultCode::CommitWithoutPrepare)
    );

    // A committed record is past recall, and a take-over does not pretend
    // otherwise.
    let mut spent = consumed_ledger();
    spent
        .commit(&binding(), &commit.context(), MonotonicMillis(COMMIT_AT))
        .expect("committed");
    assert!(spent.discard_for_lease(&commit.lease_id).is_empty());
    assert!(!spent.discard(&prepared_effect_id()));
}
