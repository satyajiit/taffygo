// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The journal is append-only, its gaps are detectable, and a replay
//! reconstructs the same projection.
//!
//! Domain model section 17.1 makes three promises about the journal, and this
//! suite is each of them:
//!
//! - an append uses an expected revision, and a conflict is reconciled rather
//!   than resolved by whoever wrote last;
//! - a projection is rebuildable from the journal;
//! - a corrupted journal fails the projection safely instead of producing a
//!   plausible-looking answer.

use audit_engine::projection::ReplayError;
use audit_engine::{
    find_sequence_gaps, replay, Actor, AggregateId, AggregateType, AppendError, AppendOnlyLog,
    DerivedEventIds, EventDraft, EventPayload, EventType, FieldName, Journal, ManualClock,
    MemoryLog, RedactionClass, SchemaVersion, Sequence, StreamId, TaskProjection, TaskState,
    TraceId, MAX_MEMORY_LOG_EVENTS,
};

fn task_stream() -> StreamId {
    StreamId::new(AggregateType::Task, AggregateId::new("task_1"))
}

fn draft(event_type: EventType, expected_revision: u64) -> EventDraft {
    EventDraft {
        stream: task_stream(),
        expected_revision,
        event_type,
        schema_version: SchemaVersion(1),
        actor: Actor::TaskRuntime,
        task_id: Some(AggregateId::new("task_1")),
        trace_id: TraceId::new("trace_1"),
        causation_event_id: None,
        correlation_id: None,
        redaction_class: RedactionClass::Decision,
        payload: EventPayload::new(),
    }
}

/// One ordinary task history: created, run, one action proposed, authorized,
/// dispatched, verified, completed.
const HISTORY: &[EventType] = &[
    EventType::TaskCreated,
    EventType::TaskStarted,
    EventType::ActionProposed,
    EventType::ApprovalRequested,
    EventType::ApprovalDecided,
    EventType::CapabilityIssued,
    EventType::ActionDispatchStarted,
    EventType::ActionVerificationCompleted,
    EventType::UserTookOver,
    EventType::TaskCompleted,
];

type FixtureJournal = Journal<ManualClock, DerivedEventIds, MemoryLog>;

fn recorded_history() -> (FixtureJournal, TaskProjection) {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    let mut live = TaskProjection::new();

    for (index, event_type) in HISTORY.iter().enumerate() {
        let mut plan = draft(*event_type, index as u64);
        if *event_type == EventType::ActionVerificationCompleted {
            plan.payload = EventPayload::new().with_enumerated(FieldName::ResultCode, "VERIFIED");
        }
        match journal.record(plan) {
            // Applying each event as it arrives is what the live projection
            // does in the running product.
            Ok(event) => live.apply(&event),
            Err(error) => unreachable!("the fixture append must succeed: {error:?}"),
        }
    }
    (journal, live)
}

#[test]
fn a_replay_reconstructs_the_projection_the_live_run_produced() {
    let (journal, live) = recorded_history();
    let replayed = match replay(&journal.log().read_stream(&task_stream())) {
        Ok(projection) => projection,
        Err(error) => unreachable!("a whole journal replays: {error:?}"),
    };

    assert_eq!(replayed, live);
    assert_eq!(replayed.state, TaskState::Completed);
    assert_eq!(replayed.actions_proposed, 1);
    assert_eq!(replayed.approvals_requested, 1);
    assert_eq!(replayed.approvals_decided, 1);
    assert_eq!(replayed.capabilities_issued, 1);
    assert_eq!(replayed.dispatches_started, 1);
    assert_eq!(replayed.verifications_completed, 1);
    assert_eq!(replayed.take_overs, 1);
    assert_eq!(replayed.last_result_code, Some("VERIFIED"));
    assert_eq!(replayed.last_sequence, Some(Sequence(10)));
    assert_eq!(replayed.revision, 10);
}

#[test]
fn a_replay_is_reproducible() {
    // Same input, same output: identifiers are derived and the clock is
    // injected, so nothing in the fold varies between runs.
    let (first, _) = recorded_history();
    let (second, _) = recorded_history();
    assert_eq!(
        first.log().read_stream(&task_stream()),
        second.log().read_stream(&task_stream())
    );
    assert_eq!(
        replay(&first.log().read_stream(&task_stream())),
        replay(&second.log().read_stream(&task_stream()))
    );
}

#[test]
fn the_journal_assigns_the_sequence_and_the_caller_cannot() {
    let (journal, _) = recorded_history();
    let events = journal.log().read_stream(&task_stream());
    let sequences: Vec<u64> = events
        .iter()
        .map(|event| event.monotonic_sequence().0)
        .collect();
    assert_eq!(sequences, (1..=10).collect::<Vec<u64>>());

    // Every identifier is distinct, and every revision advances by one.
    let mut ids: Vec<&str> = events
        .iter()
        .map(|event| event.event_id().as_str())
        .collect();
    let count = ids.len();
    ids.sort_unstable();
    ids.dedup();
    assert_eq!(ids.len(), count);
    for (index, event) in events.iter().enumerate() {
        assert_eq!(event.aggregate_revision(), index as u64 + 1);
    }
}

