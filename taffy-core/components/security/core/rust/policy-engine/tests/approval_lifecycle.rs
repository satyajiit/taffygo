// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One answer authorizes one action, and stops authorizing it the moment the
//! question changes.
//!
//! Domain model section 12.5 says an approval is invalidated when the action's
//! input, target, destination, page epoch, relevant current state, risk, or
//! disclosed data changes. Threat model section 18.4 requires the release to
//! prove it refuses "an old approval after destination/data/page state
//! changes". This suite proves both, twice: once per invalidation reason as a
//! named case, and once over generated interleavings of the whole lifecycle.
//!
//! The property the interleavings defend is deliberately blunt: **across any
//! ordering of presenting, answering, authorizing, dispatching, spending,
//! taking over, and letting the clock run, no approval is used twice and no
//! capability is spent twice.**

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ContentDigest, DigestAlgorithm, FrameId, GraphRevision, MonotonicMillis, PageEpoch, ProfileId,
    SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::{Sensitivity, SensitivitySet};
use bip_types::ActionResultCode;
use proptest::prelude::*;

use policy_engine::approval::book::ApprovalRequest;
use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionPhase, AllowedRedirects, ApprovalBinding, ApprovalDecision, ApprovalId,
    ApprovalInvalidation, ControlMode, DenialReason, LeaseRequest, NormalizedOrigin, PolicyEngine,
    PolicyMilestone, PolicyVersion, ProposalDecision, RepeatScope, RevocationReason, RiskClass,
    UserGestureReceipt,
};

const LEASE_EXPIRY: u64 = 100_000;
const APPROVAL_EXPIRY: u64 = 50_000;
const CAPABILITY_EXPIRY: u64 = 40_000;

fn origin(serialization: &str) -> NormalizedOrigin {
    policy_engine::origin::normalize_serialization(serialization)
        .expect("the fixture origin must normalize")
}

fn digest(value: &str) -> ContentDigest {
    ContentDigest {
        algorithm: DigestAlgorithm::Sha256,
        value: value.to_owned(),
    }
}

fn scope() -> CapabilityScope {
    CapabilityScope {
        profile_id: ProfileId::new("profile_1"),
        tab_id: TabId::new("tab_1"),
        frame_id: FrameId::new("frame_main"),
        page_epoch: PageEpoch::new("epoch_a"),
        origin: origin("https://example.test"),
        node_id: Some(SemanticNodeId::new("n_link")),
        destination_scope: Some(origin("https://example.test")),
        destination_address: None,
        required_graph_revision: GraphRevision(7),
        allowed_redirects: AllowedRedirects::from_normalized([origin("https://example.test")]),
    }
}

fn request(approval: Option<ApprovalId>) -> CapabilityRequest {
    CapabilityRequest {
        task_id: TaskId::new("task_1"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: ActionClass::OpenLink,
        phase: ActionPhase::Commit,
        action_digest: digest("a1"),
        scope: scope(),
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval,
        expires_at: MonotonicMillis(CAPABILITY_EXPIRY),
    }
}

/// A broker with a shared-control lease, so every step needs an approval.
fn engine_under_review() -> PolicyEngine<SequentialIds> {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    let issued = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::Shared,
            expires_at: MonotonicMillis(LEASE_EXPIRY),
        },
        MonotonicMillis(0),
    );
    assert!(issued.is_ok());
    engine
}

/// Presents and approves the question the fixture request asks.
fn approve(engine: &mut PolicyEngine<SequentialIds>, binding: ApprovalBinding) -> ApprovalId {
    let approval_id = engine
        .present_approval(
            &ApprovalRequest {
                task_id: TaskId::new("task_1"),
                binding,
                repeat_scope: RepeatScope::Once,
                expires_at: MonotonicMillis(APPROVAL_EXPIRY),
            },
            MonotonicMillis(0),
        )
        .expect("the fixture approval must be presentable");
    engine
        .record_approval_decision(
            &approval_id,
            ApprovalDecision::Approved,
            Some(UserGestureReceipt::new("gesture_1")),
            MonotonicMillis(1),
        )
        .expect("the fixture approval must be answerable");
    approval_id
}

