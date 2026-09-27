// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every action class, run through the production decision function.
//!
//! Protocol specification section 11.1 draws the line this suite defends: a
//! class may exist in the generated bindings and still be refused by the
//! production dispatcher. The taxonomy's own unit tests prove the table; this
//! suite proves the thing that actually decides — `PolicyEngine::decide_proposal`
//! with a standing lease, a valid scope, and nothing wrong except the class.
//!
//! The enumeration is exhaustive by construction: it walks `ActionClass::ALL`,
//! so a class added without a milestone entry is denied here and a class added
//! to the surface has to be added deliberately.

use bip_types::identity::{TabId, TaskId};
use bip_types::ActionResultCode;
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ClassAvailability, ControlMode, DenialReason, LeaseRequest, PolicyEngine,
    PolicyMilestone, PolicyVersion, ProposalDecision,
};

use self::fixtures::{answered, engine, proposal, surface_authorizes, LEASE_EXPIRY, NOW};

#[path = "action_surface/fixtures.rs"]
mod fixtures;

#[test]
fn every_class_off_the_ratified_surface_is_denied_at_every_milestone() {
    // The question is asked and answered *before* the proposal is decided, so
    // what this measures is whether the surface authorizes the class rather
    // than whether anybody remembered to confirm it.
    //
    // Without that this test would have gone on passing through the M5
    // ratification while saying nothing about it: a fill carrying no approval
    // resolves to `RequireApproval` and never to `Authorize`, so `authorized`
    // would have stayed false with the surface already open. A guard that
    // cannot observe the thing it guards against is not a guard.
    for milestone in PolicyMilestone::ALL {
        for (ordinal, class) in ActionClass::ALL.iter().enumerate() {
            let mut engine = engine(*milestone);
            let request = answered(&mut engine, &proposal(*class, ordinal));
            let decision = engine.decide_proposal(&request, NOW);
            let authorized = matches!(decision, ProposalDecision::Authorize(_));
            assert_eq!(
                authorized,
                surface_authorizes(*milestone, *class),
                "{} at {} decided {decision:?}",
                class.label(),
                milestone.label()
            );
        }
    }
}

#[test]
fn a_class_the_write_milestone_authorizes_is_a_question_until_it_is_answered() {
    // At M5 the browser-owned exact-value fill and download flows are on the
    // write surface, and both still need an exact answer. Unclassified form
    // writes are denied by the class gate rather than turned into a question.
    for (ordinal, class) in ActionClass::ALL.iter().enumerate() {
        let mut engine = engine(PolicyMilestone::M5);
        let decision = engine.decide_proposal(&proposal(*class, ordinal), NOW);
        let expected_question = surface_authorizes(PolicyMilestone::M5, *class)
            && class.availability() == ClassAvailability::WriteMilestone;
        if expected_question {
            assert!(
                matches!(decision, ProposalDecision::RequireApproval),
                "{} decided {decision:?} with no answer on file",
                class.label()
            );
        } else {
            assert!(
                !matches!(decision, ProposalDecision::RequireApproval),
                "{} asked a question nobody expected",
                class.label()
            );
        }
    }
}

#[test]
fn the_library_surface_opens_only_at_m6_and_keeps_writes_two_phase() {
    for milestone in [
        PolicyMilestone::M2,
        PolicyMilestone::M3,
        PolicyMilestone::M5,
    ] {
        let mut engine = engine(milestone);
        for (ordinal, class) in ActionClass::LIBRARY_MILESTONE.iter().enumerate() {
            match engine.decide_proposal(&proposal(*class, ordinal), NOW) {
                ProposalDecision::Deny(denial) => {
                    assert_eq!(denial.reason, DenialReason::ActionClassNotAuthorized);
                    assert_eq!(denial.code, ActionResultCode::DeniedByPolicy);
                }
                other => unreachable!(
                    "{} opened at {}: {other:?}",
                    class.label(),
                    milestone.label()
                ),
            }
        }
    }

    let mut engine = engine(PolicyMilestone::M6);
    assert!(matches!(
        engine.decide_proposal(&proposal(ActionClass::LibraryRead, 0), NOW),
        ProposalDecision::Authorize(_)
    ));
    let write = proposal(ActionClass::LibraryWrite, 1);
    assert!(matches!(
        engine.decide_proposal(&write, NOW),
        ProposalDecision::RequireApproval
    ));
    let answered_write = answered(&mut engine, &write);
    assert!(matches!(
        engine.decide_proposal(&answered_write, NOW),
        ProposalDecision::Authorize(_)
    ));
}

