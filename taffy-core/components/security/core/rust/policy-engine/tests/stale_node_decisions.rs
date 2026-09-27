// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Steps one to six of the section 12 sequence: the decisions over values.
//!
//! Every one of these steps is a comparison between what the capability bound
//! and what the browser observes right now, so the whole half is a table:
//! `evaluate_dispatch` is pure, and each test names the step it is about and
//! the refusal it proves.
//!
//! The order is the property. A stale epoch must be reported as a stale epoch
//! rather than as a missing node, because the two lead the task runtime to
//! different recoveries — so each test mutates exactly one thing and asserts
//! the step that catches it.
//!
//! Steps seven to ten are outcomes of effects and live in
//! `stale_node_effects.rs`.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::{ActionType, Precondition, PreconditionKind};
use bip_types::identity::{
    DocumentLifecycleState, FrameId, GraphRevision, PageEpoch, SemanticNodeId, TabId,
};
use bip_types::result_code::SideEffectCertainty;
use bip_types::sensitivity::{Sensitivity, SensitivitySet};
use bip_types::snapshot::{NodeState, SemanticRole};
use bip_types::trust::TrustSet;
use bip_types::ActionResultCode;

use policy_engine::precondition::{
    BrokerStanding, BudgetState, CapabilityStanding, DispatchCheck, FramePresence, LeaseStanding,
    NormalizedDestination, ObservedNode, TabPresence, UserInteraction,
};
use policy_engine::{
    evaluate_dispatch, ActionClass, AllowedRedirects, DispatchVerdict, NormalizedOrigin,
    ObservedState, StaleNodeStep,
};

// ---------------------------------------------------------------------------
// Fixtures: one authorized link activation, and the world it was authorized in.
// ---------------------------------------------------------------------------

fn origin(serialization: &str) -> NormalizedOrigin {
    policy_engine::origin::normalize_serialization(serialization)
        .expect("the fixture origin must normalize")
}

fn destination() -> NormalizedDestination {
    NormalizedDestination {
        origin: origin("https://example.test"),
        path: Some("/orders/42".to_owned()),
        opens_new_tab: false,
        is_download: false,
    }
}

fn observed() -> ObservedState {
    ObservedState {
        tab: TabPresence::Present,
        frame: FramePresence::Present,
        lifecycle: DocumentLifecycleState::Active,
        page_epoch: PageEpoch::new("epoch_a"),
        graph_revision: GraphRevision(9),
        origin: origin("https://example.test"),
        node: Some(ObservedNode {
            node_id: SemanticNodeId::new("n_link"),
            role: SemanticRole::Link,
            available_actions: vec![ActionType::Activate, ActionType::ScrollIntoView],
            states: vec![NodeState::Visible, NodeState::Enabled],
            sensitivity: SensitivitySet::EMPTY,
            destination: Some(destination()),
            value_digest: None,
            content_trust: TrustSet::EMPTY,
        }),
        user_interaction: UserInteraction::NoneSinceLease,
        budget: BudgetState::Remaining,
    }
}

/// Everything a `DispatchCheck` borrows, kept alive for the call.
struct Authorized {
    tab_id: TabId,
    frame_id: FrameId,
    page_epoch: PageEpoch,
    origin: NormalizedOrigin,
    destination: NormalizedOrigin,
    redirects: AllowedRedirects,
    node_id: SemanticNodeId,
    action_class: ActionClass,
    action_type: ActionType,
    expected_role: SemanticRole,
    expected_destination: Option<NormalizedDestination>,
    node_targeted: bool,
    destination_scoped: bool,
    preconditions: Vec<Precondition>,
}

impl Default for Authorized {
    fn default() -> Self {
        Self {
            tab_id: TabId::new("tab_1"),
            frame_id: FrameId::new("frame_main"),
            page_epoch: PageEpoch::new("epoch_a"),
            origin: origin("https://example.test"),
            destination: origin("https://example.test"),
            redirects: AllowedRedirects::from_normalized([origin("https://example.test")]),
            node_id: SemanticNodeId::new("n_link"),
            action_class: ActionClass::OpenLink,
            action_type: ActionType::Activate,
            expected_role: SemanticRole::Link,
            expected_destination: Some(destination()),
            node_targeted: true,
            destination_scoped: true,
            preconditions: Vec::new(),
        }
    }
}

