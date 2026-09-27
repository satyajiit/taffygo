// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Pausing and stopping revoke authority before anything waits on remote work.
//!
//! This is the milestone M3 exit criterion "stop and take over revoke the actor
//! lease before another browser action", tested against the real
//! `policy-engine` rather than against a stand-in. The test walks the effect
//! list the reducer returns in order, performs each effect against a live
//! broker, and records the state of the lease and the capability the first time
//! an effect waits on something outside the ordered core sequence.
//!
//! If revocation were second in the list, the recorded lease would still be
//! standing at that moment and the test would fail.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ContentDigest, DigestAlgorithm, FrameId, GraphRevision, MonotonicMillis, PageEpoch, ProfileId,
    SemanticNodeId, TabId, TaskId,
};
use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionPhase, AllowedRedirects, CapabilityId, CapabilityState, ControlMode,
    LeaseRequest, NormalizedOrigin, PolicyEngine, PolicyMilestone, PolicyVersion, ProposalDecision,
};
use task_engine::command::{Command, CommandKind, PauseCause};
use task_engine::effect::{revocation_precedes_remote_wait, Effect};
use task_engine::task::TaskState;
use task_engine::transition::disposition;

const NOW: MonotonicMillis = MonotonicMillis(100);

fn origin() -> NormalizedOrigin {
    match policy_engine::origin::normalize_serialization("https://example.test") {
        Ok(origin) => origin,
        Err(_) => unreachable!("the fixture origin must normalize"),
    }
}

/// A broker holding one standing lease and one undispatched capability.
struct Broker {
    engine: PolicyEngine<SequentialIds>,
    tab_id: TabId,
    capability_id: CapabilityId,
}

fn broker() -> Broker {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(1));
    let tab_id = TabId::new("tab_1");
    let issued = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: tab_id.clone(),
            control_mode: ControlMode::Assistant,
            expires_at: MonotonicMillis(10_000),
        },
        MonotonicMillis(0),
    );
    assert!(issued.is_ok(), "the fixture lease must issue");

    let request = CapabilityRequest {
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
            tab_id: tab_id.clone(),
            frame_id: FrameId::new("frame_main"),
            page_epoch: PageEpoch::new("epoch_a"),
            origin: origin(),
            node_id: Some(SemanticNodeId::new("n_link")),
            destination_scope: Some(origin()),
            destination_address: None,
            required_graph_revision: GraphRevision(7),
            allowed_redirects: AllowedRedirects::from_normalized([origin()]),
        },
        data_classes: bip_types::sensitivity::SensitivitySet::EMPTY,
        context_risk: policy_engine::RiskClass::LocalRead,
        approval: None,
        expires_at: MonotonicMillis(5_000),
    };
    let decision = engine.decide_proposal(&request, NOW);
    let ProposalDecision::Authorize(authorization) = decision else {
        unreachable!("the read-oriented surface authorizes an ordinary link")
    };
    Broker {
        engine,
        tab_id,
        capability_id: authorization.capability_id,
    }
}

/// What the world looked like the first time an effect waited on it.
#[derive(Debug, PartialEq, Eq)]
struct AtFirstWait {
    lease_standing: bool,
    capability_state: Option<CapabilityState>,
    waiting_effect: &'static str,
}

/// Performs `effects` in order and reports the world at the first remote wait.
fn perform(broker: &mut Broker, effects: &[Effect]) -> Option<AtFirstWait> {
    let mut observed = None;
    for effect in effects {
        if effect.waits_on_remote_work() && observed.is_none() {
            observed = Some(AtFirstWait {
                lease_standing: broker.engine.active_lease(&broker.tab_id, NOW).is_some(),
                capability_state: broker
                    .engine
                    .capability(&broker.capability_id)
                    .map(|capability| capability.state_at(NOW)),
                waiting_effect: effect.label(),
            });
        }
        if let Effect::RevokeAuthority { reason } = effect {
            let policy_reason = match reason {
                task_engine::RevocationReason::UserTookOver => {
                    policy_engine::RevocationReason::UserTookOver
                }
                task_engine::RevocationReason::DirectUserInput => {
                    policy_engine::RevocationReason::DirectUserInput
                }
                task_engine::RevocationReason::TaskCancelled => {
                    policy_engine::RevocationReason::TaskCancelled
                }
                task_engine::RevocationReason::PolicyRevoked => {
                    policy_engine::RevocationReason::PolicyRevoked
                }
                task_engine::RevocationReason::TabClosed => {
                    policy_engine::RevocationReason::TabClosed
                }
            };
            broker.engine.user_took_over(&broker.tab_id, policy_reason);
        }
    }
    observed
}