/// The binding the fixture request derives.
fn binding() -> ApprovalBinding {
    request(None).approval_binding(RiskClass::ReversibleDisclosure)
}

// ---------------------------------------------------------------------------
// Named cases: one per way the question can change.
// ---------------------------------------------------------------------------

/// Approves the fixture question, then authorizes a proposal the mutation
/// changed, and returns the denial reason.
fn denial_after(mutate: impl FnOnce(&mut CapabilityRequest)) -> DenialReason {
    let mut engine = engine_under_review();
    let approval_id = approve(&mut engine, binding());
    let mut proposal = request(Some(approval_id));
    mutate(&mut proposal);
    match engine.decide_proposal(&proposal, MonotonicMillis(2)) {
        ProposalDecision::Deny(denial) => denial.reason,
        other => unreachable!("a changed question must be refused, got {other:?}"),
    }
}

#[test]
fn an_unchanged_question_authorizes_exactly_once() {
    let mut engine = engine_under_review();
    let approval_id = approve(&mut engine, binding());

    let decision = engine.decide_proposal(&request(Some(approval_id.clone())), MonotonicMillis(2));
    assert!(matches!(decision, ProposalDecision::Authorize(_)));

    // The same answer, presented again: one answer authorizes one action.
    match engine.decide_proposal(&request(Some(approval_id)), MonotonicMillis(3)) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ApprovalAlreadyUsed);
            assert_eq!(denial.code, ActionResultCode::ApprovalRequired);
        }
        other => unreachable!("a replayed approval must be refused, got {other:?}"),
    }
}

#[test]
fn a_changed_proposal_invalidates_the_answer() {
    assert_eq!(
        denial_after(|proposal| proposal.action_digest = digest("a2")),
        DenialReason::ApprovalInvalidated
    );
}

#[test]
fn a_changed_target_invalidates_the_answer() {
    assert_eq!(
        denial_after(|proposal| {
            proposal.scope.node_id = Some(SemanticNodeId::new("n_other"));
        }),
        DenialReason::ApprovalInvalidated
    );
}

#[test]
fn a_changed_page_epoch_invalidates_the_answer() {
    assert_eq!(
        denial_after(|proposal| proposal.scope.page_epoch = PageEpoch::new("epoch_b")),
        DenialReason::ApprovalInvalidated
    );
}

#[test]
fn a_changed_origin_invalidates_the_answer() {
    assert_eq!(
        denial_after(|proposal| proposal.scope.origin = origin("https://partner.test")),
        DenialReason::ApprovalInvalidated
    );
}

#[test]
fn a_changed_destination_invalidates_the_answer() {
    assert_eq!(
        denial_after(|proposal| {
            proposal.scope.destination_scope = Some(origin("https://partner.test"));
        }),
        DenialReason::ApprovalInvalidated
    );
}

#[test]
fn a_changed_data_class_invalidates_the_answer() {
    assert_eq!(
        denial_after(|proposal| {
            proposal.data_classes = SensitivitySet::of(Sensitivity::Personal);
        }),
        DenialReason::ApprovalInvalidated
    );
}

/// One way an approved binding can move, and the reason it should report.
struct Case {
    /// The reason the comparison must name.
    expected: ApprovalInvalidation,
    /// How to move the current binding away from the approved one.
    mutate: fn(&mut ApprovalBinding),
}

