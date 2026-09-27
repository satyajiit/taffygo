// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict decoding of content-free Chromium download witnesses.

use core_runtime::wire;
use core_runtime::{
    ActionResultCode, BrowserSessionId, TaskDownloadActionResult, TaskDownloadDirectoryClass,
    TaskDownloadMediaType, TaskDownloadSnapshot, TaskDownloadState,
};

use crate::ffi;

use super::PendingTaskEffect;

fn fields_are_empty(terminal: &ffi::BridgeTaskTerminal) -> bool {
    terminal.task_download_browser_session_id.is_empty()
        && terminal.task_download_operation == 0
        && terminal.task_download_postcondition == 0
        && terminal.task_download_snapshots.is_empty()
        && !terminal.task_download_truncated
}

fn state(value: u8) -> Result<TaskDownloadState, ()> {
    match wire::TaskDownloadState::from_wire(u32::from(value)) {
        Some(wire::TaskDownloadState::Created) => Ok(TaskDownloadState::Created),
        Some(wire::TaskDownloadState::InProgress) => Ok(TaskDownloadState::InProgress),
        Some(wire::TaskDownloadState::Paused) => Ok(TaskDownloadState::Paused),
        Some(wire::TaskDownloadState::Complete) => Ok(TaskDownloadState::Complete),
        Some(wire::TaskDownloadState::Interrupted) => Ok(TaskDownloadState::Interrupted),
        Some(wire::TaskDownloadState::Cancelled) => Ok(TaskDownloadState::Cancelled),
        None => Err(()),
    }
}

fn media_type(value: u8) -> Result<TaskDownloadMediaType, ()> {
    match wire::TaskDownloadMediaType::from_wire(u32::from(value)) {
        Some(wire::TaskDownloadMediaType::Unknown) => Ok(TaskDownloadMediaType::Unknown),
        Some(wire::TaskDownloadMediaType::Application) => Ok(TaskDownloadMediaType::Application),
        Some(wire::TaskDownloadMediaType::Audio) => Ok(TaskDownloadMediaType::Audio),
        Some(wire::TaskDownloadMediaType::Font) => Ok(TaskDownloadMediaType::Font),
        Some(wire::TaskDownloadMediaType::Image) => Ok(TaskDownloadMediaType::Image),
        Some(wire::TaskDownloadMediaType::Message) => Ok(TaskDownloadMediaType::Message),
        Some(wire::TaskDownloadMediaType::Model) => Ok(TaskDownloadMediaType::Model),
        Some(wire::TaskDownloadMediaType::Multipart) => Ok(TaskDownloadMediaType::Multipart),
        Some(wire::TaskDownloadMediaType::Text) => Ok(TaskDownloadMediaType::Text),
        Some(wire::TaskDownloadMediaType::Video) => Ok(TaskDownloadMediaType::Video),
        None => Err(()),
    }
}

fn directory_class(value: u8) -> Result<TaskDownloadDirectoryClass, ()> {
    match wire::TaskDownloadDirectoryClass::from_wire(u32::from(value)) {
        Some(wire::TaskDownloadDirectoryClass::Undecided) => {
            Ok(TaskDownloadDirectoryClass::Undecided)
        }
        Some(wire::TaskDownloadDirectoryClass::PersonChosen) => {
            Ok(TaskDownloadDirectoryClass::PersonChosen)
        }
        Some(wire::TaskDownloadDirectoryClass::DefaultDownloads) => {
            Ok(TaskDownloadDirectoryClass::DefaultDownloads)
        }
        Some(wire::TaskDownloadDirectoryClass::ApplicationPrivate) => {
            Ok(TaskDownloadDirectoryClass::ApplicationPrivate)
        }
        None => Err(()),
    }
}

fn snapshots(terminal: &ffi::BridgeTaskTerminal) -> Result<Vec<TaskDownloadSnapshot>, ()> {
    terminal
        .task_download_snapshots
        .iter()
        .map(|snapshot| {
            TaskDownloadSnapshot::new(
                snapshot.download_id.clone(),
                state(snapshot.state)?,
                media_type(snapshot.media_type)?,
                snapshot.received_bytes,
                directory_class(snapshot.directory_class)?,
            )
            .map_err(|_| ())
        })
        .collect()
}

pub(super) fn decode(
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    code: ActionResultCode,
) -> Result<Option<TaskDownloadActionResult>, ()> {
    let operation = pending.action_operation;
    let is_task_download = matches!(
        operation,
        Some(
            wire::TaskActionOperationKind::DownloadStart
                | wire::TaskActionOperationKind::DownloadList
                | wire::TaskActionOperationKind::DownloadCancel
        )
    );
    if code != ActionResultCode::Verified {
        return (!terminal.has_task_download_result && fields_are_empty(terminal))
            .then_some(None)
            .ok_or(());
    }
    if terminal.has_task_download_result != is_task_download {
        return Err(());
    }
    if !is_task_download {
        return fields_are_empty(terminal).then_some(None).ok_or(());
    }

    let result_operation =
        wire::TaskActionOperationKind::from_wire(u32::from(terminal.task_download_operation))
            .ok_or(())?;
    if Some(result_operation) != operation || terminal.task_download_browser_session_id.is_empty() {
        return Err(());
    }
    let browser_session_id =
        BrowserSessionId::new(terminal.task_download_browser_session_id.clone()).map_err(|_| ())?;
    let postcondition =
        wire::TaskDownloadPostcondition::from_wire(u32::from(terminal.task_download_postcondition))
            .ok_or(())?;
    let mut downloads = snapshots(terminal)?;

    match (result_operation, postcondition) {
        (
            wire::TaskActionOperationKind::DownloadStart,
            wire::TaskDownloadPostcondition::Started,
        ) if downloads.len() == 1 && !terminal.task_download_truncated => {
            let Some(download) = downloads.pop() else {
                return Err(());
            };
            Ok(Some(TaskDownloadActionResult::started(
                browser_session_id,
                download,
            )))
        }
        (wire::TaskActionOperationKind::DownloadList, wire::TaskDownloadPostcondition::Listed) => {
            TaskDownloadActionResult::listed(
                browser_session_id,
                downloads,
                terminal.task_download_truncated,
            )
            .map(Some)
            .map_err(|_| ())
        }
        (
            wire::TaskActionOperationKind::DownloadCancel,
            wire::TaskDownloadPostcondition::Cancelled,
        ) if downloads.len() == 1 && !terminal.task_download_truncated => {
            let Some(download) = downloads.pop() else {
                return Err(());
            };
            TaskDownloadActionResult::cancelled(browser_session_id, download)
                .map(Some)
                .map_err(|_| ())
        }
        _ => Err(()),
    }
}
