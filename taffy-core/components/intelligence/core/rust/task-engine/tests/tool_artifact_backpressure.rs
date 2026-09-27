// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable bounds for tool outputs whose bytes remain in browser custody.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{ContentDigest, DigestAlgorithm, DispatchId, TabId};
use task_engine::action::{ActionIntent, ActionProposal, MediaOperation, MediaToolIntent};
use task_engine::command::{Command, CommandEnvelope};
use task_engine::ids::{IdempotencyKey, SequentialIds};
use task_engine::{
    ArtifactCustody, BrowserSessionId, IdempotencyClass, ManualClock, Milestone, Refusal,
    RefusalReason, ToolJobOutcome, ToolJobStatus, TraceId, MAX_RETAINED_TOOL_ARTIFACTS_PER_TASK,
    MAX_RETAINED_TOOL_ARTIFACT_BYTES_PER_TASK,
};

fn m7_seed() -> task_engine::TaskSeed {
    let mut seed = common::seed();
    seed.snapshot.milestone = Milestone::M7;
    seed
}

fn running(seed: task_engine::TaskSeed) -> common::Fixture {
    let mut fixture = common::draft_from(seed);
    fixture.must_apply(Command::StartTask(common::preview()));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

fn proposal(identity: &str) -> ActionProposal {
    ActionProposal::new(
        ActionIntent::MediaTool(MediaToolIntent {
            tool_name: "media.audio.extract".to_owned(),
            operation: MediaOperation::ExtractAudio,
            source_id: format!("download-{identity}"),
            source_browser_session_id: BrowserSessionId::new("browser_session_1")
                .unwrap_or_else(|_| unreachable!()),
            source_bytes: 8,
            tab: TabId::new("tab_1"),
            node: None,
            max_frames: 0,
            idempotency: IdempotencyClass::IdempotentWrite,
        }),
        None,
        IdempotencyKey::new(format!("proposal-{identity}")),
        false,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "0".repeat(64),
        },
    )
}

fn apply(
    reducer: &mut common::TestReducer,
    nonce: &mut u64,
    command: Command,
) -> Result<task_engine::Accepted, Refusal> {
    let envelope = CommandEnvelope::new(
        IdempotencyKey::new(format!("backpressure-{nonce}")),
        reducer.task().revision(),
        TraceId::new("trace_backpressure"),
        command,
    );
    *nonce = nonce.saturating_add(1);
    reducer.apply(envelope)
}

fn record_output(
    reducer: &mut common::TestReducer,
    nonce: &mut u64,
    identity: &str,
    output_bytes: u64,
) -> Result<task_engine::Accepted, Refusal> {
    apply(
        reducer,
        nonce,
        Command::ProposeAction(Box::new(proposal(identity))),
    )
    .unwrap_or_else(|error| panic!("proposal {identity} refused: {error:?}"));
    let action_id = reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == format!("proposal-{identity}"))
        .map_or_else(
            || panic!("proposal {identity} was not recorded"),
            |action| action.action_id().clone(),
        );
    apply(
        reducer,
        nonce,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(common::authorize()),
            dispatch_id: Some(DispatchId::new(format!("dispatch-{identity}"))),
        },
    )
    .unwrap_or_else(|error| panic!("policy {identity} refused: {error:?}"));
    apply(
        reducer,
        nonce,
        Command::RecordToolJobOutcome {
            job_id: task_engine::job_id_for_action(reducer.task().task_id(), &action_id),
            action_id,
            outcome: Box::new(ToolJobOutcome {
                status: ToolJobStatus::Succeeded,
                output_digest: Some("0".repeat(64)),
                output_bytes,
                output_chunks: 1,
            }),
        },
    )
}

#[test]
fn retained_tool_artifact_count_is_bounded_and_rebuilt_from_the_journal() {
    let seed = m7_seed();
    let mut fixture = running(seed.clone());
    let mut nonce = 100_u64;
    for artifact in 0..MAX_RETAINED_TOOL_ARTIFACTS_PER_TASK {
        assert!(record_output(&mut fixture.reducer, &mut nonce, &artifact.to_string(), 1,).is_ok());
    }
    let refusal = record_output(&mut fixture.reducer, &mut nonce, "overflow", 1)
        .expect_err("one output beyond the retained count must be refused");
    assert_eq!(refusal.reason, RefusalReason::ArtifactRegisterFull);
    assert_eq!(
        fixture.reducer.artifacts().count(),
        MAX_RETAINED_TOOL_ARTIFACTS_PER_TASK
    );
    assert!(fixture
        .reducer
        .artifacts()
        .all(|artifact| artifact.custody() == ArtifactCustody::Browser));

    let journal = fixture.reducer.journal().clone();
    let (mut rebuilt, _) = task_engine::Reducer::replay(
        seed,
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        &journal,
    )
    .expect("a live journal must replay");
    let refusal = record_output(&mut rebuilt, &mut nonce, "replayed-overflow", 1)
        .expect_err("replay must reconstruct the retained-output charge");
    assert_eq!(refusal.reason, RefusalReason::ArtifactRegisterFull);
    assert!(rebuilt
        .artifacts()
        .all(|artifact| artifact.custody() == ArtifactCustody::Browser));
}

#[test]
fn retained_tool_artifact_bytes_have_an_independent_ceiling() {
    let mut fixture = running(m7_seed());
    let mut nonce = 300_u64;
    assert!(record_output(
        &mut fixture.reducer,
        &mut nonce,
        "full-byte-budget",
        MAX_RETAINED_TOOL_ARTIFACT_BYTES_PER_TASK,
    )
    .is_ok());
    let refusal = record_output(&mut fixture.reducer, &mut nonce, "one-byte-over", 1)
        .expect_err("a second output cannot exceed the retained byte budget");
    assert_eq!(refusal.reason, RefusalReason::ArtifactRegisterFull);
    assert_eq!(fixture.reducer.artifacts().count(), 1);
}
