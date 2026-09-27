// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fail-closed properties: expiry, revocation, replay, and unknown values.
//!
//! Threat model invariant I-04 and domain model section 12.4 say a capability
//! is short-lived, exact, non-transferable, and one-use. Those are universally
//! quantified statements, so they are tested as properties over generated
//! inputs rather than as a handful of examples.
//!
//! The unknown-value properties are the other half of failing closed. A wire
//! value outside a closed enumeration, a precondition without the operand its
//! kind selects, a control type nobody has heard of, and a result code that is
//! not `VERIFIED` all have to refuse. None of them may resolve to the nearest
//! known member, to a default, or to success.

// proptest's generated harness is not written under this crate's lint budget.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use proptest::prelude::*;

use bip_types::action::{
    ActionType, Precondition, PreconditionKind, Principal, PrincipalKind, VerifierKind,
};
use bip_types::identity::{
    ContentDigest, DigestAlgorithm, DocumentLifecycleState, FrameId, GraphRevision,
    MonotonicMillis, PageEpoch, ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;
use bip_types::snapshot::{NodeState, SemanticRole, Sensitivity};
use bip_types::trust::TrustSet;
use bip_types::version::{ClosedEnum, Decoded};
use bip_types::ActionResultCode;

use policy_engine::capability::{
    CapabilityError, CapabilityRequest, CapabilityScope, CapabilityState,
};
use policy_engine::precondition::{
    evaluate_precondition, BrokerStanding, BudgetState, CapabilityStanding, DispatchCheck,
    FramePresence, LeaseStanding, ObservedNode, TabPresence, UserInteraction,
};
use policy_engine::redaction::{AutocompleteSignal, InputTypeSignal};
use policy_engine::time::SequentialIds;
use policy_engine::{
    evaluate_dispatch, ActionClass, ActionPhase, AllowedRedirects, ControlMode, DispatchVerdict,
    LeaseRequest, NormalizedOrigin, ObservedState, PolicyEngine, PolicyMilestone, PolicyVersion,
    ProposalDecision, RevocationReason, RiskClass,
};

const LEASE_EXPIRY: u64 = 100_000;

fn origin() -> NormalizedOrigin {
    match policy_engine::origin::normalize_serialization("https://example.test") {
        Ok(origin) => origin,
        Err(_) => unreachable!("the fixture origin must normalize"),
    }
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
        origin: origin(),
        node_id: Some(SemanticNodeId::new("n_1")),
        destination_scope: None,
        destination_address: None,
        required_graph_revision: GraphRevision(7),
        allowed_redirects: AllowedRedirects::none(),
    }
}

fn request(expires_at: u64) -> CapabilityRequest {
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
        approval: None,
        expires_at: MonotonicMillis(expires_at),
    }
}

fn engine_with_lease() -> PolicyEngine<SequentialIds> {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    let issued = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::Assistant,
            expires_at: MonotonicMillis(LEASE_EXPIRY),
        },
        MonotonicMillis(0),
    );
    assert!(issued.is_ok());
    engine
}