#[test]
fn the_memory_surface_opens_only_at_m6_and_keeps_writes_two_phase() {
    for milestone in [
        PolicyMilestone::M2,
        PolicyMilestone::M3,
        PolicyMilestone::M5,
    ] {
        let mut engine = engine(milestone);
        for (ordinal, class) in ActionClass::MEMORY_MILESTONE.iter().enumerate() {
            match engine.decide_proposal(&proposal(*class, ordinal), NOW) {
                ProposalDecision::Deny(denial) => {
                    assert_eq!(denial.reason, DenialReason::ActionClassNotAuthorized);
                    assert_eq!(denial.code, ActionResultCode::DeniedByPolicy);
                }
                other => unreachable!(
                    "{} opened at {}: {other:?}",
                    class.label(),
                    milestone.label()
                ),
            }
        }
    }

    let mut engine = engine(PolicyMilestone::M6);
    assert!(matches!(
        engine.decide_proposal(&proposal(ActionClass::MemoryRead, 0), NOW),
        ProposalDecision::Authorize(_)
    ));
    let write = proposal(ActionClass::MemoryWrite, 1);
    assert!(matches!(
        engine.decide_proposal(&write, NOW),
        ProposalDecision::RequireApproval
    ));
    let answered_write = answered(&mut engine, &write);
    assert!(matches!(
        engine.decide_proposal(&answered_write, NOW),
        ProposalDecision::Authorize(_)
    ));
}

#[test]
fn the_write_milestone_classes_are_denied_with_the_reason_that_names_them_until_one_takes_them() {
    for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
        let mut engine = engine(milestone);
        for (ordinal, class) in ActionClass::WRITE_MILESTONE.iter().enumerate() {
            match engine.decide_proposal(&proposal(*class, ordinal), NOW) {
                ProposalDecision::Deny(denial) => {
                    assert_eq!(
                        denial.reason,
                        DenialReason::ActionClassReservedForWriteMilestone,
                        "{} at {}",
                        class.label(),
                        milestone.label()
                    );
                    assert_eq!(denial.code, ActionResultCode::DeniedByPolicy);
                    // The sentence a person sees is a compiled-in template. It
                    // never quotes a page, a model, or a renderer.
                    assert!(!denial.reason.user_visible_reason().is_empty());
                }
                other => unreachable!("{} must be denied, got {other:?}", class.label()),
            }
        }
    }

    // At M5 exact-value fill and download initiation leave the reservation.
    // Select, toggle, and submit have no closed browser-owned consequence yet,
    // and upload has no assistant-controlled file-selection surface.
    let mut engine = engine(PolicyMilestone::M5);
    for (ordinal, class) in ActionClass::WRITE_MILESTONE.iter().enumerate() {
        let decision = engine.decide_proposal(&proposal(*class, ordinal), NOW);
        if matches!(*class, ActionClass::FillField | ActionClass::StartDownload) {
            assert!(
                !matches!(decision, ProposalDecision::Deny(_)),
                "{} is on the M5 surface and was denied: {decision:?}",
                class.label()
            );
        } else {
            match decision {
                ProposalDecision::Deny(denial) => {
                    assert_eq!(
                        denial.reason,
                        DenialReason::ActionClassReservedForWriteMilestone
                    );
                    assert_eq!(denial.code, ActionResultCode::DeniedByPolicy);
                }
                other => unreachable!("{} must be denied at M5, got {other:?}", class.label()),
            }
        }
    }
}

#[test]
fn the_denied_classes_cover_submit_upload_send_and_purchase() {
    // The roadmap names these four in so many words. Each has to be present as
    // a value and refused, so that refusing them is testable rather than
    // implicit in their absence.
    let mut engine = engine(PolicyMilestone::M2);
    for (ordinal, class) in [
        ActionClass::SubmitForm,
        ActionClass::UploadFile,
        ActionClass::SendMessage,
        ActionClass::Purchase,
    ]
    .into_iter()
    .enumerate()
    {
        let decision = engine.decide_proposal(&proposal(class, ordinal), NOW);
        assert_eq!(
            decision.result_code(),
            Some(ActionResultCode::DeniedByPolicy),
            "{}",
            class.label()
        );
    }
}

#[test]
fn a_prohibited_class_is_denied_with_no_lease_and_with_one() {
    let mut without_lease =
        PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    // The class is refused before the lease is even looked for, so a missing
    // lease never masks the reason.
    match without_lease.decide_proposal(&proposal(ActionClass::ExtractCredential, 0), NOW) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ActionClassProhibited);
        }
        other => unreachable!("expected a denial, got {other:?}"),
    }

    let mut with_lease = engine(PolicyMilestone::M2);
    match with_lease.decide_proposal(&proposal(ActionClass::BypassAccessControl, 1), NOW) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ActionClassProhibited);
        }
        other => unreachable!("expected a denial, got {other:?}"),
    }
}

#[test]
fn an_authorized_class_still_needs_a_standing_lease() {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    match engine.decide_proposal(&proposal(ActionClass::OpenLink, 0), NOW) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::NoStandingLease);
            assert_eq!(denial.code, ActionResultCode::ActorLeaseMissing);
        }
        other => unreachable!("expected a denial, got {other:?}"),
    }
}

