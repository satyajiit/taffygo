// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict decoding of typed attached-store read results (decision 0133).
//!
//! The browser reduced every row to a reference before it crossed; this
//! module only refuses a row the runtime's own row rule would refuse, a
//! result under another operation's name, and a result on an effect that was
//! not a store read.

use core_runtime::{wire, ActionResultCode};

use super::PendingTaskEffect;
use crate::ffi;

fn fields_are_empty(terminal: &ffi::BridgeTaskTerminal) -> bool {
    terminal.task_store_operation == 0
        && terminal.task_store_rows.is_empty()
        && terminal.task_store_omitted == 0
}

const fn tool_name(operation: wire::TaskActionOperationKind) -> Option<&'static str> {
    match operation {
        wire::TaskActionOperationKind::HistorySearch => Some("history.search"),
        wire::TaskActionOperationKind::HistoryRecent => Some("history.recent"),
        wire::TaskActionOperationKind::BookmarksSearch => Some("bookmarks.search"),
        wire::TaskActionOperationKind::BookmarksList => Some("bookmarks.list"),
        wire::TaskActionOperationKind::OpenTabsList => Some("open_tabs.list"),
        _ => None,
    }
}

pub(super) fn decode(
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    code: ActionResultCode,
) -> Result<Option<core_runtime::TaskStoreActionResult>, ()> {
    let operation = pending.action_operation;
    let expected_tool = operation.and_then(tool_name);
    if code != ActionResultCode::Verified {
        return (!terminal.has_task_store_result && fields_are_empty(terminal))
            .then_some(None)
            .ok_or(());
    }
    if terminal.has_task_store_result != expected_tool.is_some() {
        return Err(());
    }
    let Some(tool) = expected_tool else {
        return fields_are_empty(terminal).then_some(None).ok_or(());
    };
    let result_operation =
        wire::TaskActionOperationKind::from_wire(u32::from(terminal.task_store_operation))
            .ok_or(())?;
    if Some(result_operation) != operation
        || terminal.task_store_rows.len() > wire::MAX_TASK_STORE_RESULTS
    {
        return Err(());
    }
    let rows = terminal
        .task_store_rows
        .iter()
        .map(|row| {
            core_runtime::TaskStoreRow::new(
                row.title.clone(),
                row.host.clone(),
                row.path.clone(),
                row.when_utc_ms,
            )
            .ok_or(())
        })
        .collect::<Result<Vec<_>, ()>>()?;
    core_runtime::TaskStoreActionResult::new(tool, rows, terminal.task_store_omitted)
        .map(Some)
        .map_err(|_| ())
}