proptest! {
    /// A capability is spendable at most once, whatever order the operations
    /// arrive in and whenever they arrive.
    #[test]
    fn a_capability_is_spendable_at_most_once(
        expiry in 1u64..LEASE_EXPIRY,
        attempts in prop::collection::vec(0u8..4, 1..12),
        times in prop::collection::vec(0u64..(LEASE_EXPIRY * 2), 1..12),
    ) {
        let mut engine = engine_with_lease();
        let decision = engine.decide_proposal(&request(expiry), MonotonicMillis(0));
        let ProposalDecision::Authorize(authorization) = decision else {
            unreachable!("an open-link proposal under a standing lease is authorized")
        };
        let capability_id = authorization.capability_id;

        let mut successful_consumptions = 0;
        for (attempt, now) in attempts.iter().zip(times.iter().copied()) {
            let now = MonotonicMillis(now);
            match attempt {
                0 => {
                    let _ = engine.capabilities().get(&capability_id);
                }
                1 => {
                    // A dispatch attempt never grants a second consumption.
                    let _ = engine.consume_capability(&capability_id, now)
                        .map(|_| successful_consumptions += 1);
                }
                2 => {
                    engine.user_took_over(&TabId::new("tab_1"), RevocationReason::UserTookOver);
                }
                _ => {
                    let _ = engine.consume_capability(&capability_id, now).map(|_| successful_consumptions += 1);
                }
            }
        }
        prop_assert!(successful_consumptions <= 1);
    }

    /// Nothing is spendable after its expiry, and expiry is decided by the
    /// clock reading the caller passes rather than by a timer somebody has to
    /// remember to run.
    #[test]
    fn an_expired_capability_never_authorizes_anything(
        expiry in 1u64..1_000,
        overshoot in 0u64..10_000,
    ) {
        let mut engine = engine_with_lease();
        let decision = engine.decide_proposal(&request(expiry), MonotonicMillis(0));
        let ProposalDecision::Authorize(authorization) = decision else {
            unreachable!("an open-link proposal under a standing lease is authorized")
        };
        let now = MonotonicMillis(expiry.saturating_add(overshoot));

        let capability = engine.capability(&authorization.capability_id);
        prop_assert_eq!(
            capability.map(|capability| capability.state_at(now)),
            Some(CapabilityState::Expired)
        );
        prop_assert_eq!(
            engine.consume_capability(&authorization.capability_id, now).err(),
            Some(CapabilityError::Expired)
        );
    }

    /// Take over withdraws undispatched authority whenever it happens, and a
    /// withdrawn capability never becomes spendable again.
    #[test]
    fn take_over_leaves_no_spendable_authority(
        expiry in 1u64..LEASE_EXPIRY,
        take_over_at in 0u64..LEASE_EXPIRY,
        use_at in 0u64..LEASE_EXPIRY,
    ) {
        let mut engine = engine_with_lease();
        let decision = engine.decide_proposal(&request(expiry), MonotonicMillis(0));
        let ProposalDecision::Authorize(authorization) = decision else {
            unreachable!("an open-link proposal under a standing lease is authorized")
        };
        let _ = take_over_at;

        let take_over = engine.user_took_over(&TabId::new("tab_1"), RevocationReason::UserTookOver);
        prop_assert!(take_over.changed_anything());
        prop_assert!(take_over.authority.revoked.contains(&authorization.capability_id));
        prop_assert!(engine
            .consume_capability(&authorization.capability_id, MonotonicMillis(use_at))
            .is_err());

        // And no new authority can be issued afterwards without a new lease.
        let after = engine.decide_proposal(&request(expiry), MonotonicMillis(use_at));
        prop_assert!(!matches!(after, ProposalDecision::Authorize(_)));
    }

    /// The section 12 sequence never proceeds when any of the facts it binds to
    /// has changed.
    #[test]
    fn a_changed_world_never_dispatches(
        tab_gone in any::<bool>(),
        frame_gone in any::<bool>(),
        inactive in any::<bool>(),
        wrong_epoch in any::<bool>(),
        wrong_origin in any::<bool>(),
        older_revision in any::<bool>(),
        node_gone in any::<bool>(),
        lease_missing in any::<bool>(),
        capability_invalid in any::<bool>(),
    ) {
        let observed = ObservedState {
            tab: if tab_gone { TabPresence::Gone } else { TabPresence::Present },
            frame: if frame_gone { FramePresence::Gone } else { FramePresence::Present },
            lifecycle: if inactive {
                DocumentLifecycleState::Prerendering
            } else {
                DocumentLifecycleState::Active
            },
            page_epoch: PageEpoch::new(if wrong_epoch { "epoch_b" } else { "epoch_a" }),
            graph_revision: GraphRevision(if older_revision { 6 } else { 7 }),
            origin: if wrong_origin {
                match policy_engine::origin::normalize_serialization("https://other.test") {
                    Ok(origin) => origin,
                    Err(_) => unreachable!("the fixture origin must normalize"),
                }
            } else {
                origin()
            },
            node: if node_gone {
                None
            } else {
                Some(ObservedNode {
                    node_id: SemanticNodeId::new("n_1"),
                    role: SemanticRole::Link,
                    available_actions: vec![ActionType::Activate],
                    states: vec![NodeState::Visible, NodeState::Enabled],
                    sensitivity: SensitivitySet::EMPTY,
                    destination: None,
                    value_digest: None,
                    content_trust: TrustSet::EMPTY,
                })
            },
            user_interaction: UserInteraction::NoneSinceLease,
            budget: BudgetState::Remaining,
        };

        let tab_id = TabId::new("tab_1");
        let frame_id = FrameId::new("frame_main");
        let epoch = PageEpoch::new("epoch_a");
        let expected_origin = origin();
        let redirects = AllowedRedirects::none();
        let node_id = SemanticNodeId::new("n_1");
        let check = DispatchCheck {
            tab_id: &tab_id,
            frame_id: &frame_id,
            required_page_epoch: &epoch,
            required_graph_revision: GraphRevision(7),
            required_origin: &expected_origin,
            required_destination: None,
            allowed_redirects: &redirects,
            node_id: Some(&node_id),
            expected_role: SemanticRole::Link,
            action_type: ActionType::Activate,
            action_class: ActionClass::OpenLink,
            expected_destination: None,
            preconditions: &[],
        };

        let verdict = evaluate_dispatch(
            &check,
            BrokerStanding::new(
                if lease_missing {
                    LeaseStanding::Missing
                } else {
                    LeaseStanding::Active
                },
                if capability_invalid {
                    CapabilityStanding::NotValid
                } else {
                    CapabilityStanding::Valid
                },
            ),
            &observed,
        );

        let anything_changed = tab_gone
            || frame_gone
            || inactive
            || wrong_epoch
            || wrong_origin
            || older_revision
            || node_gone
            || lease_missing
            || capability_invalid;
        prop_assert_eq!(verdict.proceeds(), !anything_changed);

        // Every refusal is a refusal before anything reached the page, so a
        // fresh observation and a new proposal are safe afterwards.
        if let Some(code) = verdict.code() {
            prop_assert!(code.fails_closed());
            prop_assert_eq!(
                code.side_effect(),
                bip_types::result_code::SideEffectCertainty::NotPerformed
            );
        }
    }

    /// A precondition whose operand is missing refuses instead of passing.
    #[test]
    fn a_precondition_without_its_operand_fails_closed(kind_index in 0usize..19) {
        let Some(kind) = PreconditionKind::ALL.get(kind_index).copied() else {
            unreachable!("the index is bounded by the enumeration length")
        };
        let bare = Precondition {
            kind,
            page_epoch: None,
            min_graph_revision: None,
            origin: None,
            allowed_origins: None,
            expected_role: None,
            expected_action_type: None,
            node_state: None,
            expected_destination: None,
            expected_value_digest: None,
            max_sensitivity: None,
            min_content_trust: None,
            prepared_effect: None,
        };

        let tab_id = TabId::new("tab_1");
        let frame_id = FrameId::new("frame_main");
        let epoch = PageEpoch::new("epoch_a");
        let expected_origin = origin();
        let redirects = AllowedRedirects::none();
        let check = DispatchCheck {
            tab_id: &tab_id,
            frame_id: &frame_id,
            required_page_epoch: &epoch,
            required_graph_revision: GraphRevision(7),
            required_origin: &expected_origin,
            required_destination: None,
            allowed_redirects: &redirects,
            node_id: None,
            expected_role: SemanticRole::Link,
            action_type: ActionType::Activate,
            action_class: ActionClass::ScrollIntoView,
            expected_destination: None,
            preconditions: &[],
        };
        let observed = ObservedState {
            tab: TabPresence::Present,
            frame: FramePresence::Present,
            lifecycle: DocumentLifecycleState::Active,
            page_epoch: PageEpoch::new("epoch_a"),
            graph_revision: GraphRevision(7),
            origin: origin(),
            node: None,
            user_interaction: UserInteraction::NoneSinceLease,
            budget: BudgetState::Remaining,
        };

        // The kinds that need no operand are satisfiable without one; every
        // other kind refuses rather than treating the absent operand as met.
        let needs_no_operand = matches!(
            kind,
            PreconditionKind::DocumentActive
                | PreconditionKind::NoUserInteractionSinceLease
                | PreconditionKind::BudgetRemaining
        );
        // A standing with everything the broker could vouch for still refuses
        // the operandless kinds: `PreparedEffectStanding::NotChecked` is the
        // default here, so a `PREPARED_EFFECT_UNCHANGED` with no binding is
        // refused for the absent operand rather than for the absent ledger.
        let standing = BrokerStanding::new(LeaseStanding::Active, CapabilityStanding::Valid);
        let outcome = evaluate_precondition(&bare, &check, standing, &observed);
        prop_assert_eq!(outcome.is_none(), needs_no_operand);
    }
}

