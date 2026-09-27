// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the broker does with a prepared effect, and why none of it is reachable
//! yet (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`).
//!
//! The authorization half of the record's validation section: the shape of a
//! commit is checked before the ledger is read, a standing nobody asked for
//! refuses, the broker's stage-and-render surface mints nothing, and — the
//! claim the phase table exists to keep — a prepare is never authorized where
//! its commit was not, so the phase widened nothing.
//!
//! Milestone M5 authorizes browser-owned exact-value fill and download flows,
//! so two two-phase classes answer `RequireApproval` instead of a denial. The
//! remaining form mutations stay refused pending closed consequence
//! classification. The claim is therefore also stated over authority, where
//! it cannot be satisfied by a question.
//!
//! The ledger's own comparisons are `tests/prepared_effect_ledger.rs`.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::action::{ActionType, PreconditionKind};
use bip_types::identity::{MonotonicMillis, SemanticNodeId, TabId};
use bip_types::snapshot::SemanticRole;
use bip_types::ActionResultCode;

use policy_engine::prepared::{CommitPlan, PreparedEffectStanding};
use policy_engine::{
    plan_commit, ActionClass, ActionPhase, DispatchProposal, PolicyMilestone, PrepareError,
    ProposalDecision, RenderedEffect, RenderedEffectDigest, RevocationReason,
};

use common::{
    binding, consumed_ledger, engine_with_lease, observed, precondition, prepare_request,
    prepared_effect_id, proposal_digest, proposal_for, rendered, rendered_digest, Commit,
    COMMIT_AT, RENDERED_AT, STAGED_AT,
};

// ---------------------------------------------------------------------------
// Planning: the shape of the proposal
// ---------------------------------------------------------------------------

/// A commit of a two-phase class that claims no prepare is malformed, and so is
/// one that claims two.
#[test]
fn the_shape_of_a_commit_is_checked_before_the_ledger_is_read() {
    let ledger = consumed_ledger();
    let commit = Commit::default();

    // A commit of a two-phase class naming no prepare has invented the absence
    // of the check.
    assert_eq!(
        plan_commit(&ledger, &commit.context(), &[], MonotonicMillis(COMMIT_AT)),
        CommitPlan::Malformed(ActionResultCode::CommitWithoutPrepare)
    );

    // Two bindings name none: there is no rule for choosing between them that
    // is not a rule for ignoring one.
    let two = [
        precondition(PreconditionKind::PreparedEffectUnchanged, Some(binding())),
        precondition(PreconditionKind::PreparedEffectUnchanged, Some(binding())),
    ];
    assert_eq!(
        plan_commit(&ledger, &commit.context(), &two, MonotonicMillis(COMMIT_AT)),
        CommitPlan::Malformed(ActionResultCode::CommitWithoutPrepare)
    );

    let one = [precondition(
        PreconditionKind::PreparedEffectUnchanged,
        Some(binding()),
    )];

    // A prepare carrying a binding has no prepare of its own to complete.
    let mut prepare = commit.context();
    prepare.phase = ActionPhase::Prepare;
    assert_eq!(
        plan_commit(&ledger, &prepare, &one, MonotonicMillis(COMMIT_AT)),
        CommitPlan::Malformed(ActionResultCode::DeniedByPolicy)
    );

    // A class nobody is ever asked about has nothing to have prepared.
    let read = Commit {
        action_class: ActionClass::OpenLink,
        ..Commit::default()
    };
    assert_eq!(
        plan_commit(&ledger, &read.context(), &one, MonotonicMillis(COMMIT_AT)),
        CommitPlan::Malformed(ActionResultCode::DeniedByPolicy)
    );
    // ...and, carrying nothing, commits nothing prepared.
    assert_eq!(
        plan_commit(&ledger, &read.context(), &[], MonotonicMillis(COMMIT_AT)),
        CommitPlan::NoPreparedEffect
    );

    // The well-formed commit stands, and planning spends nothing.
    assert_eq!(
        plan_commit(&ledger, &commit.context(), &one, MonotonicMillis(COMMIT_AT)),
        CommitPlan::Committable(binding())
    );
    assert_eq!(ledger.commits(), 0);
}

/// A standing nobody asked the ledger about refuses.
///
/// The default is the refusing one, so a caller that holds no ledger cannot
/// forget the field into a pass: reporting the effect unchanged would claim a
/// comparison nothing performed.
#[test]
fn an_unasked_ledger_never_reports_an_effect_unchanged() {
    assert_eq!(
        PreparedEffectStanding::default(),
        PreparedEffectStanding::NotChecked
    );
    assert_eq!(
        PreparedEffectStanding::NotChecked.refusal(),
        Some(ActionResultCode::Unsupported)
    );
    assert_eq!(PreparedEffectStanding::Unchanged.refusal(), None);
    assert_eq!(
        CommitPlan::NoPreparedEffect.standing(),
        PreparedEffectStanding::NotChecked
    );
    assert_eq!(
        CommitPlan::Malformed(ActionResultCode::CommitWithoutPrepare).standing(),
        PreparedEffectStanding::NotChecked
    );
}

