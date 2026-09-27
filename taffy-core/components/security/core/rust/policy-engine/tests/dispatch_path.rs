// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The whole path, once: lease, proposal, dispatch check, dispatch, outcome.
//!
//! Protocol specification section 12 runs ten steps. This suite walks all ten
//! against one document through the broker's own interface, then moves the
//! document underneath the first six and asserts that each check refuses with
//! the code the specification names.
//!
//! The pure form of the same sequence, branch by branch, is
//! `tests/stale_node_sequence.rs`. This file is about the broker: that the
//! ticket it hands back is one-use, that a refusal leaves the authority
//! unspent, and that a take-over between authorization and dispatch stops the
//! dispatch.

use bip_types::action::{ActionType, Principal, PrincipalKind};
use bip_types::identity::{
    ContentDigest, DigestAlgorithm, DocumentLifecycleState, FrameId, GraphRevision,
    MonotonicMillis, PageEpoch, ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;
use bip_types::snapshot::{NodeState, SemanticRole};
use bip_types::trust::TrustSet;
use bip_types::ActionResultCode;

use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::precondition::{
    BudgetState, FramePresence, NormalizedDestination, ObservedNode, TabPresence, UserInteraction,
};
use policy_engine::report::{DispatchAck, JournalOutcome, PostconditionReport};
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionPhase, AllowedRedirects, ConsumptionOutcome, ControlMode, DispatchDecision,
    DispatchProposal, Dispatched, Journalled, LeaseRequest, NormalizedOrigin, Observation,
    ObservedState, PolicyEngine, PolicyMilestone, PolicyVersion, ProposalDecision,
    RevocationReason, RiskClass, StaleNodeStep,
};

fn origin(serialization: &str) -> NormalizedOrigin {
    match policy_engine::origin::normalize_serialization(serialization) {
        Ok(origin) => origin,
        Err(_) => unreachable!("the fixture origin must normalize"),
    }
}

fn engine() -> PolicyEngine<SequentialIds> {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    let issued = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::Assistant,
            expires_at: MonotonicMillis(10_000),
        },
        MonotonicMillis(0),
    );
    assert!(issued.is_ok());
    engine
}

fn request() -> CapabilityRequest {
    CapabilityRequest {
        task_id: TaskId::new("task_1"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: ActionClass::OpenLink,
        phase: ActionPhase::Commit,
        action_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a1".to_owned(),
        },
        scope: CapabilityScope {
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
        },
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval: None,
        expires_at: MonotonicMillis(5_000),
    }
}

fn destination() -> NormalizedDestination {
    NormalizedDestination {
        origin: origin("https://example.test"),
        path: Some("/orders/42".to_owned()),
        opens_new_tab: false,
        is_download: false,
    }
}

fn proposal() -> DispatchProposal {
    DispatchProposal {
        action_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a1".to_owned(),
        },
        node_id: Some(SemanticNodeId::new("n_link")),
        expected_role: SemanticRole::Link,
        action_type: ActionType::Activate,
        expected_destination: Some(destination()),
        preconditions: Vec::new(),
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

#[test]
fn an_ordinary_link_activation_runs_the_whole_path() {
    let mut engine = engine();
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request(), MonotonicMillis(0))
    else {
        unreachable!("an ordinary link activation is authorized")
    };

    let decision = engine.authorize_dispatch(
        &authorization.capability_id,
        &proposal(),
        &observed(),
        MonotonicMillis(1),
    );
    let DispatchDecision::Dispatch(ticket) = decision else {
        unreachable!("the observed world matches the authorized one")
    };
    assert_eq!(ticket.action_class(), ActionClass::OpenLink);
    assert_eq!(ticket.node_id(), Some(&SemanticNodeId::new("n_link")));
    let capability_id = ticket.capability_id().clone();

    // Step 7 — the intent is durable before the effect.
    let Journalled::Recorded(intent) = ticket.journal_intent(JournalOutcome::Recorded) else {
        unreachable!("the journal write succeeded")
    };
    assert!(intent.capability().is_some());

    // Step 8 — the command goes down the normal input path. The broker is
    // released here, so a take-over can run again while step 9 waits.
    let Dispatched::Accepted(dispatched) = intent.dispatch(DispatchAck::AcceptedByExecutor) else {
        unreachable!("the executor accepted the command")
    };

    // Step 9 — a renderer acknowledgement alone is not a verification.
    let Observation::StillWaiting(dispatched) =
        dispatched.observe(PostconditionReport::AcknowledgedOnly)
    else {
        unreachable!("an acknowledgement alone keeps the sequence waiting")
    };
    let Observation::Settled(settled) = dispatched.observe(PostconditionReport::Satisfied) else {
        unreachable!("browser-owned corroboration settles the sequence")
    };
    assert_eq!(settled.settled_code(), Some(ActionResultCode::Verified));
    assert_eq!(settled.decided_at(), StaleNodeStep::ObservePostconditions);

    // Step 10 — the terminal result is recorded and the capability is spent.
    let outcome = settled.record(&mut engine, MonotonicMillis(2));
    assert_eq!(outcome.code, ActionResultCode::Verified);
    assert_eq!(outcome.consumption, ConsumptionOutcome::Spent);
    assert!(outcome.is_verified());
    assert_eq!(outcome.decided_at, StaleNodeStep::RecordTerminalResult);
    assert!(engine
        .consume_capability(&capability_id, MonotonicMillis(3))
        .is_err());
}