#[test]
fn an_earlier_event_is_never_changed_by_a_later_one() {
    // The log can only grow, so the prefix it held before an append is the
    // prefix it holds after one. There is no API that could do otherwise: the
    // trait has one mutating method and it takes a whole sealed event.
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    let mut prefixes = Vec::new();
    for (index, event_type) in HISTORY.iter().enumerate() {
        assert!(journal.record(draft(*event_type, index as u64)).is_ok());
        prefixes.push(journal.log().read_all());
    }

    for (index, prefix) in prefixes.iter().enumerate() {
        let later = journal.log().read_all();
        assert_eq!(prefix.len(), index + 1);
        assert_eq!(later.get(..prefix.len()), Some(prefix.as_slice()));
    }
}

#[test]
fn a_conflicting_append_is_refused_rather_than_overwriting() {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    assert!(journal.record(draft(EventType::TaskCreated, 0)).is_ok());

    // A second writer that still believes the stream is empty.
    assert_eq!(
        journal.record(draft(EventType::TaskStarted, 0)).err(),
        Some(AppendError::RevisionConflict {
            expected: 0,
            actual: 1
        })
    );
    // Nothing was written, and the stream is where it was.
    assert_eq!(journal.log().len(), 1);
    assert_eq!(journal.revision(&task_stream()), 1);

    // Reconciled by reading the current revision and appending again.
    assert!(journal.record(draft(EventType::TaskStarted, 1)).is_ok());
    assert_eq!(journal.log().len(), 2);
}

#[test]
fn a_journal_reopened_over_an_existing_log_continues_its_sequence() {
    let (journal, _) = recorded_history();
    let restored_log = journal.log().clone();
    let mut reopened = Journal::new(ManualClock::at(2_000), DerivedEventIds, restored_log);

    let appended = match reopened.record(draft(EventType::TaskFailed, HISTORY.len() as u64)) {
        Ok(event) => event,
        Err(error) => unreachable!("a restored journal appends after its durable tail: {error:?}"),
    };
    assert_eq!(appended.monotonic_sequence(), Sequence(11));
    assert_eq!(appended.aggregate_revision(), 11);
    assert!(find_sequence_gaps(&reopened.log().read_stream(&task_stream())).is_empty());
}

#[test]
fn a_missing_event_is_detected_and_refuses_to_produce_a_projection() {
    let (journal, _) = recorded_history();
    let mut events = journal.log().read_stream(&task_stream());
    // Whatever removed it — a truncated read, a partial restore, a corrupted
    // page — the projection must not quietly be built without it.
    events.remove(4);

    let gaps = find_sequence_gaps(&events);
    assert_eq!(gaps.len(), 1);
    assert_eq!(gaps.first().map(|gap| gap.expected), Some(Sequence(5)));
    assert_eq!(gaps.first().map(|gap| gap.found), Some(Sequence(6)));

    assert_eq!(replay(&events), Err(ReplayError::SequenceBroken(gaps)));
}

#[test]
fn a_repeated_or_reordered_event_is_detected_too() {
    let (journal, _) = recorded_history();
    let events = journal.log().read_stream(&task_stream());

    let mut repeated = events.clone();
    let Some(third) = events.get(2).cloned() else {
        unreachable!("the history has ten events")
    };
    repeated.insert(3, third);
    assert!(!find_sequence_gaps(&repeated).is_empty());
    assert!(replay(&repeated).is_err());

    let mut reordered = events;
    reordered.swap(1, 2);
    assert!(!find_sequence_gaps(&reordered).is_empty());
    assert!(replay(&reordered).is_err());
}

#[test]
fn two_streams_are_two_independent_histories() {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    let action_stream = StreamId::new(AggregateType::Action, AggregateId::new("act_1"));

    assert!(journal.record(draft(EventType::TaskCreated, 0)).is_ok());
    let mut action = draft(EventType::ActionProposed, 0);
    action.stream = action_stream.clone();
    assert!(journal.record(action).is_ok());
    assert!(journal.record(draft(EventType::TaskStarted, 1)).is_ok());

    // Each stream numbers its own events from one, and the interleaved log has
    // no gaps in either.
    assert_eq!(journal.next_sequence(&task_stream()), Sequence(3));
    assert_eq!(journal.next_sequence(&action_stream), Sequence(2));
    assert!(find_sequence_gaps(&journal.log().read_all()).is_empty());
}

#[test]
fn the_in_memory_journal_has_constant_time_tails_and_a_hard_ceiling() {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    for revision in 0..MAX_MEMORY_LOG_EVENTS {
        assert!(journal
            .record(draft(EventType::TaskStarted, revision as u64))
            .is_ok());
    }
    assert_eq!(journal.log().len(), MAX_MEMORY_LOG_EVENTS);
    assert_eq!(
        journal
            .record(draft(EventType::TaskStarted, MAX_MEMORY_LOG_EVENTS as u64,))
            .err(),
        Some(AppendError::LogRejected("memory log full"))
    );
}