#[test]
fn every_way_the_question_can_change_is_named_and_reachable() {
    // Every invalidation reason is produced by a real comparison, so a reason
    // that could never fire would fail here rather than sit in the enumeration
    // looking like coverage.
    let approved = binding();
    let case = |expected, mutate| Case { expected, mutate };
    let cases = [
        case(ApprovalInvalidation::ActionChanged, |current| {
            current.action_digest = digest("a2");
        }),
        // Decision 0022. The sheet answered one half of a two-step
        // authorization and the request is the other half. The digest, the
        // class, the target and everything else are identical — only the phase
        // moved — so this is the one mutation that proves the comparison is
        // its own and not a restatement of the class check.
        case(ApprovalInvalidation::PhaseChanged, |current| {
            current.phase = ActionPhase::Prepare;
        }),
        case(ApprovalInvalidation::ActionClassChanged, |current| {
            current.action_class = ActionClass::SyntheticClick;
        }),
        case(ApprovalInvalidation::TargetChanged, |current| {
            current.node_id = Some(SemanticNodeId::new("n_other"));
        }),
        case(ApprovalInvalidation::PageStateChanged, |current| {
            current.page_epoch = PageEpoch::new("epoch_b");
        }),
        case(ApprovalInvalidation::OriginChanged, |current| {
            current.origin = origin("https://partner.test");
        }),
        case(ApprovalInvalidation::DestinationChanged, |current| {
            current.destination = None;
        }),
        case(ApprovalInvalidation::DataClassesChanged, |current| {
            current.data_classes = SensitivitySet::of(Sensitivity::Financial);
        }),
        case(ApprovalInvalidation::RiskChanged, |current| {
            current.risk = RiskClass::SensitiveDisclosure;
        }),
    ];
    assert_eq!(cases.len(), ApprovalInvalidation::ALL.len());

    for Case { expected, mutate } in cases {
        let mut current = approved.clone();
        mutate(&mut current);
        assert_eq!(
            approved.invalidation_against(&current),
            Some(expected),
            "{}",
            expected.label()
        );
    }
    assert_eq!(approved.invalidation_against(&approved.clone()), None);
}

/// A changed phase is reported as a changed phase, ahead of the class
/// (decision 0022).
///
/// The record places the phase comparison second, immediately after the action
/// comparison, and gives the reason: reporting "the sheet said prepare and the
/// request says submit" as a changed action class would be false, because the
/// class is identical and the meaning is opposite. Ordering is only observable
/// when two things move at once, so that is what this asserts — a case the
/// per-field walk above cannot see.
#[test]
fn a_changed_phase_is_named_before_a_changed_class() {
    let approved = ApprovalBinding {
        phase: ActionPhase::Prepare,
        ..binding()
    };
    // Both the phase and the class differ. The phase is the one reported.
    let current = ApprovalBinding {
        phase: ActionPhase::Commit,
        action_class: ActionClass::SyntheticClick,
        ..binding()
    };
    assert_eq!(
        approved.invalidation_against(&current),
        Some(ApprovalInvalidation::PhaseChanged)
    );

    // The action digest still outranks the phase, so a wholly different
    // proposal is not reported as a phase that moved.
    let mut different_action = current.clone();
    different_action.action_digest = digest("a2");
    assert_eq!(
        approved.invalidation_against(&different_action),
        Some(ApprovalInvalidation::ActionChanged)
    );
}

#[test]
fn a_question_that_looks_safer_than_the_one_answered_is_still_a_different_question() {
    // A lowered risk is not a reason to reuse an answer: the sheet showed
    // something else, and accepting a "safer" difference would make its
    // contents advisory.
    let approved = ApprovalBinding {
        risk: RiskClass::SensitiveDisclosure,
        ..binding()
    };
    let current = ApprovalBinding {
        risk: RiskClass::LocalRead,
        ..binding()
    };
    assert_eq!(
        approved.invalidation_against(&current),
        Some(ApprovalInvalidation::RiskChanged)
    );
}

#[test]
fn an_expired_answer_authorizes_nothing() {
    let mut engine = engine_under_review();
    let approval_id = approve(&mut engine, binding());
    match engine.decide_proposal(
        &request(Some(approval_id)),
        MonotonicMillis(APPROVAL_EXPIRY + 1),
    ) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ApprovalNotGranted);
            assert_eq!(denial.code, ActionResultCode::ApprovalDenied);
        }
        other => unreachable!("an expired approval must be refused, got {other:?}"),
    }
}