#[test]
fn one_ticket_authorizes_one_dispatch_and_a_second_attempt_finds_nothing_to_spend() {
    let mut engine = engine();
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request(), MonotonicMillis(0))
    else {
        unreachable!("an ordinary link activation is authorized")
    };
    let capability_id = authorization.capability_id.clone();

    let decision =
        engine.authorize_dispatch(&capability_id, &proposal(), &observed(), MonotonicMillis(1));
    let DispatchDecision::Dispatch(ticket) = decision else {
        unreachable!("the first dispatch is authorized")
    };
    let Journalled::Recorded(intent) = ticket.journal_intent(JournalOutcome::Recorded) else {
        unreachable!("the journal write succeeded")
    };
    let Dispatched::Accepted(dispatched) = intent.dispatch(DispatchAck::AcceptedByExecutor) else {
        unreachable!("the executor accepted the command")
    };
    let Observation::Settled(settled) = dispatched.observe(PostconditionReport::Satisfied) else {
        unreachable!("the postconditions held")
    };
    assert!(settled
        .record(&mut engine, MonotonicMillis(2))
        .is_verified());

    // The same capability, presented again: the ledger has already spent it.
    let decision =
        engine.authorize_dispatch(&capability_id, &proposal(), &observed(), MonotonicMillis(3));
    assert_eq!(
        decision.result_code(),
        Some(ActionResultCode::CapabilityExpired)
    );
}

#[test]
fn a_ticket_given_back_without_a_send_spends_the_authority_and_touches_nothing() {
    let mut engine = engine();
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request(), MonotonicMillis(0))
    else {
        unreachable!("an ordinary link activation is authorized")
    };
    let capability_id = authorization.capability_id.clone();
    let decision =
        engine.authorize_dispatch(&capability_id, &proposal(), &observed(), MonotonicMillis(1));
    let DispatchDecision::Dispatch(ticket) = decision else {
        unreachable!("the dispatch is authorized")
    };

    let outcome = ticket.abandon(MonotonicMillis(2));
    assert_eq!(outcome.code, ActionResultCode::CancelledByUser);
    assert_eq!(outcome.consumption, ConsumptionOutcome::Spent);
    assert_eq!(
        outcome.code.side_effect(),
        bip_types::result_code::SideEffectCertainty::NotPerformed
    );
}

#[test]
fn a_journal_write_that_fails_sends_nothing_and_ends_as_an_internal_fault() {
    let mut engine = engine();
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request(), MonotonicMillis(0))
    else {
        unreachable!("an ordinary link activation is authorized")
    };
    let decision = engine.authorize_dispatch(
        &authorization.capability_id,
        &proposal(),
        &observed(),
        MonotonicMillis(1),
    );
    let DispatchDecision::Dispatch(ticket) = decision else {
        unreachable!("the dispatch is authorized")
    };
    let Journalled::Settled(settled) = ticket.journal_intent(JournalOutcome::WriteFailed) else {
        unreachable!("a failed journal write sends nothing")
    };
    assert_eq!(
        settled.settled_code(),
        Some(ActionResultCode::InternalError)
    );
    let outcome = settled.record(&mut engine, MonotonicMillis(2));
    assert_eq!(outcome.code, ActionResultCode::InternalError);
    assert_eq!(outcome.refusal_step, Some(StaleNodeStep::JournalIntent));
}

/// One way the world can move between observation and dispatch.
struct StaleCase {
    /// What changed, for the failure message.
    name: &'static str,
    /// Which step of the sequence is expected to notice.
    step: StaleNodeStep,
    /// The code the specification gives that refusal.
    code: ActionResultCode,
    /// How to move the world.
    mutate: fn(&mut ObservedState),
}

