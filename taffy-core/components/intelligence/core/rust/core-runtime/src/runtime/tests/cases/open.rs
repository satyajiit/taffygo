// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

#[test]
fn begin_submit_only_encodes_and_withholds_task_effects() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = runtime(false, &calls);
    let task_id = task_id();
    let commit = begin_for(&mut runtime, &task_id, "commit-1");

    assert_eq!(
        calls.borrow().as_slice(),
        &["audit_encode", "storage_encode"]
    );
    assert_eq!(runtime.commit_in_flight(&task_id), Some(true));
    assert_eq!(runtime.pending_len(), 1);
    assert!(matches!(
        commit.body,
        crate::contract::EffectRequest::Storage(_)
    ));
}

#[test]
fn fresh_task_is_hidden_until_creation_commit_and_dropped_on_failure() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = empty_runtime(false, &calls);
    let task_id = task_id();
    let begin = runtime.begin_open_task(
        load(task_id.as_str()),
        operation_id("open-task-1"),
        Deadline::from_millis(100),
        1,
        1,
    );
    let commit = match begin {
        Ok(value) => value.commit,
        Err(_) => unreachable!(),
    };
    assert!(runtime.task(&task_id).is_none());
    assert_eq!(runtime.task_count(), 0);
    assert_eq!(runtime.pending_len(), 1);

    let terminal = commit.with_body(Completion::Failed(RuntimeError::StorageConflict));
    assert!(matches!(
        runtime.complete_open_task(&task_id, terminal, 2),
        Ok(OpenTaskCommitOutcome::NotOpened(_))
    ));
    assert!(runtime.task(&task_id).is_none());
    assert_eq!(runtime.pending_len(), 0);
    assert_eq!(
        runtime.components.workspaces.project_core_api(),
        Ok(Vec::new())
    );
}

#[test]
fn start_task_storage_effect_binds_envelope_to_resulting_revision() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = empty_runtime_with_task_revision(false, &calls, 3);
    let begin = runtime
        .begin_open_task(
            load(task_id().as_str()),
            operation_id("open-task-revision-binding"),
            Deadline::from_millis(100),
            1,
            1,
        )
        .unwrap_or_else(|_| unreachable!("a fresh task emits its creation commit"));
    let storage = match &begin.commit.body {
        EffectRequest::Storage(storage) => storage.to_wire(),
        _ => unreachable!("a fresh task emits a storage commit"),
    };

    assert_eq!(storage.expected_revision, 0);
    assert!(storage.resulting_revision > storage.expected_revision);
    assert_eq!(begin.commit.task_revision, storage.resulting_revision);
}

#[test]
fn zero_source_prepare_is_released_only_after_creation_commit_and_only_once() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = empty_runtime(false, &calls);
    let task_id = TaskId::new("task-discovery");
    let begin = runtime
        .begin_open_task(
            discovery_load(task_id.as_str()),
            operation_id("open-task-discovery"),
            Deadline::from_millis(100),
            1,
            1,
        )
        .unwrap_or_else(|_| unreachable!("the bounded discovery task opens"));
    assert!(runtime.task(&task_id).is_none());
    assert!(matches!(begin.commit.body, EffectRequest::Storage(_)));

    let terminal = storage_success(&begin.commit);
    let outcome = runtime.complete_open_task(&task_id, terminal.clone(), 2);
    assert!(matches!(
        outcome,
        Ok(OpenTaskCommitOutcome::Opened(opened))
            if opened.effects == vec![Effect::PrepareDiscoveryTab {
                browser_session_id: task_engine::BrowserSessionId::new("browser-session-1")
                    .unwrap_or_else(|_| unreachable!()),
                remaining_new_source_cap: 4,
            }]
    ));
    assert!(runtime.task(&task_id).is_some());
    assert_eq!(
        runtime.complete_open_task(&task_id, terminal, 3),
        Err(crate::runtime::OpenTaskCompletionError::NoOpenPending)
    );
}

#[test]
fn failed_zero_source_creation_never_releases_prepare() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = empty_runtime(false, &calls);
    let task_id = TaskId::new("task-discovery-failed");
    let begin = runtime
        .begin_open_task(
            discovery_load(task_id.as_str()),
            operation_id("open-task-discovery-failed"),
            Deadline::from_millis(100),
            1,
            1,
        )
        .unwrap_or_else(|_| unreachable!("the bounded discovery task stages"));
    let terminal = begin
        .commit
        .with_body(Completion::Failed(RuntimeError::StorageConflict));
    assert!(matches!(
        runtime.complete_open_task(&task_id, terminal, 2),
        Ok(OpenTaskCommitOutcome::NotOpened(_))
    ));
    assert!(runtime.task(&task_id).is_none());
    assert_eq!(runtime.pending_len(), 0);
}

#[test]
fn fresh_research_task_atomically_commits_its_derived_workspace() {
    let calls = Rc::new(RefCell::new(Vec::new()));
    let mut runtime = empty_runtime(false, &calls);
    let task_id = task_id();
    let begin = runtime
        .begin_open_task(
            load(task_id.as_str()),
            operation_id("open-with-workspace"),
            Deadline::from_millis(100),
            1,
            1_787_337_000_000,
        )
        .unwrap_or_else(|_| unreachable!("fresh task is valid"));
    let workspace = match &begin.commit.body {
        EffectRequest::Storage(storage) => storage
            .to_wire()
            .workspace
            .unwrap_or_else(|| unreachable!("research task derives a workspace")),
        _ => unreachable!("task creation emits storage"),
    };
    assert_eq!(
        (workspace.expected_revision, workspace.resulting_revision),
        (0, 1)
    );
    assert_eq!(workspace.workspace_id.len(), 32);
    assert!(workspace
        .workspace_id
        .bytes()
        .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)));
    let snapshot = taffy_storage::workspace::decode_snapshot(&workspace.snapshot)
        .unwrap_or_else(|_| unreachable!("workspace bytes are canonical"));
    assert_eq!(snapshot.workspace_id.to_text(), workspace.workspace_id);
    assert_eq!(snapshot.revision, 1);
    assert_eq!(snapshot.goal, "redacted test goal");
    assert!(runtime
        .components
        .workspaces
        .project_core_api()
        .is_ok_and(|items| items.is_empty()));

    let terminal = storage_success(&begin.commit);
    assert!(matches!(
        runtime.complete_open_task(&task_id, terminal, 2),
        Ok(OpenTaskCommitOutcome::Opened(_))
    ));
    assert!(runtime
        .components
        .workspaces
        .project_core_api()
        .is_ok_and(|items| items.len() == 1
            && items[0].workspace_id == workspace.workspace_id
            && items[0].revision == 1));
}