// ---------------------------------------------------------------------------
// The mechanism was inert until M5, and what it does now that it is not
// ---------------------------------------------------------------------------

/// A commit at M3 is refused.
///
/// The headline inertness claim, kept as the statement about M3 that it always
/// was. Every class that is prepared and committed separately was a class no
/// ratified milestone authorized, so a commit of one at M3 never becomes
/// authority, whatever else is in order. M3 is no longer the newest ratified
/// surface — decision 0089 ratified M5 — and the test below is what says what
/// happens there.
#[test]
fn a_commit_at_m3_is_refused() {
    let two_phase: Vec<ActionClass> = ActionClass::ALL
        .iter()
        .copied()
        .filter(|class| class.is_two_phase())
        .collect();
    assert!(
        !two_phase.is_empty(),
        "the mechanism would be vacuous with no two-phase class"
    );

    for class in two_phase {
        let mut engine = engine_with_lease(PolicyMilestone::M3);
        let decision = engine.decide_proposal(
            &proposal_for(class, ActionPhase::Commit),
            MonotonicMillis(1),
        );
        match decision {
            ProposalDecision::Deny(denial) => {
                assert!(
                    denial.code.fails_closed(),
                    "committing {} did not fail closed",
                    class.label()
                );
            }
            other => unreachable!(
                "committing {} at M3 must be refused, got {other:?}",
                class.label()
            ),
        }
    }
}

/// The phase widened nothing: a prepare is refused wherever the single-step
/// form is, for every class and every ratified milestone.
///
/// Decision 0022's own validation line. The phase-aware baseline makes a
/// prepare cheaper than its commit, and this is the assertion that "cheaper"
/// never crossed into "authorizable": if the committed form of a class is
/// refused at a milestone, the prepared form is refused there too.
///
/// This test walks `PolicyMilestone::ALL`, so decision 0089 put M5 into it
/// without a line changing. What M5 changed is what the original implication
/// measures. `result_code().is_some()` was a synonym for "was refused" while
/// every two-phase class was refused everywhere; at M5 download initiation is
/// on the surface and answers `RequireApproval`, which carries
/// `ApprovalRequired`, so the implication is now satisfiable by a question as
/// well as by a refusal — true, and weaker than it reads. The second assertion
/// is the one that does not weaken, because a question is not authorization.
#[test]
fn a_prepare_is_refused_wherever_the_single_step_form_is() {
    for milestone in PolicyMilestone::ALL {
        for class in ActionClass::ALL {
            let mut committed = engine_with_lease(*milestone);
            let commit_decision = committed.decide_proposal(
                &proposal_for(*class, ActionPhase::Commit),
                MonotonicMillis(1),
            );

            let mut prepared = engine_with_lease(*milestone);
            let prepare_decision = prepared.decide_proposal(
                &proposal_for(*class, ActionPhase::Prepare),
                MonotonicMillis(1),
            );

            let commit_authorized = matches!(commit_decision, ProposalDecision::Authorize(_));
            let prepare_authorized = matches!(prepare_decision, ProposalDecision::Authorize(_));

            if commit_decision.result_code().is_some() {
                assert!(
                    prepare_decision.result_code().is_some(),
                    "preparing {} became authorizable at {}",
                    class.label(),
                    milestone.label()
                );
            }

            assert!(
                commit_authorized || !prepare_authorized,
                "preparing {} was authorized at {} where committing it was not",
                class.label(),
                milestone.label()
            );
        }
    }
}