/// Every change the sequence has to notice, in the order it notices them.
fn stale_cases() -> Vec<StaleCase> {
    vec![
        StaleCase {
            name: "the tab closed",
            step: StaleNodeStep::ResolveTabAndFrame,
            code: ActionResultCode::TabGone,
            mutate: |state| state.tab = TabPresence::Gone,
        },
        StaleCase {
            name: "the frame detached",
            step: StaleNodeStep::ResolveTabAndFrame,
            code: ActionResultCode::FrameGone,
            mutate: |state| state.frame = FramePresence::Gone,
        },
        StaleCase {
            name: "the document is not active",
            step: StaleNodeStep::ConfirmDocumentAndEpoch,
            code: ActionResultCode::DocumentInactive,
            mutate: |state| state.lifecycle = DocumentLifecycleState::Frozen,
        },
        StaleCase {
            name: "the document was replaced",
            step: StaleNodeStep::ConfirmDocumentAndEpoch,
            code: ActionResultCode::StalePageEpoch,
            mutate: |state| state.page_epoch = PageEpoch::new("epoch_b"),
        },
        StaleCase {
            name: "the origin changed",
            step: StaleNodeStep::ConfirmOrigin,
            code: ActionResultCode::OriginChanged,
            mutate: |state| state.origin = origin("https://other.test"),
        },
        StaleCase {
            name: "the node is gone",
            step: StaleNodeStep::ResolveNode,
            code: ActionResultCode::NodeGone,
            mutate: |state| state.node = None,
        },
        StaleCase {
            name: "the graph is older than the observation",
            step: StaleNodeStep::ResolveNode,
            code: ActionResultCode::StaleGraph,
            mutate: |state| state.graph_revision = GraphRevision(6),
        },
        StaleCase {
            name: "the role changed",
            step: StaleNodeStep::ReevaluateNode,
            code: ActionResultCode::RoleOrActionChanged,
            mutate: |state| {
                if let Some(node) = state.node.as_mut() {
                    node.role = SemanticRole::Button;
                }
            },
        },
        StaleCase {
            name: "the node is no longer visible",
            step: StaleNodeStep::ReevaluateNode,
            code: ActionResultCode::NotVisible,
            mutate: |state| {
                if let Some(node) = state.node.as_mut() {
                    node.states = vec![NodeState::Enabled];
                }
            },
        },
        StaleCase {
            name: "the node is obscured",
            step: StaleNodeStep::ReevaluateNode,
            code: ActionResultCode::Occluded,
            mutate: |state| {
                if let Some(node) = state.node.as_mut() {
                    node.states = vec![NodeState::Visible, NodeState::Enabled, NodeState::Obscured];
                }
            },
        },
        StaleCase {
            name: "the destination changed",
            step: StaleNodeStep::ReevaluateNode,
            code: ActionResultCode::DestinationChanged,
            mutate: |state| {
                if let Some(node) = state.node.as_mut() {
                    node.destination = Some(NormalizedDestination {
                        origin: origin("https://elsewhere.test"),
                        path: None,
                        opens_new_tab: false,
                        is_download: false,
                    });
                }
            },
        },
        StaleCase {
            name: "the budget ran out",
            step: StaleNodeStep::ReevaluateNode,
            code: ActionResultCode::BudgetExceeded,
            mutate: |state| state.budget = BudgetState::Exhausted,
        },
    ]
}

#[test]
fn each_step_of_the_sequence_refuses_with_the_code_the_specification_names() {
    for StaleCase {
        name,
        step,
        code,
        mutate,
    } in stale_cases()
    {
        let mut engine = engine();
        let ProposalDecision::Authorize(authorization) =
            engine.decide_proposal(&request(), MonotonicMillis(0))
        else {
            unreachable!("an ordinary link activation is authorized")
        };
        let mut state = observed();
        mutate(&mut state);

        let decision = engine.authorize_dispatch(
            &authorization.capability_id,
            &proposal(),
            &state,
            MonotonicMillis(1),
        );
        assert_eq!(decision.refusal_step(), Some(step), "case: {name}");
        assert_eq!(decision.result_code(), Some(code), "case: {name}");

        // A refusal leaves the capability unspent and never in flight, so the
        // task runtime may observe again and propose a new action.
        assert!(engine
            .capability(&authorization.capability_id)
            .is_some_and(|capability| capability.is_live_at(MonotonicMillis(2))));
    }
}

#[test]
fn a_capability_from_another_proposal_does_not_authorize_this_one() {
    let mut engine = engine();
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request(), MonotonicMillis(0))
    else {
        unreachable!("an ordinary link activation is authorized")
    };

    let mut replayed = proposal();
    replayed.action_digest = ContentDigest {
        algorithm: DigestAlgorithm::Sha256,
        value: "a2".to_owned(),
    };
    let decision = engine.authorize_dispatch(
        &authorization.capability_id,
        &replayed,
        &observed(),
        MonotonicMillis(1),
    );
    assert_eq!(
        decision.result_code(),
        Some(ActionResultCode::DeniedByPolicy)
    );

    let mut retargeted = proposal();
    retargeted.node_id = Some(SemanticNodeId::new("n_other"));
    let decision = engine.authorize_dispatch(
        &authorization.capability_id,
        &retargeted,
        &observed(),
        MonotonicMillis(1),
    );
    assert_eq!(
        decision.result_code(),
        Some(ActionResultCode::DeniedByPolicy)
    );
}

#[test]
fn take_over_between_authorization_and_dispatch_stops_the_dispatch() {
    let mut engine = engine();
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request(), MonotonicMillis(0))
    else {
        unreachable!("an ordinary link activation is authorized")
    };

    let take_over = engine.user_took_over(&TabId::new("tab_1"), RevocationReason::UserTookOver);
    assert!(take_over.authority.in_flight.is_empty());

    let decision = engine.authorize_dispatch(
        &authorization.capability_id,
        &proposal(),
        &observed(),
        MonotonicMillis(1),
    );
    assert_eq!(
        decision.refusal_step(),
        Some(StaleNodeStep::ConfirmLeaseAndCapability)
    );
    assert_eq!(
        decision.result_code(),
        Some(ActionResultCode::ActorLeaseMissing)
    );
}