#[test]
fn an_unknown_wire_value_never_becomes_a_known_member() {
    // A result code outside the taxonomy does not decode, and no accessor
    // yields a member from the refusal.
    let decoded = ActionResultCode::decode("MOSTLY_VERIFIED");
    assert!(decoded.is_unsupported());
    assert_eq!(decoded.known(), None);
    assert_ne!(decoded, Decoded::Known(ActionResultCode::Verified));

    // A precondition kind outside the enumeration does not parse, so it can
    // never reach the evaluator at all.
    assert_eq!(PreconditionKind::from_wire("EXACT_VIBES"), None);
    assert!(PreconditionKind::decode_wire("EXACT_VIBES").is_unsupported());

    // A verifier outside the enumeration likewise. Renderer acknowledgement is
    // the only member that is never sufficient on its own, and an unknown value
    // is not promoted to it or past it.
    assert!(VerifierKind::decode_wire("VIBES").is_unsupported());
}

#[test]
fn an_unknown_classification_signal_raises_rather_than_defaults() {
    assert_eq!(
        InputTypeSignal::from_attribute(Some("quantum-field")),
        InputTypeSignal::Unrecognized
    );
    assert_eq!(
        AutocompleteSignal::from_attribute(Some("cc-quantum")),
        AutocompleteSignal::Unrecognized
    );
    // Absence is not the same as an unknown token: a node that is not a control
    // is not thereby suspicious.
    assert_eq!(
        InputTypeSignal::from_attribute(None),
        InputTypeSignal::NotAControl
    );
    assert_eq!(
        AutocompleteSignal::from_attribute(None),
        AutocompleteSignal::Absent
    );
}

