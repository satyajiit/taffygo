// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Replaying after a simulated process death reconstructs the run and repeats
//! nothing.
//!
//! Domain model section 18.3 and the milestone M2 exit review both ask the same
//! question: after the isolated core process dies mid-action, does the runtime come
//! back as the same run? `tests/replay_properties.rs` answers it for the task
//! reducer over generated scripts. This suite answers the part that crosses
//! crate boundaries, which is where the interesting mistakes are:
//!
//! - **authority does not come back.** A lease is process-local, and the
//!   capability ledger dies with the process. A rebuilt task holds no authority
//!   and has to ask for it again.
//! - **a consequential action is not repeated.** An attempt that was in flight
//!   ends `OUTCOME_UNKNOWN`, its recovery rule forbids an unattended retry, and
//!   the command that dispatched it is a duplicate that performs nothing.
//! - **completion is not invented.** A rebuild reaches the state the run
//!   reached and no further.
//! - **the audit journal reconstructs identically.** Replaying the recorded
//!   events twice produces the same projection and the same serialized records,
//!   byte for byte.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use audit_engine::{
    audit_record, find_sequence_gaps, replay, Actor, AggregateId, AggregateType, DerivedEventIds,
    EventDraft, EventPayload, EventType, FieldName, Journal, ManualClock, MemoryLog,
    RedactionClass, SchemaVersion, StreamId, TraceId,
};
use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ContentDigest, DigestAlgorithm, DispatchId, FrameId, GraphRevision, MonotonicMillis, PageEpoch,
    ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;
use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::time::SequentialIds as PolicyIds;
use policy_engine::{
    ActionClass, ActionPhase, AllowedRedirects, ControlMode, LeaseRequest, NormalizedOrigin,
    PolicyEngine, PolicyMilestone, PolicyVersion, ProposalDecision, RiskClass,
};
use task_engine::action::ActionState;
use task_engine::command::Command;
use task_engine::journal::TaskJournal;
use task_engine::reducer::{Recovery, Reducer};
use task_engine::task::TaskState;
use task_engine::tool::RecoveryRule;

const TAB: &str = "tab_1";

fn origin() -> NormalizedOrigin {
    policy_engine::origin::normalize_serialization("https://example.test")
        .expect("the fixture origin must normalize")
}

/// A broker holding a lease and one issued capability.
fn broker() -> (PolicyEngine<PolicyIds>, policy_engine::CapabilityId) {
    let mut engine = PolicyEngine::new(PolicyIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    engine
        .issue_lease(
            &LeaseRequest {
                task_id: TaskId::new("task_1"),
                tab_id: TabId::new(TAB),
                control_mode: ControlMode::Assistant,
                expires_at: MonotonicMillis(10_000),
            },
            MonotonicMillis(0),
        )
        .expect("the fixture lease must issue");

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
            tab_id: TabId::new(TAB),
            frame_id: FrameId::new("frame_main"),
            page_epoch: PageEpoch::new("epoch_a"),
            origin: origin(),
            node_id: Some(SemanticNodeId::new("n_link")),
            destination_scope: Some(origin()),
            destination_address: None,
            required_graph_revision: GraphRevision(7),
            allowed_redirects: AllowedRedirects::from_normalized([origin()]),
        },
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval: None,
        expires_at: MonotonicMillis(5_000),
    };
    let ProposalDecision::Authorize(authorization) =
        engine.decide_proposal(&request, MonotonicMillis(1))
    else {
        unreachable!("an ordinary link activation is authorized")
    };
    (engine, authorization.capability_id)
}

/// What one live run left behind when the process died.
struct Died {
    journal: TaskJournal,
    state: TaskState,
    revision: u64,
    action_id: bip_types::identity::ActionId,
    capability_id: policy_engine::CapabilityId,
    audit_events: Vec<audit_engine::EventEnvelope>,
}