/// What the write milestone actually did to the two-phase mechanism.
///
/// The inertness claim above was "no ratified milestone authorizes a two-phase
/// class", and M5 ends it for the browser-owned download flow. That is the
/// mechanism starting rather than a hole, and the difference is worth an
/// assertion of its own: every two-phase class M5 put on the surface is asked
/// about before it is authorized, and every one it did not is still refused
/// outright.
#[test]
fn the_write_milestone_starts_the_mechanism_rather_than_bypassing_it() {
    for class in ActionClass::ALL {
        if !class.is_two_phase() {
            continue;
        }
        let mut engine = engine_with_lease(PolicyMilestone::M5);
        let decision = engine.decide_proposal(
            &proposal_for(*class, ActionPhase::Commit),
            MonotonicMillis(1),
        );
        if class.is_authorized_at(PolicyMilestone::M5) {
            assert!(
                matches!(decision, ProposalDecision::RequireApproval),
                "{} is on the M5 surface and decided {decision:?} with nothing confirmed",
                class.label()
            );
        } else {
            match decision {
                ProposalDecision::Deny(denial) => assert!(
                    denial.code.fails_closed(),
                    "committing {} did not fail closed",
                    class.label()
                ),
                other => unreachable!(
                    "{} is off the M5 surface and decided {other:?}",
                    class.label()
                ),
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The broker's own surface
// ---------------------------------------------------------------------------

/// The broker's prepared-effect surface stages, renders, and refuses — and
/// staging hands back nothing a commit could present.
#[test]
fn the_broker_stages_and_renders_without_minting_anything() {
    let mut engine = engine_with_lease(PolicyMilestone::M3);

    // Staging returns the unit, which is the type-level statement that
    // preparation mints nothing.
    let staged: Result<(), PrepareError> =
        engine.stage_prepared_effect(&prepare_request(), MonotonicMillis(STAGED_AT));
    assert_eq!(staged, Ok(()));
    assert_eq!(engine.prepared_effects().len(), 1);

    // The same identity cannot be staged twice.
    assert_eq!(
        engine.stage_prepared_effect(&prepare_request(), MonotonicMillis(STAGED_AT)),
        Err(PrepareError::AlreadyStaged)
    );

    // A rendering dated before the prepare, or after the present, is refused.
    let backdated = RenderedEffect {
        digest: RenderedEffectDigest::from_trusted_surface(rendered_digest()),
        rendered_at: MonotonicMillis(STAGED_AT - 1),
    };
    assert_eq!(
        engine.record_rendered_effect(
            &prepared_effect_id(),
            &backdated,
            MonotonicMillis(RENDERED_AT)
        ),
        Err(PrepareError::RenderingOutOfOrder)
    );

    engine
        .record_rendered_effect(
            &prepared_effect_id(),
            &rendered(),
            MonotonicMillis(RENDERED_AT),
        )
        .expect("the rendering is recorded");

    // Withdrawing it is possible exactly once while it is live.
    assert!(engine.discard_prepared_effect(&prepared_effect_id()));
    assert!(!engine.discard_prepared_effect(&prepared_effect_id()));
}

/// A take-over reports the preparations it withdrew.
#[test]
fn a_take_over_reports_the_preparations_it_ended() {
    let mut engine = engine_with_lease(PolicyMilestone::M3);
    let lease = engine
        .active_lease(&TabId::new("tab_1"), MonotonicMillis(1))
        .expect("the lease is standing")
        .lease_id()
        .clone();

    let mut request = prepare_request();
    request.lease_id = lease;
    engine
        .stage_prepared_effect(&request, MonotonicMillis(STAGED_AT))
        .expect("staged");

    let take_over = engine.user_took_over(&TabId::new("tab_1"), RevocationReason::UserTookOver);
    assert_eq!(
        take_over.discarded_prepared_effects,
        vec![prepared_effect_id()]
    );
    assert!(take_over.changed_anything());
}

/// A read-oriented class carrying a prepared-effect binding is refused before
/// the sequence runs.
///
/// The malformed shape is a fact about the authority rather than about the
/// world, so it lands beside the digest and scope checks at step four rather
/// than being reported as a precondition that failed on the page.
#[test]
fn a_dispatch_carrying_a_binding_it_cannot_have_earned_is_refused() {
    let mut engine = engine_with_lease(PolicyMilestone::M3);
    // An open link is on the ratified surface, so this dispatch gets far enough
    // to be about the prepared effect rather than about the class.
    let decision = engine.decide_proposal(
        &proposal_for(ActionClass::OpenLink, ActionPhase::Commit),
        MonotonicMillis(1),
    );
    let ProposalDecision::Authorize(authorization) = decision else {
        unreachable!("an open-link commit under a standing lease is authorized")
    };

    let proposal = DispatchProposal {
        action_digest: proposal_digest(),
        node_id: Some(SemanticNodeId::new("n_send")),
        expected_role: SemanticRole::Button,
        action_type: ActionType::Activate,
        expected_destination: None,
        preconditions: vec![precondition(
            PreconditionKind::PreparedEffectUnchanged,
            Some(binding()),
        )],
    };
    let decision = engine.authorize_dispatch(
        &authorization.capability_id,
        &proposal,
        &observed(),
        MonotonicMillis(2),
    );
    assert_eq!(
        decision.result_code(),
        Some(ActionResultCode::DeniedByPolicy)
    );
}