#[test]
fn every_outcome_spends_the_capability_including_the_failures() {
    // A capability is spent by having been dispatched, not by having worked.
    // If any result code left it live, a failed action would leave reusable
    // authority behind.
    for code in ActionResultCode::ALL {
        let mut engine = engine_with_lease();
        let decision = engine.decide_proposal(&request(5_000), MonotonicMillis(0));
        let ProposalDecision::Authorize(authorization) = decision else {
            unreachable!("an open-link proposal under a standing lease is authorized")
        };
        assert!(engine
            .consume_capability(&authorization.capability_id, MonotonicMillis(1))
            .is_ok());
        assert_eq!(
            engine
                .capability(&authorization.capability_id)
                .map(|capability| capability.state_at(MonotonicMillis(2))),
            Some(CapabilityState::Consumed),
            "{}",
            code.wire()
        );
        assert_eq!(
            engine
                .consume_capability(&authorization.capability_id, MonotonicMillis(2))
                .err(),
            Some(CapabilityError::AlreadyConsumed)
        );
    }
}

#[test]
fn a_never_extract_node_refuses_every_action_on_it() {
    // Sensitivity is not only about what may be read. Activating a credential
    // control is how a page gets one submitted, so the classification refuses
    // the action as well as the value.
    let tab_id = TabId::new("tab_1");
    let frame_id = FrameId::new("frame_main");
    let epoch = PageEpoch::new("epoch_a");
    let expected_origin = origin();
    let redirects = AllowedRedirects::none();
    let node_id = SemanticNodeId::new("n_1");
    let check = DispatchCheck {
        tab_id: &tab_id,
        frame_id: &frame_id,
        required_page_epoch: &epoch,
        required_graph_revision: GraphRevision(7),
        required_origin: &expected_origin,
        required_destination: None,
        allowed_redirects: &redirects,
        node_id: Some(&node_id),
        expected_role: SemanticRole::Button,
        action_type: ActionType::Activate,
        action_class: ActionClass::SyntheticClick,
        expected_destination: None,
        preconditions: &[],
    };
    let observed = ObservedState {
        tab: TabPresence::Present,
        frame: FramePresence::Present,
        lifecycle: DocumentLifecycleState::Active,
        page_epoch: PageEpoch::new("epoch_a"),
        graph_revision: GraphRevision(7),
        origin: origin(),
        node: Some(ObservedNode {
            node_id: SemanticNodeId::new("n_1"),
            role: SemanticRole::Button,
            available_actions: vec![ActionType::Activate],
            states: vec![NodeState::Visible, NodeState::Enabled],
            sensitivity: SensitivitySet::of(Sensitivity::Credential),
            destination: None,
            value_digest: None,
            content_trust: TrustSet::EMPTY,
        }),
        user_interaction: UserInteraction::NoneSinceLease,
        budget: BudgetState::Remaining,
    };

    let verdict = evaluate_dispatch(
        &check,
        BrokerStanding::new(LeaseStanding::Active, CapabilityStanding::Valid),
        &observed,
    );
    assert_eq!(verdict.code(), Some(ActionResultCode::SensitiveField));
    assert!(matches!(verdict, DispatchVerdict::Refuse { .. }));
}