/// Runs a task until an action is in flight, then loses the process.
fn die_mid_dispatch() -> Died {
    let (mut engine, capability_id) = broker();
    let mut fixture = common::running();
    let action_id = fixture
        .action_id
        .clone()
        .expect("the running fixture proposed an action");

    let mut audit = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    let stream = StreamId::new(AggregateType::Task, AggregateId::new("task_1"));
    let mut revision = 0;
    let record_audit = |journal: &mut Journal<_, _, MemoryLog>,
                        event_type: EventType,
                        payload: EventPayload,
                        revision: &mut u64| {
        journal
            .record(EventDraft {
                stream: stream.clone(),
                expected_revision: *revision,
                event_type,
                schema_version: SchemaVersion(1),
                actor: Actor::TaskRuntime,
                task_id: Some(AggregateId::new("task_1")),
                trace_id: TraceId::new("trace_test"),
                causation_event_id: None,
                correlation_id: None,
                redaction_class: RedactionClass::Decision,
                payload,
            })
            .expect("the audit append must succeed");
        *revision = revision.saturating_add(1);
    };

    record_audit(
        &mut audit,
        EventType::TaskStarted,
        EventPayload::new().with_identifier(FieldName::TaskId, "task_1"),
        &mut revision,
    );
    record_audit(
        &mut audit,
        EventType::CapabilityIssued,
        EventPayload::new()
            .with_identifier(FieldName::CapabilityId, capability_id.as_str())
            .with_enumerated(FieldName::ActionClass, ActionClass::OpenLink.label()),
        &mut revision,
    );

    // The intent is journalled and the authority is spent: from here the effect
    // cannot be ruled out.
    fixture.must_apply(Command::DispatchAction {
        action_id: action_id.clone(),
        dispatch_id: DispatchId::new("dispatch_0"),
    });
    engine
        .consume_capability(&capability_id, MonotonicMillis(2))
        .expect("the authority is spent when the intent is journalled");
    record_audit(
        &mut audit,
        EventType::ActionDispatchStarted,
        EventPayload::new().with_identifier(FieldName::ActionId, action_id.as_str()),
        &mut revision,
    );

    let died = Died {
        journal: fixture.reducer.journal().clone(),
        state: fixture.reducer.task().state(),
        revision: fixture.reducer.task().revision(),
        action_id,
        capability_id,
        audit_events: audit.log().events().to_vec(),
    };

    // The process dies. Everything process-local goes with it.
    drop(engine);
    drop(fixture);
    died
}

/// Rebuilds the task reducer, exactly as recovery would.
fn rebuild(journal: &TaskJournal) -> (common::TestReducer, Recovery) {
    Reducer::replay(
        common::seed(),
        common::defaults(),
        task_engine::ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        journal,
    )
    .expect("a journal this reducer wrote must replay")
}

#[test]
fn a_rebuild_reconstructs_the_state_the_revision_and_the_journal() {
    let died = die_mid_dispatch();
    let (rebuilt, recovery) = rebuild(&died.journal);

    assert_eq!(rebuilt.task().state(), died.state);
    assert_eq!(rebuilt.task().revision(), died.revision);
    assert_eq!(rebuilt.journal().entries(), died.journal.entries());
    assert_eq!(recovery.state, died.state);
    assert_eq!(recovery.revision, died.revision);
}

#[test]
fn a_rebuild_is_deterministic_so_two_recoveries_agree() {
    let died = die_mid_dispatch();
    let (first, first_recovery) = rebuild(&died.journal);
    let (second, second_recovery) = rebuild(&died.journal);

    assert_eq!(first.task().state(), second.task().state());
    assert_eq!(first.task().revision(), second.task().revision());
    assert_eq!(first.journal().entries(), second.journal().entries());
    assert_eq!(first_recovery, second_recovery);
}