#[test]
fn pausing_revokes_the_lease_and_the_capability_before_waiting() {
    let mut fixture = common::running();
    let mut broker = broker();
    assert!(broker.engine.active_lease(&broker.tab_id, NOW).is_some());

    let envelope = fixture.envelope(Command::PauseTask {
        cause: PauseCause::User,
    });
    let committed = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|refusal| unreachable!("pausing a running task: {refusal:?}"));
    assert_eq!(committed.to, TaskState::Pausing);
    assert!(revocation_precedes_remote_wait(&committed.effects));

    let observed = perform(&mut broker, &committed.effects);
    assert_eq!(
        observed,
        Some(AtFirstWait {
            lease_standing: false,
            capability_state: Some(CapabilityState::Revoked),
            waiting_effect: "await_in_flight_work",
        })
    );
}

#[test]
fn pause_closes_a_one_use_approval_before_resume() {
    let mut fixture = common::in_state(TaskState::AwaitingConsent, true);
    let action_id = fixture
        .action_id
        .clone()
        .unwrap_or_else(|| unreachable!("the fixture carries an action"));
    fixture.must_apply(Command::ApproveAction {
        approval: common::receipt(),
        still_current: true,
        expires_at_monotonic_ms: 50_000,
        expires_at_utc_ms: 50_000,
        browser_session_id: task_engine::BrowserSessionId::new("browser_session_1")
            .unwrap_or_else(|_| unreachable!("valid session identity")),
    });
    assert_eq!(
        fixture
            .reducer
            .action(&action_id)
            .map(task_engine::ActionRecord::state),
        Some(task_engine::ActionState::Proposed)
    );

    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::User,
    });
    fixture.must_apply(Command::PauseSettled);
    fixture.must_apply(Command::ResumeTask);

    let action = fixture
        .reducer
        .action(&action_id)
        .unwrap_or_else(|| unreachable!("the historical action remains recorded"));
    assert_eq!(fixture.reducer.task().state(), TaskState::Queued);
    assert_eq!(action.state(), task_engine::ActionState::Cancelled);
    assert!(
        action.approval().is_some(),
        "the receipt remains audit history"
    );
}

#[test]
fn takeover_and_stop_cancel_the_pending_approval_before_settling() {
    for command in [Command::TakeOver, Command::CancelTask] {
        let mut fixture = common::in_state(TaskState::AwaitingConsent, true);
        let action_id = fixture
            .action_id
            .clone()
            .unwrap_or_else(|| unreachable!("the fixture carries an action"));
        fixture.must_apply(command);
        assert_eq!(
            fixture
                .reducer
                .action(&action_id)
                .map(task_engine::ActionRecord::state),
            Some(task_engine::ActionState::Cancelled)
        );
    }
}

#[test]
fn stopping_revokes_the_lease_and_the_capability_before_waiting() {
    let mut fixture = common::running();
    let mut broker = broker();

    let envelope = fixture.envelope(Command::CancelTask);
    let committed = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|refusal| unreachable!("stopping a running task: {refusal:?}"));
    assert_eq!(committed.to, TaskState::Cancelling);

    let observed = perform(&mut broker, &committed.effects);
    assert_eq!(
        observed.as_ref().map(|at| at.lease_standing),
        Some(false),
        "authority was still standing when the runtime began to wait"
    );
    assert_eq!(
        observed.map(|at| at.capability_state),
        Some(Some(CapabilityState::Revoked))
    );
}

#[test]
fn taking_over_revokes_the_lease_and_the_capability_before_waiting() {
    let mut fixture = common::running();
    let mut broker = broker();

    let envelope = fixture.envelope(Command::TakeOver);
    let committed = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|refusal| unreachable!("taking over a running task: {refusal:?}"));
    assert_eq!(committed.to, TaskState::Pausing);

    let observed = perform(&mut broker, &committed.effects);
    assert_eq!(observed.map(|at| at.lease_standing), Some(false));
}

