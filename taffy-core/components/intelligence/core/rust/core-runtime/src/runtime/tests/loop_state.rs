// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

/// Decision 0072: the loop state map lives beside the task sessions, so the
/// close path drops both together and the open path constructs a session with
/// no loop state at all. Nothing transient survives the task it belonged to.
#[test]
fn loop_state_dies_with_its_task_and_is_not_reborn_by_reopen() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = TaskId::new("task-1");
    assert!(runtime.loop_state("task-1").is_none());
    runtime.loop_state_mut("task-1").person_answer = Some("held answer".to_owned());
    runtime.loop_state_mut("task-1").ask_prompt = Some("held subject".to_owned());
    runtime.loop_state_mut("task-1").nested_goal = Some("held nested goal".to_owned());
    assert!(runtime.loop_state("task-1").is_some());

    let Ok(terminals) = runtime.close_task(&task_id) else {
        unreachable!()
    };
    assert!(terminals.is_empty());
    assert!(runtime.loop_state("task-1").is_none());

    // Reopening with its durable workspace binding brings the session back;
    // the transient state does not. The operation identity is fresh because
    // the first open's identity is already settled.
    reopen(&mut runtime, "task-1");
    assert!(runtime.loop_state("task-1").is_none());
    let state = runtime.loop_state_mut("task-1");
    assert!(state.person_answer.is_none());
    assert!(state.ask_prompt.is_none());
    assert!(state.nested_goal.is_none());
    assert!(state.residency.is_none());
    assert!(state.refused.is_none());
    assert!(state.held.is_none());
}

fn reopen(runtime: &mut Runtime, value: &str) {
    let task_id = TaskId::new(value);
    let begin = runtime.begin_open_task(
        load_with_workspace(value, 254),
        operation_id(&format!("reopen-{value}")),
        Deadline::from_millis(100),
        3,
        3,
    );
    let commit = match begin {
        Ok(value) => value.commit,
        Err(_) => unreachable!(),
    };
    let terminal = storage_success(&commit);
    assert!(matches!(
        runtime.complete_open_task(&task_id, terminal, 4),
        Ok(OpenTaskCommitOutcome::Opened(_))
    ));
}

/// The walk and compose splitters answer only for an open task, and both hand
/// back the same lazily created entry the accessors see.
#[test]
fn walk_surfaces_answer_only_for_an_open_task() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    assert!(runtime.walk_surfaces("task-9").is_none());
    assert!(runtime.compose_surfaces("task-9").is_none());
    let Some((task, state)) = runtime.walk_surfaces("task-1") else {
        unreachable!()
    };
    assert_eq!(task.task_id().as_str(), "task-1");
    state.nested_goal = Some("narrowed".to_owned());
    let Some((_, state, _)) = runtime.compose_surfaces("task-1") else {
        unreachable!()
    };
    assert_eq!(state.nested_goal.as_deref(), Some("narrowed"));
}

use bip_types::identity::{FrameId, PageEpoch, TabId};
use bip_types::Sensitivity;
use task_engine::{ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence};

#[test]
fn handover_page_is_invalidated_only_after_exact_commit_and_conversation_stays() {
    for kind in [
        task_engine::EventKind::HandoverRequested,
        task_engine::EventKind::HandoverCompleted,
    ] {
        let calls = Rc::new(RefCell::new(Vec::new()));
        let mut runtime = runtime(false, &calls);
        let task_id = task_id();
        let evidence = handover_page_evidence("tab-live", "https://example.test", "epoch-1");
        let state = runtime.loop_state_mut(task_id.as_str());
        state
            .page
            .replace_source(
                task_engine::SourceId::from_bytes([7; 16]),
                &evidence,
                loop_kernel::context::PageArena::new(),
            )
            .unwrap_or_else(|_| unreachable!());
        state.person_answer = Some("continue the original errand".to_owned());
        let commit = begin_for(&mut runtime, &task_id, "handover-commit");
        // The recording task double emits one event. Substitute the real
        // reducer's handover event to exercise the commit boundary itself.
        let pending = runtime
            .tasks
            .get_mut(task_id.as_str())
            .and_then(|session| session.pending_submit.as_mut())
            .unwrap_or_else(|| unreachable!());
        pending.accepted.events = vec![task_engine::TaskEvent::record(
            kind,
            task_engine::CommandKind::CompleteHandover,
        )];
        assert_eq!(
            runtime
                .loop_state(task_id.as_str())
                .unwrap_or_else(|| unreachable!())
                .page
                .source_ids()
                .len(),
            1
        );
        let wrong = commit.with_body(Completion::StorageCommitted {
            committed_revision: u64::MAX,
        });
        assert!(runtime.complete_commit(&task_id, wrong, 2).is_err());
        assert_eq!(
            runtime
                .loop_state(task_id.as_str())
                .unwrap_or_else(|| unreachable!())
                .page
                .source_ids()
                .len(),
            1
        );
        assert!(matches!(
            runtime.complete_commit(&task_id, storage_success(&commit), 3),
            Ok(CommitOutcome::Committed(_))
        ));
        let state = runtime
            .loop_state(task_id.as_str())
            .unwrap_or_else(|| unreachable!());
        assert_eq!(state.page.source_ids().len(), 0);
        assert_eq!(
            state.person_answer.as_deref(),
            Some("continue the original errand")
        );
    }
}

fn handover_page_evidence(tab_id: &str, origin: &str, epoch: &str) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new(tab_id),
        frame_id: FrameId("frame-main".to_owned()),
        page_epoch: PageEpoch(epoch.to_owned()),
        graph_revision: 9,
        normalized_origin: origin.to_owned(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 1,
            text_byte_count: 4,
        },
        total_bytes: 32,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    }
}