impl Authorized {
    fn check(&self) -> DispatchCheck<'_> {
        DispatchCheck {
            tab_id: &self.tab_id,
            frame_id: &self.frame_id,
            required_page_epoch: &self.page_epoch,
            required_graph_revision: GraphRevision(7),
            required_origin: &self.origin,
            required_destination: self.destination_scoped.then_some(&self.destination),
            allowed_redirects: &self.redirects,
            node_id: self.node_targeted.then_some(&self.node_id),
            expected_role: self.expected_role,
            action_type: self.action_type,
            action_class: self.action_class,
            expected_destination: self.expected_destination.as_ref(),
            preconditions: &self.preconditions,
        }
    }
}

/// Runs steps one to six against a moved world and returns the verdict.
fn verdict_after(mutate: impl FnOnce(&mut Authorized, &mut ObservedState)) -> DispatchVerdict {
    let mut authorized = Authorized::default();
    let mut state = observed();
    mutate(&mut authorized, &mut state);
    evaluate_dispatch(
        &authorized.check(),
        BrokerStanding::new(LeaseStanding::Active, CapabilityStanding::Valid),
        &state,
    )
}

/// Asserts a refusal at one step with one code, and that nothing reached the
/// page.
#[track_caller]
fn refuses(
    mutate: impl FnOnce(&mut Authorized, &mut ObservedState),
    step: StaleNodeStep,
    code: ActionResultCode,
) {
    let verdict = verdict_after(mutate);
    assert_eq!(verdict, DispatchVerdict::refuse(step, code));
    assert!(step.is_decision());
    assert_eq!(code.side_effect(), SideEffectCertainty::NotPerformed);
    assert!(code.fails_closed());
}

// ---------------------------------------------------------------------------
// Step 1 — resolve the tab and frame.
// ---------------------------------------------------------------------------

#[test]
fn step_one_a_closed_tab_refuses_with_tab_gone() {
    refuses(
        |_, state| state.tab = TabPresence::Gone,
        StaleNodeStep::ResolveTabAndFrame,
        ActionResultCode::TabGone,
    );
}

#[test]
fn step_one_a_detached_frame_refuses_with_frame_gone() {
    refuses(
        |_, state| state.frame = FramePresence::Gone,
        StaleNodeStep::ResolveTabAndFrame,
        ActionResultCode::FrameGone,
    );
}

#[test]
fn step_one_runs_before_step_two_so_a_closed_tab_is_not_reported_as_a_stale_epoch() {
    refuses(
        |_, state| {
            state.tab = TabPresence::Gone;
            state.page_epoch = PageEpoch::new("epoch_b");
        },
        StaleNodeStep::ResolveTabAndFrame,
        ActionResultCode::TabGone,
    );
}

// ---------------------------------------------------------------------------
// Step 2 — an active document, then the exact page epoch.
// ---------------------------------------------------------------------------

#[test]
fn step_two_every_inactive_lifecycle_refuses_with_document_inactive() {
    for lifecycle in DocumentLifecycleState::ALL {
        if *lifecycle == DocumentLifecycleState::Active {
            continue;
        }
        refuses(
            |_, state| state.lifecycle = *lifecycle,
            StaleNodeStep::ConfirmDocumentAndEpoch,
            ActionResultCode::DocumentInactive,
        );
    }
}

#[test]
fn step_two_a_new_document_refuses_with_stale_page_epoch() {
    refuses(
        |_, state| state.page_epoch = PageEpoch::new("epoch_b"),
        StaleNodeStep::ConfirmDocumentAndEpoch,
        ActionResultCode::StalePageEpoch,
    );
}

#[test]
fn step_two_checks_the_lifecycle_before_the_epoch() {
    // A prerendering document is not a document this action may reach, whatever
    // epoch it carries, so the lifecycle refusal wins.
    refuses(
        |_, state| {
            state.lifecycle = DocumentLifecycleState::Prerendering;
            state.page_epoch = PageEpoch::new("epoch_b");
        },
        StaleNodeStep::ConfirmDocumentAndEpoch,
        ActionResultCode::DocumentInactive,
    );
}

// ---------------------------------------------------------------------------
// Step 3 — the origin.
// ---------------------------------------------------------------------------

#[test]
fn step_three_a_different_origin_refuses_with_origin_changed() {
    refuses(
        |_, state| state.origin = origin("https://partner.test"),
        StaleNodeStep::ConfirmOrigin,
        ActionResultCode::OriginChanged,
    );
}