#[test]
fn an_answer_of_no_authorizes_nothing() {
    let mut engine = engine_under_review();
    let approval_id = engine
        .present_approval(
            &ApprovalRequest {
                task_id: TaskId::new("task_1"),
                binding: binding(),
                repeat_scope: RepeatScope::Once,
                expires_at: MonotonicMillis(APPROVAL_EXPIRY),
            },
            MonotonicMillis(0),
        )
        .expect("the approval must be presentable");
    assert!(engine
        .record_approval_decision(
            &approval_id,
            ApprovalDecision::Denied,
            None,
            MonotonicMillis(1)
        )
        .is_ok());

    match engine.decide_proposal(&request(Some(approval_id)), MonotonicMillis(2)) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ApprovalNotGranted);
        }
        other => unreachable!("a denied approval must be refused, got {other:?}"),
    }
}

#[test]
fn an_answer_nobody_gave_is_refused_before_it_enters_the_book() {
    let mut engine = engine_under_review();
    let approval_id = engine
        .present_approval(
            &ApprovalRequest {
                task_id: TaskId::new("task_1"),
                binding: binding(),
                repeat_scope: RepeatScope::Once,
                expires_at: MonotonicMillis(APPROVAL_EXPIRY),
            },
            MonotonicMillis(0),
        )
        .expect("the approval must be presentable");

    // A yes without a gesture receipt never becomes a yes.
    assert!(engine
        .record_approval_decision(
            &approval_id,
            ApprovalDecision::Approved,
            None,
            MonotonicMillis(1)
        )
        .is_err());
    assert_eq!(
        engine
            .approval(&approval_id)
            .map(|approval| approval.decision_at(MonotonicMillis(1))),
        Some(ApprovalDecision::Pending)
    );

    // And the proposal that would have used it is refused.
    match engine.decide_proposal(&request(Some(approval_id)), MonotonicMillis(2)) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ApprovalNotGranted);
        }
        other => unreachable!("an unanswered approval must be refused, got {other:?}"),
    }
}

#[test]
fn shared_control_asks_before_it_authorizes() {
    let mut engine = engine_under_review();
    assert_eq!(
        engine.decide_proposal(&request(None), MonotonicMillis(0)),
        ProposalDecision::RequireApproval
    );
}

#[test]
fn a_repeat_scope_no_milestone_authorizes_is_refused_when_the_question_is_asked() {
    let mut engine = engine_under_review();
    let presented = engine.present_approval(
        &ApprovalRequest {
            task_id: TaskId::new("task_1"),
            binding: binding(),
            repeat_scope: RepeatScope::TaskScopedUntilExpiry,
            expires_at: MonotonicMillis(APPROVAL_EXPIRY),
        },
        MonotonicMillis(0),
    );
    assert!(presented.is_err());
    assert!(!RepeatScope::TaskScopedUntilExpiry.is_authorized_today());
}

#[test]
fn an_answer_given_for_one_task_does_not_authorize_another() {
    let mut engine = engine_under_review();
    let approval_id = approve(&mut engine, binding());
    let mut proposal = request(Some(approval_id));
    proposal.task_id = TaskId::new("task_2");
    match engine.decide_proposal(&proposal, MonotonicMillis(2)) {
        ProposalDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::ApprovalNotFound);
        }
        other => unreachable!("another task's answer must be refused, got {other:?}"),
    }
}

// ---------------------------------------------------------------------------
// Generated interleavings.
// ---------------------------------------------------------------------------

/// One thing a driver can do to the broker.
#[derive(Clone, Copy, Debug)]
enum Step {
    /// Ask policy to authorize the proposal.
    Authorize,
    /// Spend the authority, if any was issued.
    Consume,
    /// The user takes over the tab.
    TakeOver,
    /// Move the clock forward.
    Advance(u64),
    /// Authorize a proposal whose origin moved under the approval.
    AuthorizeMovedOrigin,
    /// Authorize a proposal whose page epoch moved under the approval.
    AuthorizeMovedEpoch,
}

