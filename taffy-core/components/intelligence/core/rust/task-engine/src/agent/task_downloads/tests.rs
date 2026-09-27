// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

fn session() -> BrowserSessionId {
    BrowserSessionId::new("browser-session-1").unwrap_or_else(|_| unreachable!())
}

fn snapshot(id: &str, state: TaskDownloadState) -> TaskDownloadSnapshot {
    TaskDownloadSnapshot::new(
        id.to_owned(),
        state,
        TaskDownloadMediaType::Application,
        42,
        TaskDownloadDirectoryClass::Undecided,
    )
    .unwrap_or_else(|_| unreachable!())
}

#[test]
fn a_list_is_bounded_unique_and_hides_guids() {
    let result = TaskDownloadActionResult::listed(
        session(),
        vec![snapshot(
            "secret-page-authored-guid",
            TaskDownloadState::Complete,
        )],
        false,
    )
    .unwrap_or_else(|_| unreachable!());
    let mut handles = TaskDownloadHandleTable::default();
    let transcript = handles
        .apply_verified(&result)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        transcript.result_pieces().concat(),
        "Downloads: download one (complete)."
    );
    assert!(!transcript
        .result_pieces()
        .concat()
        .contains("secret-page-authored-guid"));
    assert_eq!(
        handles.resolve(1).map(TaskDownloadSnapshot::download_id),
        Some("secret-page-authored-guid")
    );
}

#[test]
fn duplicate_or_oversized_lists_are_refused() {
    let duplicate = snapshot("download-1", TaskDownloadState::InProgress);
    assert_eq!(
        TaskDownloadActionResult::listed(session(), vec![duplicate.clone(), duplicate], false,),
        Err(TaskDownloadResultError::DuplicateDownload)
    );
    let many = (0..=MAX_TASK_DOWNLOAD_RESULTS)
        .map(|index| snapshot(&format!("download-{index}"), TaskDownloadState::Created))
        .collect();
    assert_eq!(
        TaskDownloadActionResult::listed(session(), many, true),
        Err(TaskDownloadResultError::TooManyDownloads)
    );
    assert_eq!(
        TaskDownloadActionResult::listed(
            session(),
            vec![snapshot("download-1", TaskDownloadState::Created)],
            true,
        ),
        Err(TaskDownloadResultError::InvalidTruncation)
    );
}

#[test]
fn list_cannot_rebind_generation_resident_handles_to_another_session() {
    let mut handles = TaskDownloadHandleTable::default();
    handles
        .apply_verified(&TaskDownloadActionResult::started(
            session(),
            snapshot("download-1", TaskDownloadState::Created),
        ))
        .unwrap_or_else(|_| unreachable!());
    let other_session =
        BrowserSessionId::new("browser-session-2").unwrap_or_else(|_| unreachable!());
    let listed = TaskDownloadActionResult::listed(other_session, Vec::new(), false)
        .unwrap_or_else(|_| unreachable!());

    assert_eq!(
        handles.apply_verified(&listed),
        Err(TaskDownloadResultError::WrongBrowserSession)
    );
    assert_eq!(handles.browser_session_id(), Some(&session()));
}

#[test]
fn only_a_task_started_handle_can_be_cancelled() {
    let mut handles = TaskDownloadHandleTable::default();
    let started = TaskDownloadActionResult::started(
        session(),
        snapshot("owned-download", TaskDownloadState::InProgress),
    );
    let transcript = handles
        .apply_verified(&started)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        transcript.result_pieces().concat(),
        "Download one in progress."
    );
    assert!(handles.resolve_task_started(1).is_some());

    let listed = TaskDownloadActionResult::listed(
        session(),
        vec![
            snapshot("manual-download", TaskDownloadState::InProgress),
            snapshot("owned-download", TaskDownloadState::InProgress),
        ],
        false,
    )
    .unwrap_or_else(|_| unreachable!());
    handles
        .apply_verified(&listed)
        .unwrap_or_else(|_| unreachable!());
    assert!(handles.resolve_task_started(1).is_none());
    assert_eq!(
        handles
            .resolve_task_started(2)
            .map(TaskDownloadSnapshot::download_id),
        Some("owned-download")
    );

    let cancelled = TaskDownloadActionResult::cancelled(
        session(),
        snapshot("owned-download", TaskDownloadState::Cancelled),
    )
    .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        handles
            .apply_verified(&cancelled)
            .unwrap_or_else(|_| unreachable!()),
        TaskDownloadTranscriptOutcome::Cancelled
    );
    assert_eq!(
        handles
            .resolve_task_started(2)
            .map(TaskDownloadSnapshot::state),
        Some(TaskDownloadState::Cancelled)
    );
}

#[test]
fn polling_counts_only_owned_progressing_rows_from_a_complete_table() {
    let mut handles = TaskDownloadHandleTable::default();
    assert_eq!(handles.pending_task_downloads(), None);
    for state in [
        TaskDownloadState::Created,
        TaskDownloadState::InProgress,
        TaskDownloadState::Paused,
        TaskDownloadState::Interrupted,
        TaskDownloadState::Cancelled,
        TaskDownloadState::Complete,
    ] {
        handles
            .apply_verified(&TaskDownloadActionResult::started(
                session(),
                snapshot("owned", state),
            ))
            .unwrap_or_else(|_| unreachable!());
        assert_eq!(
            handles.pending_task_downloads(),
            Some(usize::from(matches!(
                state,
                TaskDownloadState::Created | TaskDownloadState::InProgress
            )))
        );
    }
    let manual = TaskDownloadActionResult::listed(
        session(),
        vec![snapshot("manual", TaskDownloadState::InProgress)],
        false,
    )
    .unwrap_or_else(|_| unreachable!());
    handles
        .apply_verified(&manual)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(handles.pending_task_downloads(), Some(0));
    assert_eq!(handles.completed_task_downloads(), 0);
    let truncated = TaskDownloadActionResult::listed(
        session(),
        (0..MAX_TASK_DOWNLOAD_RESULTS)
            .map(|index| snapshot(&format!("manual-{index}"), TaskDownloadState::Created))
            .collect(),
        true,
    )
    .unwrap_or_else(|_| unreachable!());
    handles
        .apply_verified(&truncated)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(handles.pending_task_downloads(), None);
}