#[test]
fn step_three_a_same_site_origin_is_still_a_different_origin() {
    refuses(
        |_, state| state.origin = origin("https://sub.example.test"),
        StaleNodeStep::ConfirmOrigin,
        ActionResultCode::OriginChanged,
    );
}

// ---------------------------------------------------------------------------
// Step 4 — the actor lease and the capability.
// ---------------------------------------------------------------------------

#[test]
fn step_four_a_missing_lease_refuses_with_actor_lease_missing() {
    let authorized = Authorized::default();
    let verdict = evaluate_dispatch(
        &authorized.check(),
        BrokerStanding::new(LeaseStanding::Missing, CapabilityStanding::Valid),
        &observed(),
    );
    assert_eq!(
        verdict,
        DispatchVerdict::refuse(
            StaleNodeStep::ConfirmLeaseAndCapability,
            ActionResultCode::ActorLeaseMissing
        )
    );
}

#[test]
fn step_four_authority_that_no_longer_stands_refuses_with_capability_expired() {
    let authorized = Authorized::default();
    let verdict = evaluate_dispatch(
        &authorized.check(),
        BrokerStanding::new(LeaseStanding::Active, CapabilityStanding::NotValid),
        &observed(),
    );
    assert_eq!(
        verdict,
        DispatchVerdict::refuse(
            StaleNodeStep::ConfirmLeaseAndCapability,
            ActionResultCode::CapabilityExpired
        )
    );
}

#[test]
fn step_four_checks_the_lease_before_the_capability() {
    let authorized = Authorized::default();
    let verdict = evaluate_dispatch(
        &authorized.check(),
        BrokerStanding::new(LeaseStanding::Missing, CapabilityStanding::NotValid),
        &observed(),
    );
    assert_eq!(
        verdict.code(),
        Some(ActionResultCode::ActorLeaseMissing),
        "an action with no actor is not a capability problem"
    );
}

// ---------------------------------------------------------------------------
// Step 5 — resolve the node at the required graph revision.
// ---------------------------------------------------------------------------

#[test]
fn step_five_a_node_that_no_longer_resolves_refuses_with_node_gone() {
    refuses(
        |_, state| state.node = None,
        StaleNodeStep::ResolveNode,
        ActionResultCode::NodeGone,
    );
}

#[test]
fn step_five_a_different_node_identifier_refuses_with_node_gone() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.node_id = SemanticNodeId::new("n_other");
            }
        },
        StaleNodeStep::ResolveNode,
        ActionResultCode::NodeGone,
    );
}

#[test]
fn step_five_an_older_graph_revision_refuses_with_stale_graph() {
    refuses(
        |_, state| state.graph_revision = GraphRevision(6),
        StaleNodeStep::ResolveNode,
        ActionResultCode::StaleGraph,
    );
}

#[test]
fn step_five_a_document_level_action_resolves_no_node_at_all() {
    let verdict = verdict_after(|authorized, state| {
        authorized.node_targeted = false;
        authorized.action_class = ActionClass::ScrollIntoView;
        authorized.expected_destination = None;
        authorized.destination_scoped = false;
        state.node = None;
    });
    assert_eq!(verdict, DispatchVerdict::Proceed);
}

// ---------------------------------------------------------------------------
// Step 6 — re-evaluate everything a live document can change.
// ---------------------------------------------------------------------------

#[test]
fn step_six_a_changed_role_refuses_with_role_or_action_changed() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.role = SemanticRole::Button;
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::RoleOrActionChanged,
    );
}

#[test]
fn step_six_an_action_the_adapter_withdrew_refuses_with_role_or_action_changed() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.available_actions = vec![ActionType::ScrollIntoView];
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::RoleOrActionChanged,
    );
}

#[test]
fn step_six_a_never_extract_classification_refuses_with_sensitive_field() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.sensitivity = SensitivitySet::of(Sensitivity::Credential);
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::SensitiveField,
    );
}

#[test]
fn step_six_an_unasserted_visibility_refuses_with_not_visible() {
    // Absence of VISIBLE is not an assertion of NOT_VISIBLE, and an action
    // policy needs the positive assertion, so absence refuses.
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.states = vec![NodeState::Enabled];
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::NotVisible,
    );
}

#[test]
fn step_six_an_obscured_node_refuses_with_occluded() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.states = vec![NodeState::Visible, NodeState::Enabled, NodeState::Obscured];
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::Occluded,
    );
}