#[test]
fn denying_an_approval_revokes_before_waiting() {
    let mut fixture = common::in_state(TaskState::AwaitingConsent, true);
    let mut broker = broker();

    let envelope = fixture.envelope(Command::DenyAction {
        approval: common::receipt(),
    });
    let committed = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|refusal| unreachable!("denying an approval: {refusal:?}"));
    assert_eq!(committed.to, TaskState::Pausing);

    let observed = perform(&mut broker, &committed.effects);
    assert_eq!(observed.map(|at| at.lease_standing), Some(false));
}

#[test]
fn every_command_that_enters_a_settling_state_revokes_first() {
    for state in TaskState::ALL {
        for kind in CommandKind::ALL {
            let cell = disposition(*state, *kind);
            let enters_settling = cell.targets().iter().any(|target| target.is_settling());
            if !enters_settling {
                continue;
            }
            let mut fixture = common::for_cell(*state, cell.guards());
            let command = common::command::command_for(*kind, &fixture);
            let envelope = fixture.envelope(command);
            let committed = fixture.reducer.apply(envelope).unwrap_or_else(|refusal| {
                panic!("{} x {}: {refusal:?}", state.label(), kind.label())
            });
            assert!(
                committed
                    .effects
                    .iter()
                    .any(task_engine::effect::Effect::revokes_authority),
                "{} x {} enters {} without revoking authority",
                state.label(),
                kind.label(),
                committed.to.label()
            );
            assert!(
                revocation_precedes_remote_wait(&committed.effects),
                "{} x {} waits on remote work while authority still stands",
                state.label(),
                kind.label()
            );
        }
    }
}

#[test]
fn a_terminal_task_issues_no_new_action_and_cancels_authority_that_arrives_late() {
    for terminal in TaskState::TERMINAL {
        let mut fixture = common::in_state(*terminal, false);

        let envelope = fixture.envelope(Command::ProposeAction(Box::new(common::proposal(
            "late_proposal",
        ))));
        assert_eq!(
            fixture.reducer.apply(envelope).err().map(|r| r.reason),
            Some(task_engine::RefusalReason::TaskIsTerminal),
            "{} accepted a proposal",
            terminal.label()
        );

        let Some(action_id) = fixture.action_id.clone() else {
            continue;
        };
        let envelope = fixture.envelope(Command::DispatchAction {
            action_id: action_id.clone(),
            dispatch_id: bip_types::identity::DispatchId::new("dispatch_late"),
        });
        assert_eq!(
            fixture.reducer.apply(envelope).err().map(|r| r.reason),
            Some(task_engine::RefusalReason::TaskIsTerminal),
            "{} accepted a dispatch",
            terminal.label()
        );

        // Authority that arrives after the task ended cancels the action; it
        // never authorizes it.
        let envelope = fixture.envelope(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(common::authorize()),
            dispatch_id: None,
        });
        let committed = fixture
            .reducer
            .apply(envelope)
            .unwrap_or_else(|refusal| unreachable!("a late decision is recorded: {refusal:?}"));
        assert_eq!(committed.to, *terminal);
        assert_eq!(
            fixture
                .reducer
                .action(&action_id)
                .map(task_engine::ActionRecord::state),
            Some(task_engine::ActionState::Cancelled),
            "{} authorized an action after it ended",
            terminal.label()
        );
    }
}

#[test]
fn a_nonrunning_task_cancels_authority_that_arrives_late() {
    for state in [
        TaskState::AwaitingConsent,
        TaskState::Queued,
        TaskState::WaitingUser,
        TaskState::Pausing,
        TaskState::Paused,
        TaskState::Cancelling,
        TaskState::Completing,
    ] {
        let mut fixture = common::in_state(state, state == TaskState::AwaitingConsent);
        let action_id = fixture
            .action_id
            .clone()
            .unwrap_or_else(|| unreachable!("the fixture carries an action"));
        let envelope = fixture.envelope(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(common::authorize()),
            dispatch_id: None,
        });
        let committed = fixture.reducer.apply(envelope).unwrap_or_else(|refusal| {
            unreachable!(
                "a late decision is recorded in {}: {refusal:?}",
                state.label()
            )
        });
        assert_eq!(committed.to, state);
        assert!(committed.effects.is_empty());
        assert_eq!(
            fixture
                .reducer
                .action(&action_id)
                .map(task_engine::ActionRecord::state),
            Some(task_engine::ActionState::Cancelled),
            "{} accepted authority after it stopped running",
            state.label()
        );
    }
}