#[test]
fn user_control_authorizes_nothing_on_the_ratified_surface_either() {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    let refused = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::User,
            expires_at: LEASE_EXPIRY,
        },
        NOW,
    );
    assert!(refused.is_err());

    for (ordinal, class) in ActionClass::ALL.iter().enumerate() {
        let decision = engine.decide_proposal(&proposal(*class, ordinal), NOW);
        assert!(
            !matches!(decision, ProposalDecision::Authorize(_)),
            "{} was authorized under user control",
            class.label()
        );
    }
}

#[test]
fn the_production_decision_names_one_reason_for_every_class_at_every_milestone() {
    // Every class times every ratified milestone, decided by the function the
    // browser broker actually calls, with the exact reason and the exact result
    // code written down. A class added to the taxonomy without an availability
    // does not compile; a class added to the list without a position fails the
    // const check in `action_class`; and a class that compiles and is listed
    // still lands here, where it is either on the surface deliberately or
    // refused with a reason somebody had to choose.
    //
    // The fixture proposal carries no approval, which is why the third arm
    // exists rather than being an `unreachable!`. Before M5 no class on the
    // surface needed one and the pair (`Authorize`, `None`) covered the whole
    // table; from M5 download initiation is on the surface *and* refused for
    // want of an answer, so "authorized by the surface" and "authorized right
    // now" are two different readings and this test states both.
    for milestone in PolicyMilestone::ALL {
        let mut engine = engine(*milestone);
        for (ordinal, class) in ActionClass::ALL.iter().enumerate() {
            let request = proposal(*class, ordinal);
            let decision = engine.decide_proposal(&request, NOW);
            let expected = DenialReason::for_class(*class, *milestone);
            match (decision, expected) {
                (ProposalDecision::Authorize(_), None) => {
                    // Nothing the surface authorizes at a read-oriented risk is
                    // asked about, so an authorization here is also a statement
                    // that no confirmation was owed.
                    assert!(!request.effective_risk().requires_exact_approval());
                }
                (ProposalDecision::RequireApproval, None) => {
                    // On the surface, and waiting for a person. The two ways to
                    // owe a question are the risk lattice's exact-approval rule
                    // and decision 0089's per-use rule, and at least one of them
                    // has to be what is owed.
                    assert!(
                        request.effective_risk().requires_exact_approval()
                            || policy_engine::per_use_confirmation_required(
                                *class,
                                request.data_classes
                            ),
                        "{} at {} asked a question nothing owes",
                        class.label(),
                        milestone.label()
                    );
                }
                (ProposalDecision::Deny(denial), Some(reason)) => {
                    assert_eq!(
                        denial.reason,
                        reason,
                        "{} at {}",
                        class.label(),
                        milestone.label()
                    );
                    assert_eq!(denial.code, reason.result_code());
                    assert!(denial.code.fails_closed());
                }
                (decision, expected) => unreachable!(
                    "{} at {} decided {decision:?} against {expected:?}",
                    class.label(),
                    milestone.label()
                ),
            }
        }
    }
}

#[test]
fn every_class_the_release_excludes_is_present_as_a_value_and_refused() {
    // Refusing by leaving a value out is invisible; refusing by naming it is a
    // table. This asserts the table covers the write milestone, the excluded
    // commitments, and the permanent prohibitions, and that none of them is
    // empty — an empty group would make the proof above vacuous.
    let groups: [(ClassAvailability, DenialReason); 6] = [
        (
            ClassAvailability::WriteMilestone,
            DenialReason::ActionClassReservedForWriteMilestone,
        ),
        (
            ClassAvailability::LibraryMilestone,
            DenialReason::ActionClassNotAuthorized,
        ),
        (
            ClassAvailability::MemoryMilestone,
            DenialReason::ActionClassNotAuthorized,
        ),
        (
            ClassAvailability::ComputationMilestone,
            DenialReason::ActionClassNotAuthorized,
        ),
        (
            ClassAvailability::ExcludedFromRelease,
            DenialReason::ActionClassExcludedFromRelease,
        ),
        (
            ClassAvailability::Prohibited,
            DenialReason::ActionClassProhibited,
        ),
    ];
    for (availability, reason) in groups {
        let members: Vec<&ActionClass> = ActionClass::ALL
            .iter()
            .filter(|class| class.availability() == availability)
            .collect();
        assert!(!members.is_empty(), "{reason:?} covers no class");
        let mut engine = engine(PolicyMilestone::M2);
        for (ordinal, class) in members.into_iter().enumerate() {
            match engine.decide_proposal(&proposal(*class, ordinal), NOW) {
                ProposalDecision::Deny(denial) => {
                    assert_eq!(denial.reason, reason, "{}", class.label());
                }
                other => unreachable!("{} must be denied, got {other:?}", class.label()),
            }
        }
    }
}

#[path = "action_surface/risk_and_parity.rs"]
mod risk_and_parity;