#[test]
fn no_authority_survives_the_process_that_held_it() {
    let died = die_mid_dispatch();
    let (_, recovery) = rebuild(&died.journal);
    assert_eq!(recovery.leases_restored, 0);

    // The broker that comes back is a new one. It knows no lease, no capability,
    // and nothing in the journal can teach it either: a lease is never
    // serialized and a capability record is metadata, not reusable authority.
    let fresh = PolicyEngine::new(PolicyIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    assert!(fresh
        .active_lease(&TabId::new(TAB), MonotonicMillis(0))
        .is_none());
    assert!(fresh.capability(&died.capability_id).is_none());
    assert_eq!(fresh.capabilities().consumption_count(), 0);
}

#[test]
fn an_attempt_that_was_in_flight_comes_back_unknown_and_not_verified() {
    let died = die_mid_dispatch();
    let (rebuilt, recovery) = rebuild(&died.journal);

    assert_eq!(
        recovery.unknown_outcome_actions,
        vec![died.action_id.clone()]
    );
    let action = rebuilt
        .action(&died.action_id)
        .expect("the rebuilt task knows the action");
    assert_eq!(action.state(), ActionState::OutcomeUnknown);
    assert!(!action.is_verified());
    assert_eq!(action.result(), None);
    assert!(recovery.requires_revalidation);
}

#[test]
fn a_consequential_attempt_is_never_retried_without_a_person() {
    let died = die_mid_dispatch();
    let (rebuilt, _) = rebuild(&died.journal);
    let action = rebuilt
        .action(&died.action_id)
        .expect("the rebuilt task knows the action");

    // The rule comes from the tool definition, never from how convenient a
    // retry would be.
    let rule = action.recovery_rule();
    if rule.permits_unattended_retry() {
        // Only a pure read may be repeated unattended. Anything that could have
        // touched the page needs reconciliation or a person.
        assert_eq!(rule, RecoveryRule::RetryWithinEpochAndBudget);
        assert!(!action.proposal().action_class().mutates());
    } else {
        assert!(matches!(
            rule,
            RecoveryRule::RetryAfterStateCheck
                | RecoveryRule::ReconcileFirst
                | RecoveryRule::NeverAutomatically
        ));
    }
}

#[test]
fn replaying_the_dispatch_command_after_a_rebuild_performs_nothing() {
    let died = die_mid_dispatch();
    let (mut rebuilt, _) = rebuild(&died.journal);

    // The command that dispatched the action, presented again with the key the
    // journal already carries. A repeat returns the original receipt and no
    // effect, which is what makes a retry after process death safe.
    let repeat = died
        .journal
        .commands()
        .find(|record| record.envelope.kind() == task_engine::command::CommandKind::DispatchAction)
        .map(|record| record.envelope.clone())
        .expect("the run dispatched an action");

    let accepted = rebuilt
        .apply(repeat)
        .expect("a repeat is accepted, not refused");
    assert!(accepted.duplicate);
    assert!(accepted.effects.is_empty());
    assert_eq!(
        rebuilt
            .action(&died.action_id)
            .map(task_engine::ActionRecord::state),
        Some(ActionState::OutcomeUnknown),
        "a repeat does not move the action out of its unknown outcome"
    );
}

#[test]
fn a_rebuild_never_invents_completion() {
    let died = die_mid_dispatch();
    let (rebuilt, _) = rebuild(&died.journal);
    assert_ne!(died.state, TaskState::Completed);
    assert_ne!(rebuilt.task().state(), TaskState::Completed);
    assert!(rebuilt.task().terminal_result().is_none());
}

#[test]
fn the_audit_journal_replays_to_the_same_projection_and_the_same_bytes() {
    let died = die_mid_dispatch();
    assert!(find_sequence_gaps(&died.audit_events).is_empty());

    let first = replay(&died.audit_events).expect("the audit journal replays");
    let second = replay(&died.audit_events).expect("the audit journal replays twice");
    assert_eq!(first, second);

    let render = |events: &[audit_engine::EventEnvelope]| -> Vec<String> {
        events
            .iter()
            .map(|event| {
                serde_json::to_string(&audit_record(event)).expect("the audit record serializes")
            })
            .collect()
    };
    assert_eq!(render(&died.audit_events), render(&died.audit_events));
}

#[test]
fn the_audit_journal_records_the_dispatch_exactly_once() {
    let died = die_mid_dispatch();
    let dispatched = died
        .audit_events
        .iter()
        .filter(|event| event.event_type() == EventType::ActionDispatchStarted)
        .count();
    assert_eq!(dispatched, 1);

    // And the task journal agrees: one dispatched key, recorded once.
    let keys = died.journal.dispatched_keys();
    let mut unique = keys.clone();
    unique.sort();
    unique.dedup();
    assert_eq!(keys.len(), unique.len());
    assert_eq!(keys.len(), 1);
}