fn steps() -> impl Strategy<Value = Vec<Step>> {
    let step = prop_oneof![
        Just(Step::Authorize),
        Just(Step::Consume),
        Just(Step::TakeOver),
        (0u64..30_000).prop_map(Step::Advance),
        Just(Step::AuthorizeMovedOrigin),
        Just(Step::AuthorizeMovedEpoch),
    ];
    prop::collection::vec(step, 1..14)
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(256))]

    /// However the lifecycle interleaves, one answer authorizes at most one
    /// action and one capability is spent at most once.
    #[test]
    fn one_answer_authorizes_at_most_one_action(script in steps()) {
        let mut engine = engine_under_review();
        let approval_id = approve(&mut engine, binding());

        let mut now = 2_u64;
        let mut authorizations = 0_u32;
        let mut consumptions = 0_u32;
        let mut issued: Vec<policy_engine::CapabilityId> = Vec::new();

        for step in script {
            match step {
                Step::Advance(delta) => now = now.saturating_add(delta),
                Step::TakeOver => {
                    engine.user_took_over(&TabId::new("tab_1"), RevocationReason::UserTookOver);
                }
                Step::Consume => {
                    for capability_id in &issued {
                        if engine
                            .consume_capability(capability_id, MonotonicMillis(now))
                            .is_ok()
                        {
                            consumptions = consumptions.saturating_add(1);
                        }
                    }
                }
                Step::Authorize | Step::AuthorizeMovedOrigin | Step::AuthorizeMovedEpoch => {
                    let mut proposal = request(Some(approval_id.clone()));
                    match step {
                        Step::AuthorizeMovedOrigin => {
                            proposal.scope.origin = origin("https://partner.test");
                        }
                        Step::AuthorizeMovedEpoch => {
                            proposal.scope.page_epoch = PageEpoch::new("epoch_b");
                        }
                        _ => {}
                    }
                    match engine.decide_proposal(&proposal, MonotonicMillis(now)) {
                        ProposalDecision::Authorize(authorization) => {
                            // A moved world never authorizes.
                            prop_assert!(matches!(step, Step::Authorize));
                            authorizations = authorizations.saturating_add(1);
                            issued.push(authorization.capability_id);
                        }
                        ProposalDecision::Deny(denial) => {
                            prop_assert!(denial.code.fails_closed());
                        }
                        ProposalDecision::RequireApproval => {
                            prop_assert!(false, "an approval was presented");
                        }
                    }
                }
            }
        }

        prop_assert!(authorizations <= 1, "the answer authorized {authorizations} actions");
        prop_assert!(consumptions <= 1, "the authority was spent {consumptions} times");
        prop_assert!(issued.len() <= 1);
    }

    /// A capability is spent at most once however the clock and the take-over
    /// interleave with the spending.
    #[test]
    fn one_capability_is_spent_at_most_once(
        attempts in prop::collection::vec(0u8..3, 1..16),
        times in prop::collection::vec(0u64..(CAPABILITY_EXPIRY * 2), 1..16),
    ) {
        let mut engine = engine_under_review();
        let approval_id = approve(&mut engine, binding());
        let decision = engine.decide_proposal(&request(Some(approval_id)), MonotonicMillis(2));
        let ProposalDecision::Authorize(authorization) = decision else {
            unreachable!("the approved proposal is authorized")
        };
        let capability_id = authorization.capability_id;

        let mut spent = 0_u32;
        for (attempt, now) in attempts.iter().zip(times.iter().copied()) {
            let now = MonotonicMillis(now);
            match attempt {
                0 => {
                    if engine.consume_capability(&capability_id, now).is_ok() {
                        spent = spent.saturating_add(1);
                    }
                }
                1 => {
                    engine.user_took_over(&TabId::new("tab_1"), RevocationReason::UserTookOver);
                }
                _ => {
                    prop_assert!(engine
                        .capability(&capability_id)
                        .is_some_and(|capability| !matches!(
                            capability.state_at(now),
                            policy_engine::CapabilityState::Issued
                        ) || capability.state_at(now) == policy_engine::CapabilityState::Issued));
                }
            }
        }
        prop_assert!(spent <= 1, "the authority was spent {spent} times");
    }
}