#[test]
fn step_six_a_disabled_node_refuses_with_not_enabled() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.states = vec![NodeState::Visible, NodeState::Disabled];
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::NotEnabled,
    );
}

#[test]
fn step_six_an_unasserted_enabled_state_refuses_with_not_enabled() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.states = vec![NodeState::Visible];
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::NotEnabled,
    );
}

#[test]
fn step_six_a_class_that_touches_nothing_skips_the_interaction_checks() {
    // Observing and scrolling do not put a synthetic interaction on the node,
    // so an invisible node does not refuse them.
    let verdict = verdict_after(|authorized, state| {
        authorized.action_class = ActionClass::ScrollIntoView;
        authorized.action_type = ActionType::ScrollIntoView;
        authorized.expected_destination = None;
        authorized.destination_scoped = false;
        if let Some(node) = state.node.as_mut() {
            node.states = Vec::new();
            node.destination = None;
        }
    });
    assert_eq!(verdict, DispatchVerdict::Proceed);
}

#[test]
fn step_six_a_changed_destination_refuses_with_destination_changed() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.destination = Some(NormalizedDestination {
                    origin: origin("https://example.test"),
                    path: Some("/somewhere-else".to_owned()),
                    opens_new_tab: false,
                    is_download: false,
                });
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::DestinationChanged,
    );
}

#[test]
fn step_six_a_node_that_lost_its_destination_refuses_with_destination_changed() {
    refuses(
        |_, state| {
            if let Some(node) = state.node.as_mut() {
                node.destination = None;
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::DestinationChanged,
    );
}

#[test]
fn step_six_a_destination_outside_the_capability_scope_refuses_with_destination_changed() {
    // The proposal and the node agree; the capability authorized somewhere
    // else. The authorization is what decides.
    refuses(
        |authorized, state| {
            let elsewhere = NormalizedDestination {
                origin: origin("https://partner.test"),
                path: Some("/orders/42".to_owned()),
                opens_new_tab: false,
                is_download: false,
            };
            authorized.expected_destination = Some(elsewhere.clone());
            authorized.redirects =
                AllowedRedirects::from_normalized([origin("https://partner.test")]);
            if let Some(node) = state.node.as_mut() {
                node.destination = Some(elsewhere);
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::DestinationChanged,
    );
}

#[test]
fn step_six_a_destination_no_redirect_policy_permits_refuses_with_denied_by_policy() {
    refuses(
        |authorized, state| {
            let elsewhere = NormalizedDestination {
                origin: origin("https://partner.test"),
                path: None,
                opens_new_tab: false,
                is_download: false,
            };
            authorized.expected_destination = Some(elsewhere.clone());
            authorized.destination = origin("https://partner.test");
            authorized.redirects = AllowedRedirects::none();
            if let Some(node) = state.node.as_mut() {
                node.destination = Some(elsewhere);
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::DeniedByPolicy,
    );
}

#[test]
fn step_six_an_exhausted_budget_refuses_with_budget_exceeded() {
    refuses(
        |_, state| state.budget = BudgetState::Exhausted,
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::BudgetExceeded,
    );
}

#[test]
fn step_six_a_declared_precondition_that_does_not_hold_refuses_with_its_own_code() {
    refuses(
        |authorized, state| {
            authorized.preconditions = vec![Precondition {
                kind: PreconditionKind::NoUserInteractionSinceLease,
                page_epoch: None,
                min_graph_revision: None,
                origin: None,
                allowed_origins: None,
                expected_role: None,
                expected_action_type: None,
                node_state: None,
                expected_value_digest: None,
                expected_destination: None,
                max_sensitivity: None,
                min_content_trust: None,
                prepared_effect: None,
            }];
            state.user_interaction = UserInteraction::ObservedSinceLease;
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::CancelledByUser,
    );
}

#[test]
fn step_six_the_node_is_re_evaluated_before_the_budget_is_consulted() {
    refuses(
        |_, state| {
            state.budget = BudgetState::Exhausted;
            if let Some(node) = state.node.as_mut() {
                node.role = SemanticRole::Button;
            }
        },
        StaleNodeStep::ReevaluateNode,
        ActionResultCode::RoleOrActionChanged,
    );
}

#[test]
fn steps_one_to_six_proceed_when_the_world_did_not_move() {
    assert_eq!(verdict_after(|_, _| {}), DispatchVerdict::Proceed);
}
